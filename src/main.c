// omarchy-aquarium — a GLSL underwater scene drawn on a Wayland layer-shell
// surface, one per output. Sits on the "bottom" layer by default, which is
// above the wallpaper and below every window, so Omarchy's own background
// (and its solar rotation) is left completely untouched.

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <malloc.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "hypr.h"
#include "palette.h"
#include "seeds.h"
#include "anim.h"
#include "shader_frag.h"

#define MAX_OUTPUTS 8

struct output {
	struct wl_output *wl_output;
	uint32_t global_name;
	int32_t scale;
	char name[64];

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_egl_window *egl_window;
	EGLSurface egl_surface;

	int32_t refresh_mhz; /* current mode, 0 if unknown */
	GLuint lut_tex;      /* per-column floor height + slope */
	int lut_w;
	int width, height;   /* logical, from the layer-surface configure */
	int bw, bh;          /* buffer pixels */
	double next_due;     /* wall-clock time this output should draw again */
	bool configured;
	int inflight;        /* commits whose frame callback has not returned */
	bool frame_drawn;    /* next frame already drawn, awaiting its commit */
	bool swap_interval_set;
	bool used;
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct output outputs[MAX_OUTPUTS];

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLConfig egl_config;

static GLuint program, lut_program, ray_program, vbo;
static GLuint lut_fbo, ray_fbo, ray_tex;
static GLint lut_u_res, u_floorlut;
static GLint ray_u_res, ray_u_time, u_raylut;
#define RAY_LUT_W 2048
static GLint u_res, u_time, u_deep, u_shallow, u_light, u_accent, u_fish;
static GLint u_weed, u_jelly, u_anem, u_star, u_turtle, u_sun, u_day;
static bool gl_ready;

static volatile sig_atomic_t running = 1;
static double start_time;

/* options */
static double opt_fps = 60.0;
static bool opt_stats = false;
static bool opt_suspend = true;
static int opt_buffer_scale = 0;      /* 0 = follow the output */
static unsigned long stat_frames;      /* frames rendered */
static unsigned long presented_frames; /* frame callbacks returned */
static bool suspended;
static int hypr_fd = -1;
static double opt_speed = 1.0;
static float opt_fish = 11.0f;
static float opt_weed = 14.0f;
static float opt_jelly = 3.0f;
static float opt_anem = 2.0f;
static float opt_star = 2.0f;
static float opt_turtle = 1.0f;
static const char *opt_output = NULL;
static char *opt_shader_src = NULL;
static enum zwlr_layer_shell_v1_layer opt_layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
static double opt_battery_fps = 10.0;  /* 0 = never throttle on battery */
static bool opt_solar = true;

/* Sun state: the tuned anchor is the default, and what the moon uses. */
static float sun_x = -0.34f, sun_y = 0.68f, day_f = 1.0f;
static bool sun_fixed;                 /* --sun given: never recompute */
static double loc_lat, loc_lon;
static bool have_loc;

static bool on_battery;

static struct palette pal;
static struct seeds seed_data;

static double now_sec(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void on_signal(int sig) {
	(void)sig;
	running = 0;
}

/* ---------------------------------------------------------------- solar */

/* Location comes from the same file Omarchy's solar wallpaper rotation uses,
 * so the water and the wallpaper behind it agree on where the sun is. */
static void load_location(void) {
	char path[512];
	snprintf(path, sizeof(path), "%s/.config/omarchy/dynamic-wallpaper.json",
	         getenv("HOME") ? getenv("HOME") : "");
	FILE *f = fopen(path, "rb");
	if (!f) return;
	char buf[8192];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	const char *la = strstr(buf, "\"latitude\"");
	const char *lo = strstr(buf, "\"longitude\"");
	if (!la || !lo) return;
	la = strchr(la, ':');
	lo = strchr(lo, ':');
	if (!la || !lo) return;
	loc_lat = strtod(la + 1, NULL);
	loc_lon = strtod(lo + 1, NULL);
	have_loc = true;
}

/* NOAA-style approximation; a degree of error moves the sun less than the
 * waves do. Elevation and azimuth in degrees, azimuth 0=N 90=E 180=S. */
static void solar_angles(time_t when, double *elev, double *az) {
	double d = when / 86400.0 + 2440587.5 - 2451545.0;
	double g = fmod(357.529 + 0.98560028 * d, 360.0) * M_PI / 180.0;
	double q = fmod(280.459 + 0.98564736 * d, 360.0);
	double L = fmod(q + 1.915 * sin(g) + 0.020 * sin(2.0 * g), 360.0) * M_PI / 180.0;
	double ob = (23.439 - 0.00000036 * d) * M_PI / 180.0;
	double ra = atan2(cos(ob) * sin(L), cos(L));
	double dec = asin(sin(ob) * sin(L));
	double gmst = fmod(18.697374558 + 24.06570982441908 * d, 24.0);
	double ha = fmod(gmst + loc_lon / 15.0, 24.0) * 15.0 * M_PI / 180.0 - ra;
	double lat = loc_lat * M_PI / 180.0;
	*elev = asin(sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha)) * 180.0 / M_PI;
	double a = atan2(sin(ha), cos(ha) * sin(lat) - tan(dec) * cos(lat));
	*az = fmod(a * 180.0 / M_PI + 180.0, 360.0);
	if (*az < 0.0) *az += 360.0;
}

static void update_solar(void) {
	if (sun_fixed || !opt_solar || !have_loc) return;
	static double last;
	double now = now_sec();
	if (last != 0.0 && now - last < 60.0) return;
	last = now;

	double elev, az;
	solar_angles(time(NULL), &elev, &az);

	/* Civil twilight ramp: full night 6 degrees under the horizon. */
	double day = (elev + 6.0) / 12.0;
	if (day < 0.0) day = 0.0;
	if (day > 1.0) day = 1.0;

	/* Facing south: east rises on the left, west sets on the right. */
	double sx = (az - 180.0) / 110.0;
	if (sx < -1.0) sx = -1.0;
	if (sx > 1.0) sx = 1.0;
	sx *= 0.85;
	double sy = 0.60 + 0.30 * sin((elev > 0.0 ? elev : 0.0) * M_PI / 180.0);

	/* Through twilight, hand over to the moon's fixed anchor. */
	sun_x = (float)(sx * day + -0.34 * (1.0 - day));
	sun_y = (float)(sy * day + 0.72 * (1.0 - day));
	day_f = (float)day;
}

/* -------------------------------------------------------------- battery */

static void update_battery(void) {
	if (opt_battery_fps <= 0.0) return;
	static double last;
	double now = now_sec();
	if (last != 0.0 && now - last < 5.0) return;
	last = now;
	FILE *f = fopen("/sys/class/power_supply/macsmc-ac/online", "rb");
	if (!f) return;
	int c = fgetc(f);
	fclose(f);
	if (c == '0' || c == '1') on_battery = (c == '0');
}

/* Set by the governor when the display rate cannot be held: a perfectly
 * even half-rate beats a juddering two-thirds rate. 0 = uncapped. */
static double gov_cap;
static double gov_window_start;
static unsigned long gov_window_frames;
static int gov_bad_windows;
static double gov_locked_at;
static double gov_preheat_until;   /* saturate the GPU until then */
static bool gov_started;

static double effective_fps(void) {
	double fps = opt_fps;
	if (on_battery && opt_battery_fps > 0.0 && opt_battery_fps < fps)
		fps = opt_battery_fps;
	if (gov_cap > 0.0 && gov_cap < fps)
		fps = gov_cap;
	return fps;
}

/* The GPU's DVFS makes full rate bistable: miss the deadline and the clocks
 * sag, guaranteeing the next miss. When a few seconds of trying prove the
 * display rate cannot be held, lock the exact half rate — an even cadence
 * reads smoother than a faster, uneven one — and retry now and then, in
 * case the load lightens or a driver update raises the ceiling. */
static void update_governor(double refresh_hz) {
	double base = opt_fps;
	if (on_battery && opt_battery_fps > 0.0 && opt_battery_fps < base)
		base = opt_battery_fps;
	if (refresh_hz <= 0.0 || base < refresh_hz - 0.5) return;

	double now = now_sec();
	if (suspended) { gov_window_start = 0.0; return; }

	if (gov_cap > 0.0) {
		if (now - gov_locked_at >= 120.0) {
			gov_cap = 0.0;
			gov_bad_windows = 0;
			gov_window_start = 0.0;
			gov_preheat_until = now + 2.0;
		}
		return;
	}

	if (!gov_started) {
		/* First probe: warm the clocks before judging. */
		gov_started = true;
		gov_preheat_until = now + 2.0;
	}
	if (now < gov_preheat_until) {
		/* The DVFS governor only grants full clocks to a saturated GPU;
		 * measuring during the ramp would condemn a rate the hot clocks
		 * can hold. Keep the window closed until the burst is over. */
		gov_window_start = 0.0;
		return;
	}
	if (gov_window_start == 0.0) {
		gov_window_start = now;
		gov_window_frames = presented_frames;
		return;
	}
	if (now - gov_window_start < 5.0) return;

	double fps = (double)(presented_frames - gov_window_frames)
	           / (now - gov_window_start);
	gov_window_start = now;
	gov_window_frames = presented_frames;
	/* The scene glides slowly, so a near-full rate with the odd dropped
	 * frame still reads smoother than an even half rate; only a real
	 * collapse is worth trading for the locked cadence. */
	if (fps < refresh_hz * 0.75) {
		if (++gov_bad_windows >= 3) {
			gov_cap = refresh_hz / 2.0;
			gov_locked_at = now;
			gov_bad_windows = 0;
			fprintf(stderr, "aquarium: %.0f fps not holding (%.1f delivered), "
			        "locking an even %.0f; retrying in 2 min\n",
			        refresh_hz, fps, gov_cap);
		}
	} else {
		gov_bad_windows = 0;
	}
}

/* ------------------------------------------------------------------ GL */

static GLuint compile(GLenum type, const char *src, const char *what) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "aquarium: %s shader failed to compile:\n%s\n", what, log);
		exit(1);
	}
	return s;
}

static const char *VERT =
	"attribute vec2 aPos;\n"
	"void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static void init_gl(void) {
	const char *frag = opt_shader_src ? opt_shader_src : AQUARIUM_FRAG;
	GLuint vs = compile(GL_VERTEX_SHADER, VERT, "vertex");
	GLuint fs = compile(GL_FRAGMENT_SHADER, frag, "fragment");
	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glBindAttribLocation(program, 0, "aPos");
	glLinkProgram(program);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		fprintf(stderr, "aquarium: link failed:\n%s\n", log);
		exit(1);
	}
	glDeleteShader(vs);
	glDeleteShader(fs);

	/* Second variant of the same source: the floor-LUT bake pass. */
	{
		size_t n = strlen(frag) + 64;
		char *lsrc = malloc(n);
		snprintf(lsrc, n, "#define FLOOR_LUT_PASS\n%s", frag);
		GLuint lvs = compile(GL_VERTEX_SHADER, VERT, "vertex");
		GLuint lfs = compile(GL_FRAGMENT_SHADER, lsrc, "floor-LUT fragment");
		free(lsrc);
		lut_program = glCreateProgram();
		glAttachShader(lut_program, lvs);
		glAttachShader(lut_program, lfs);
		glBindAttribLocation(lut_program, 0, "aPos");
		glLinkProgram(lut_program);
		GLint lok = 0;
		glGetProgramiv(lut_program, GL_LINK_STATUS, &lok);
		if (!lok) {
			fprintf(stderr, "aquarium: floor-LUT link failed\n");
			exit(1);
		}
		glDeleteShader(lvs);
		glDeleteShader(lfs);
		lut_u_res = glGetUniformLocation(lut_program, "uRes");
	}

	/* Third variant: the per-frame ray-strip bake. */
	{
		size_t n = strlen(frag) + 64;
		char *rsrc = malloc(n);
		snprintf(rsrc, n, "#define RAY_LUT_PASS\n%s", frag);
		GLuint rvs = compile(GL_VERTEX_SHADER, VERT, "vertex");
		GLuint rfs = compile(GL_FRAGMENT_SHADER, rsrc, "ray-LUT fragment");
		free(rsrc);
		ray_program = glCreateProgram();
		glAttachShader(ray_program, rvs);
		glAttachShader(ray_program, rfs);
		glBindAttribLocation(ray_program, 0, "aPos");
		glLinkProgram(ray_program);
		GLint rok = 0;
		glGetProgramiv(ray_program, GL_LINK_STATUS, &rok);
		if (!rok) {
			fprintf(stderr, "aquarium: ray-LUT link failed\n");
			exit(1);
		}
		glDeleteShader(rvs);
		glDeleteShader(rfs);
		ray_u_res = glGetUniformLocation(ray_program, "uRes");
		ray_u_time = glGetUniformLocation(ray_program, "uTime");

		glGenTextures(1, &ray_tex);
		glBindTexture(GL_TEXTURE_2D, ray_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, RAY_LUT_W, 1, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glGenFramebuffers(1, &ray_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, ray_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D, ray_tex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	/* All programs are linked for good; let the driver drop its compiler. */
	glReleaseShaderCompiler();

	/* The entity seeds are uniforms, set once and never touched again. */
	glUseProgram(program);
	seeds_compute(&seed_data);
	seeds_upload(program, &seed_data);

	u_res = glGetUniformLocation(program, "uRes");
	u_floorlut = glGetUniformLocation(program, "uFloorLUT");
	u_raylut = glGetUniformLocation(program, "uRayLUT");
	u_time = glGetUniformLocation(program, "uTime");
	u_deep = glGetUniformLocation(program, "uDeep");
	u_shallow = glGetUniformLocation(program, "uShallow");
	u_light = glGetUniformLocation(program, "uLight");
	u_accent = glGetUniformLocation(program, "uAccent");
	u_fish = glGetUniformLocation(program, "uFishCount");
	u_weed = glGetUniformLocation(program, "uWeedCount");
	u_jelly = glGetUniformLocation(program, "uJellyCount");
	u_anem = glGetUniformLocation(program, "uAnemCount");
	u_star = glGetUniformLocation(program, "uStarCount");
	u_turtle = glGetUniformLocation(program, "uTurtle");
	u_sun = glGetUniformLocation(program, "uSun");
	u_day = glGetUniformLocation(program, "uDay");

	/* One oversized triangle covering the clip volume. */
	static const GLfloat tri[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);

	gl_ready = true;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t t);
static const struct wl_callback_listener frame_listener = {.done = frame_done};

/* True when this output runs at the display's own rate: presentation then
 * paces off frame callbacks and the GPU is kept fed by pre-rendering. */
static bool at_refresh(const struct output *o) {
	return o->refresh_mhz > 0 &&
	       effective_fps() * 1000.0 >= (double)o->refresh_mhz - 500.0;
}

/* The floor height field depends only on the pixel column, so it is baked
 * once per output size into a width x 1 texture by the same shader source:
 * one fetch replaces two four-octave fbms on every pixel of the lower scene,
 * and the values are the very ones the per-pixel evaluation produced. */
static void build_floor_lut(struct output *o) {
	if (!o->lut_tex) glGenTextures(1, &o->lut_tex);
	glBindTexture(GL_TEXTURE_2D, o->lut_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, o->bw, 1, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (!lut_fbo) glGenFramebuffers(1, &lut_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, lut_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_2D, o->lut_tex, 0);
	glViewport(0, 0, o->bw, 1);
	glUseProgram(lut_program);
	glUniform2f(lut_u_res, (float)o->bw, 1.0f);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	o->lut_w = o->bw;
}

/* While the governor probes the display rate, draw one extra throwaway
 * frame per cycle into a scratch target: the doubled load saturates the GPU
 * so the DVFS governor grants the clocks the probe is trying to measure.
 * A separate target, because a second draw into the un-presented back
 * buffer would be discarded by hidden-surface removal and cost nothing. */
static GLuint heat_fbo, heat_tex;
static int heat_w, heat_h;

static void heat_pulse(int w, int h, int n) {
	if (!heat_tex || heat_w != w || heat_h != h) {
		if (!heat_tex) glGenTextures(1, &heat_tex);
		glBindTexture(GL_TEXTURE_2D, heat_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		heat_w = w;
		heat_h = h;
	}
	if (!heat_fbo) glGenFramebuffers(1, &heat_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, heat_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_2D, heat_tex, 0);
	glViewport(0, 0, w, h);
	for (int burst = 0; burst < n; burst++) {
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glFlush();
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void preheat_drop(void) {
	if (heat_tex) { glDeleteTextures(1, &heat_tex); heat_tex = 0; }
	if (heat_fbo) { glDeleteFramebuffers(1, &heat_fbo); heat_fbo = 0; }
	heat_w = heat_h = 0;
}

/* Draw one frame into the current back buffer and flush it to the GPU
 * without presenting: the commit happens later, in render(). */
static void draw_frame(struct output *o, double when) {
	if (!eglMakeCurrent(egl_display, o->egl_surface, o->egl_surface, egl_context)) {
		fprintf(stderr, "aquarium: eglMakeCurrent failed\n");
		running = 0;
		return;
	}
	if (!gl_ready) init_gl();

	/* We pace ourselves off the frame callback; a blocking swap would stall
	 * the whole event loop and turn a 60fps target into 30. */
	if (!o->swap_interval_set) {
		eglSwapInterval(egl_display, 0);
		o->swap_interval_set = true;
	}

	if (!o->lut_tex || o->lut_w != o->bw) build_floor_lut(o);

	/* Bake this frame's ray strip: the beam profile is one-dimensional in
	 * the angle from the sun, so 2048 texels replace two four-octave fbms
	 * on nearly every pixel. */
	glBindFramebuffer(GL_FRAMEBUFFER, ray_fbo);
	glViewport(0, 0, RAY_LUT_W, 1);
	glUseProgram(ray_program);
	glUniform2f(ray_u_res, (float)RAY_LUT_W, 1.0f);
	glUniform1f(ray_u_time, (float)((when - start_time) * opt_speed));
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, ray_tex);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, o->lut_tex);

	glViewport(0, 0, o->bw, o->bh);
	glUseProgram(program);
	glUniform1i(u_floorlut, 0);
	glUniform1i(u_raylut, 1);
	glUniform2f(u_res, (float)o->bw, (float)o->bh);
	glUniform1f(u_time, (float)((when - start_time) * opt_speed));
	glUniform3fv(u_deep, 1, pal.deep);
	glUniform3fv(u_shallow, 1, pal.shallow);
	glUniform3fv(u_light, 1, pal.light);
	glUniform3fv(u_accent, 1, pal.accent);
	glUniform1f(u_fish, opt_fish);
	glUniform1f(u_weed, opt_weed);
	glUniform1f(u_jelly, opt_jelly);
	glUniform1f(u_anem, opt_anem);
	glUniform1f(u_star, opt_star);
	glUniform1f(u_turtle, opt_turtle);
	glUniform2f(u_sun, sun_x, sun_y);
	glUniform1f(u_day, day_f);

	/* Everything that moves is placed on the CPU, once per frame. */
	{
		struct anim an;
		anim_compute(&an, &seed_data, (when - start_time) * opt_speed,
		             (float)o->bw / (float)o->bh, opt_fish, opt_jelly);
		anim_upload(program, &an);
	}

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFlush();
	o->frame_drawn = true;
}

static void render(struct output *o) {
	double now = now_sec();
	double period = 1.0 / effective_fps();

	if (!o->frame_drawn) draw_frame(o, now);
	if (!running || !o->egl_surface) return;

	struct wl_callback *cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(cb, &frame_listener, o);
	o->inflight++;

	eglMakeCurrent(egl_display, o->egl_surface, o->egl_surface, egl_context);
	eglSwapBuffers(egl_display, o->egl_surface);
	o->frame_drawn = false;
	stat_frames++;

	/* Shader compilation (link plus the driver's draw-time specialisations)
	 * leaves megabytes of freed-but-kept heap behind; hand it back once the
	 * first few frames have flushed all of it out. */
	static bool trimmed;
	if (!trimmed && stat_frames >= 3) {
		malloc_trim(0);
		trimmed = true;
	}

	/* At full rate, start on the next frame immediately so the GPU never
	 * idles between frames: half-idle looks like light load to the DVFS
	 * governor, which then holds the clocks too low to make the deadline —
	 * measured as a hard 30fps ceiling. The pre-rendered frame is committed
	 * by the next frame callback. At lower targets the idle gaps are the
	 * point: they are where the battery savings live. */
	if (at_refresh(o)) {
		draw_frame(o, now + period);
		if (now < gov_preheat_until) {
			/* Ramp phase: oversubscribe outright. */
			heat_pulse(o->bw, o->bh, 3);
		} else {
			/* Holding phase: at full rate the scene plus compositor load
			 * the GPU to ~90%, which the DVFS governor treats as light
			 * enough to sag — and one sagged frame re-locks 30. A ninth
			 * of a frame of extra work per cycle keeps it saturated; it
			 * only runs while full rate is actually being attempted. */
			heat_pulse(o->bw / 3, o->bh / 3, 1);
		}
	} else if (heat_tex) {
		preheat_drop();
	}

	o->next_due += period;
	if (o->next_due < now) o->next_due = now + period;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t t) {
	(void)t;
	struct output *o = data;
	wl_callback_destroy(cb);
	if (o->inflight > 0) o->inflight--;
	presented_frames++;
	/* The main loop decides when this output is next due; keeping the frame
	 * callback in the chain is what makes rendering stop by itself when the
	 * compositor stops painting us, e.g. once the display sleeps. */

	/* At or above the display's own rate the wall clock only beats against
	 * the vblank; let the compositor pace us instead — due again the moment
	 * it reports the last frame painted, one render per refresh, no drift. */
	if (at_refresh(o))
		o->next_due = 0.0;
}

/* Nothing is drawn while suspended, and the whole swapchain is torn down: on
 * a 4K-class output that is ~50 MB of buffers handed back to the system right
 * when a fullscreen app is the thing that wants the memory. The wl_surface
 * survives; unmapping it (null-buffer commit) is what releases the
 * compositor-side buffers too. */
static void suspend_output(struct output *o) {
	if (!o->surface || !o->egl_window) return;
	eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(egl_display, o->egl_surface);
	o->egl_surface = EGL_NO_SURFACE;
	wl_egl_window_destroy(o->egl_window);
	o->egl_window = NULL;
	o->frame_drawn = false;
	wl_surface_attach(o->surface, NULL, 0, 0);
	wl_surface_commit(o->surface);
	o->configured = false;
	o->inflight = 0;
}

/* Re-map: a commit with no buffer attached asks the layer shell for a fresh
 * configure, and ls_configure rebuilds the EGL window and draws. */
static void resume_output(struct output *o) {
	if (!o->surface) return;
	if (o->egl_window) {
		/* Never torn down — suspended before the first draw. Just draw. */
		if (o->configured) o->next_due = now_sec();
		return;
	}
	wl_surface_commit(o->surface);
}

static void set_suspended(bool want) {
	if (want == suspended) return;
	suspended = want;
	for (int i = 0; i < MAX_OUTPUTS; i++) {
		struct output *o = &outputs[i];
		if (!o->used) continue;
		if (suspended) suspend_output(o);
		else resume_output(o);
	}
}

static void update_suspend(void) {
	if (!opt_suspend || hypr_fd < 0) return;
	int fs = hypr_has_fullscreen();
	if (fs < 0) return;                 /* query failed: leave things as they are */
	set_suspended(fs == 1);
}

/* -------------------------------------------------------- layer surface */

static void ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h) {
	struct output *o = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);

	int bscale = opt_buffer_scale > 0 ? opt_buffer_scale : o->scale;
	o->width = (int)w;
	o->height = (int)h;
	o->bw = o->width * bscale;
	o->bh = o->height * bscale;

	o->next_due = now_sec();
	if (o->bw <= 0 || o->bh <= 0) return;

	if (!o->egl_window) {
		o->egl_window = wl_egl_window_create(o->surface, o->bw, o->bh);
		o->egl_surface = eglCreateWindowSurface(
			egl_display, egl_config, (EGLNativeWindowType)o->egl_window, NULL);
		if (o->egl_surface == EGL_NO_SURFACE) {
			fprintf(stderr, "aquarium: eglCreateWindowSurface failed on %s\n", o->name);
			running = 0;
			return;
		}
	} else {
		wl_egl_window_resize(o->egl_window, o->bw, o->bh, 0, 0);
	}

	wl_surface_set_buffer_scale(o->surface, bscale);

	/* Click-through: never take pointer or touch input. */
	struct wl_region *empty = wl_compositor_create_region(compositor);
	wl_surface_set_input_region(o->surface, empty);
	wl_region_destroy(empty);

	/* Fully opaque: lets the compositor skip what is behind us. */
	struct wl_region *full = wl_compositor_create_region(compositor);
	wl_region_add(full, 0, 0, o->width, o->height);
	wl_surface_set_opaque_region(o->surface, full);
	wl_region_destroy(full);

	o->configured = true;
	o->inflight = 0;
	o->frame_drawn = false;   /* any pre-rendered frame is the wrong size now */
	if (!suspended) render(o);
}

static void ls_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)ls;
	struct output *o = data;
	fprintf(stderr, "aquarium: layer surface on %s closed\n", o->name);
	o->configured = false;
	running = 0;
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {
	.configure = ls_configure,
	.closed = ls_closed,
};

static void start_output(struct output *o) {
	if (o->surface) return;
	if (opt_output && strcmp(opt_output, o->name) != 0) return;

	o->surface = wl_compositor_create_surface(compositor);
	o->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		layer_shell, o->surface, o->wl_output, opt_layer, "aquarium");
	zwlr_layer_surface_v1_add_listener(o->layer_surface, &ls_listener, o);
	zwlr_layer_surface_v1_set_anchor(o->layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(o->layer_surface, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(o->layer_surface,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	wl_surface_commit(o->surface);
}

/* --------------------------------------------------------------- output */

static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
                         int32_t pw, int32_t ph, int32_t sub, const char *make,
                         const char *model, int32_t tr) {
	(void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph;
	(void)sub; (void)make; (void)model; (void)tr;
}
static void out_mode(void *data, struct wl_output *wo, uint32_t flags,
                     int32_t w, int32_t h, int32_t refresh) {
	(void)wo; (void)w; (void)h;
	struct output *o = data;
	if (flags & WL_OUTPUT_MODE_CURRENT) o->refresh_mhz = refresh;
}
static void out_scale(void *data, struct wl_output *wo, int32_t factor) {
	(void)wo;
	struct output *o = data;
	o->scale = factor > 0 ? factor : 1;
}
static void out_name(void *data, struct wl_output *wo, const char *name) {
	(void)wo;
	struct output *o = data;
	snprintf(o->name, sizeof(o->name), "%s", name);
}
static void out_description(void *d, struct wl_output *o, const char *desc) {
	(void)d; (void)o; (void)desc;
}
static void out_done(void *data, struct wl_output *wo) {
	(void)wo;
	start_output(data);
}

static const struct wl_output_listener output_listener = {
	.geometry = out_geometry,
	.mode = out_mode,
	.done = out_done,
	.scale = out_scale,
	.name = out_name,
	.description = out_description,
};

/* ------------------------------------------------------------- registry */

static void reg_global(void *data, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t version) {
	(void)data;
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(reg, name, &wl_compositor_interface,
		                              version < 4 ? version : 4);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface,
		                               version < 4 ? version : 4);
	} else if (strcmp(iface, wl_output_interface.name) == 0) {
		for (int i = 0; i < MAX_OUTPUTS; i++) {
			if (outputs[i].used) continue;
			struct output *o = &outputs[i];
			memset(o, 0, sizeof(*o));
			o->used = true;
			o->scale = 1;
			o->global_name = name;
			snprintf(o->name, sizeof(o->name), "output-%u", name);
			uint32_t v = version < 4 ? version : 4;
			o->wl_output = wl_registry_bind(reg, name, &wl_output_interface, v);
			wl_output_add_listener(o->wl_output, &output_listener, o);
			break;
		}
	}
}

static void destroy_output(struct output *o) {
	if (o->egl_surface != EGL_NO_SURFACE && o->egl_surface)
		eglDestroySurface(egl_display, o->egl_surface);
	if (o->egl_window) wl_egl_window_destroy(o->egl_window);
	if (o->layer_surface) zwlr_layer_surface_v1_destroy(o->layer_surface);
	if (o->surface) wl_surface_destroy(o->surface);
	if (o->wl_output) wl_output_release(o->wl_output);
	memset(o, 0, sizeof(*o));
}

static void reg_remove(void *data, struct wl_registry *reg, uint32_t name) {
	(void)data; (void)reg;
	for (int i = 0; i < MAX_OUTPUTS; i++) {
		if (outputs[i].used && outputs[i].global_name == name) {
			destroy_output(&outputs[i]);
			return;
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = reg_global,
	.global_remove = reg_remove,
};

/* ------------------------------------------------------------------ cli */

static void usage(void) {
	puts(
	"omarchy-aquarium — animated underwater desktop background\n"
	"\n"
	"Usage: omarchy-aquarium [options]\n"
	"\n"
	"  --fps N          frames per second (default 60; at the display's own\n"
	"                   rate frames lock to the compositor's vblank)\n"
	"  --speed X        animation speed multiplier (default 1.0)\n"
	"  --fish N         number of fish, 0-16 (default 11)\n"
	"  --weed N         seaweed and kelp blades, 0-20 (default 14)\n"
	"  --jelly N        jellyfish, 0-5 (default 3)\n"
	"  --anemone N      anemones on the boulders, 0-4 (default 2)\n"
	"  --starfish N     starfish on the sand, 0-4 (default 2)\n"
	"  --turtle 0|1     the occasional passing turtle (default 1)\n"
	"  --theme          tint the water from the current Omarchy theme\n"
	"  --output NAME    only draw on this output (e.g. eDP-1)\n"
	"  --buffer-scale N render at scale N instead of the output's (1 quarters\n"
	"                   the pixels on a HiDPI display)\n"
	"  --battery-fps N  fps cap while on battery power (default 10; 0 = off)\n"
	"  --no-solar       keep the tuned afternoon sun instead of tracking the\n"
	"                   real one (location comes from Omarchy's\n"
	"                   dynamic-wallpaper.json)\n"
	"  --sun X,Y        pin the sun anchor (implies --no-solar)\n"
	"  --no-suspend     keep drawing even behind a fullscreen window\n"
	"  --layer L        bottom (default, above wallpaper) or background\n"
	"  --shader FILE    load a fragment shader from FILE instead of the built-in\n"
	"  --stats          log frames-per-second every 5s to stderr\n"
	"  -h, --help       this message");
}

static char *read_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "aquarium: cannot open %s: %s\n", path, strerror(errno));
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc((size_t)n + 1);
	if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fprintf(stderr, "aquarium: cannot read %s\n", path);
		exit(1);
	}
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static void parse_args(int argc, char **argv) {
	bool want_theme = false;
	palette_default(&pal);
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		bool has_next = (i + 1 < argc);
		if ((!strcmp(a, "--fps")) && has_next) {
			opt_fps = atof(argv[++i]);
			if (opt_fps < 1.0) opt_fps = 1.0;
			if (opt_fps > 144.0) opt_fps = 144.0;
		} else if (!strcmp(a, "--speed") && has_next) {
			opt_speed = atof(argv[++i]);
		} else if (!strcmp(a, "--fish") && has_next) {
			opt_fish = (float)atof(argv[++i]);
			if (opt_fish < 0.0f) opt_fish = 0.0f;
			if (opt_fish > 16.0f) opt_fish = 16.0f;
		} else if (!strcmp(a, "--weed") && has_next) {
			opt_weed = (float)atof(argv[++i]);
			if (opt_weed < 0.0f) opt_weed = 0.0f;
			if (opt_weed > 20.0f) opt_weed = 20.0f;
		} else if (!strcmp(a, "--jelly") && has_next) {
			opt_jelly = (float)atof(argv[++i]);
			if (opt_jelly < 0.0f) opt_jelly = 0.0f;
			if (opt_jelly > 5.0f) opt_jelly = 5.0f;
		} else if (!strcmp(a, "--anemone") && has_next) {
			opt_anem = (float)atof(argv[++i]);
			if (opt_anem < 0.0f) opt_anem = 0.0f;
			if (opt_anem > 4.0f) opt_anem = 4.0f;
		} else if (!strcmp(a, "--starfish") && has_next) {
			opt_star = (float)atof(argv[++i]);
			if (opt_star < 0.0f) opt_star = 0.0f;
			if (opt_star > 4.0f) opt_star = 4.0f;
		} else if (!strcmp(a, "--turtle") && has_next) {
			opt_turtle = (float)atof(argv[++i]) > 0.5f ? 1.0f : 0.0f;
		} else if (!strcmp(a, "--battery-fps") && has_next) {
			opt_battery_fps = atof(argv[++i]);
			if (opt_battery_fps < 0.0) opt_battery_fps = 0.0;
		} else if (!strcmp(a, "--no-solar")) {
			opt_solar = false;
		} else if (!strcmp(a, "--sun") && has_next) {
			if (sscanf(argv[++i], "%f,%f", &sun_x, &sun_y) == 2)
				sun_fixed = true;
			else {
				fprintf(stderr, "aquarium: --sun wants X,Y\n");
				exit(1);
			}
		} else if (!strcmp(a, "--no-suspend")) {
			opt_suspend = false;
		} else if (!strcmp(a, "--buffer-scale") && has_next) {
			opt_buffer_scale = atoi(argv[++i]);
			if (opt_buffer_scale < 1) opt_buffer_scale = 1;
			if (opt_buffer_scale > 4) opt_buffer_scale = 4;
		} else if (!strcmp(a, "--stats")) {
			opt_stats = true;
		} else if (!strcmp(a, "--theme")) {
			want_theme = true;
		} else if (!strcmp(a, "--output") && has_next) {
			opt_output = argv[++i];
		} else if (!strcmp(a, "--layer") && has_next) {
			const char *l = argv[++i];
			if (!strcmp(l, "background"))
				opt_layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
			else if (!strcmp(l, "bottom"))
				opt_layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
			else {
				fprintf(stderr, "aquarium: unknown layer '%s'\n", l);
				exit(1);
			}
		} else if (!strcmp(a, "--shader") && has_next) {
			opt_shader_src = read_file(argv[++i]);
		} else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage();
			exit(0);
		} else {
			fprintf(stderr, "aquarium: unknown option '%s'\n", a);
			usage();
			exit(1);
		}
	}
	if (want_theme) palette_from_theme(&pal);
}

/* ----------------------------------------------------------------- main */

int main(int argc, char **argv) {
	parse_args(argc, argv);
	load_location();
	update_solar();
	update_battery();

	struct sigaction sa = {.sa_handler = on_signal};
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "aquarium: cannot connect to a Wayland display\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);   /* globals */
	wl_display_roundtrip(display);   /* output names/scales -> start_output */

	if (!compositor || !layer_shell) {
		fprintf(stderr, "aquarium: compositor lacks wlr-layer-shell-unstable-v1\n");
		return 1;
	}

	egl_display = eglGetDisplay((EGLNativeDisplayType)display);
	if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, NULL, NULL)) {
		fprintf(stderr, "aquarium: eglInitialize failed\n");
		return 1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);

	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
		EGL_NONE,
	};
	EGLint n = 0;
	if (!eglChooseConfig(egl_display, cfg_attr, &egl_config, 1, &n) || n == 0) {
		fprintf(stderr, "aquarium: no suitable EGL config\n");
		return 1;
	}
	static const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, ctx_attr);
	if (egl_context == EGL_NO_CONTEXT) {
		fprintf(stderr, "aquarium: eglCreateContext failed\n");
		return 1;
	}

	start_time = now_sec();

	/* Surfaces were created during the roundtrips; let them configure. */
	wl_display_roundtrip(display);

	int fd = wl_display_get_fd(display);
	double stat_last = now_sec();
	unsigned long stat_last_frames = 0;

	if (opt_suspend) {
		hypr_fd = hypr_events_open();
		if (hypr_fd < 0)
			fprintf(stderr, "aquarium: no Hyprland event socket, "
			                "drawing unconditionally\n");
		else
			update_suspend();
	}

	/* Commit a little before the target time. The compositor needs the buffer
	 * in hand before the vblank it will present on; committing exactly on the
	 * deadline misses it and the frame slips to the one after, which is how a
	 * 30fps target on a 60Hz output silently becomes 20fps. */
	const double lead = 0.004;

	while (running) {
		double now = now_sec();
		double sleep_for = 10.0;
		update_battery();
		update_solar();
		{
			double refresh_hz = 0.0;
			for (int i = 0; i < MAX_OUTPUTS; i++)
				if (outputs[i].used && outputs[i].refresh_mhz > 0) {
					refresh_hz = outputs[i].refresh_mhz / 1000.0;
					break;
				}
			update_governor(refresh_hz);
		}
		if (!suspended) {
			for (int i = 0; i < MAX_OUTPUTS; i++) {
				struct output *o = &outputs[i];
				if (!o->used || !o->configured) continue;
				/* At full rate, allow a second frame in flight: commits
				 * then keep flowing on the wall clock even while the
				 * callback chain is still stuck at the old cadence —
				 * without it, the flip from a sagged 30 up to 60 never
				 * happens (commits per callback, callbacks per commit). */
				if (o->inflight >= (at_refresh(o) ? 2 : 1)) continue;
				double due = o->next_due - lead;
				if (now >= due)
					render(o);
				else if (due - now < sleep_for)
					sleep_for = due - now;
			}
		}

		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);

		struct pollfd pfd[2];
		int nfds = 1;
		pfd[0] = (struct pollfd){.fd = fd, .events = POLLIN};
		if (hypr_fd >= 0) {
			pfd[1] = (struct pollfd){.fd = hypr_fd, .events = POLLIN};
			nfds = 2;
		}

		int timeout_ms = (int)(sleep_for * 1000.0);
		if (opt_stats && timeout_ms > 1000) timeout_ms = 1000;
		if (timeout_ms < 1) timeout_ms = 1;
		int rc = poll(pfd, (nfds_t)nfds, timeout_ms);
		if (rc > 0 && (pfd[0].revents & POLLIN)) {
			wl_display_read_events(display);
		} else {
			wl_display_cancel_read(display);
			if (rc < 0 && errno != EINTR) break;
		}
		wl_display_dispatch_pending(display);

		if (nfds == 2 && (pfd[1].revents & (POLLIN | POLLHUP | POLLERR))) {
			int ev = hypr_events_drain(hypr_fd);
			if (ev < 0) {
				close(hypr_fd);
				hypr_fd = -1;
				set_suspended(false);   /* fail open: keep the scene alive */
			} else if (ev > 0) {
				update_suspend();
			}
		}

		if (opt_stats) {
			double n2 = now_sec();
			if (n2 - stat_last >= 5.0) {
				fprintf(stderr,
				        "aquarium: %.1f fps (%lu shown in %.1fs)%s\n",
				        (double)(presented_frames - stat_last_frames) / (n2 - stat_last),
				        presented_frames - stat_last_frames, n2 - stat_last,
				        suspended ? " [suspended]" : "");
				fflush(stderr);
				stat_last = n2;
				stat_last_frames = presented_frames;
			}
		}
	}

	if (hypr_fd >= 0) close(hypr_fd);
	for (int i = 0; i < MAX_OUTPUTS; i++)
		if (outputs[i].used) destroy_output(&outputs[i]);
	if (egl_context != EGL_NO_CONTEXT) {
		eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(egl_display, egl_context);
	}
	if (egl_display != EGL_NO_DISPLAY) eglTerminate(egl_display);
	wl_display_disconnect(display);
	free(opt_shader_src);
	return 0;
}
