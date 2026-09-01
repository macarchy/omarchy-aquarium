// Offscreen renderer: draws one frame of the aquarium shader to a PNG-able
// PPM, without touching the desktop. Used to iterate on the look.
//
//   ./build/aquarium-preview --time 12 --out frame.ppm

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "palette.h"
#include "seeds.h"
#include "anim.h"
#include "shader_frag.h"

/* Linear divisor for the background target: the smooth layers are baked at
 * 1/BG_DIV in each axis, so 1/(BG_DIV*BG_DIV) of the pixels and of the memory.
 * Bilinear magnification puts them back.
 *
 * Swept 2..16 against the golden frames. 8 is the floor of the cost curve
 * (12.13 / 11.59 / 10.15 ms at 2 / 4 / 8, then back up to 10.40 and 11.47 at
 * 12 and 16 as the bake stops dominating and the upsample starts to), and it
 * is still nowhere near the fidelity limit: the worst per-channel delta across
 * day, golden hour and night is 4 of 255, p999 = 2, against a gate of 12.
 * Smoothness is not what runs out here. */
#ifndef BG_DIV
#define BG_DIV 8
#endif

static const char *VERT =
	"attribute vec2 aPos;\n"
	"void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static GLuint compile(GLenum type, const char *src, const char *what) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "preview: %s shader failed:\n%s\n", what, log);
		exit(1);
	}
	return s;
}

static char *read_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); exit(1); }
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = malloc((size_t)n + 1);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "read failed\n"); exit(1); }
	b[n] = '\0';
	fclose(f);
	return b;
}

int main(int argc, char **argv) {
	int W = 1280, H = 800;
	float t = 8.0f, fish = 11.0f;
	float weed = 14.0f, jelly = 3.0f, anem = 2.0f, star = 2.0f, turtle = 1.0f;
	float sun_x = -0.34f, sun_y = 0.68f, day = 1.0f;
	const char *out = "frame.ppm";
	int frames = 0;
	long pace_us = 0;
	int pipe_mode = 0;
	char *shader = NULL;
	struct palette pal;
	palette_default(&pal);

	for (int i = 1; i < argc; i++) {
		int has = i + 1 < argc;
		if (!strcmp(argv[i], "--width") && has) W = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--height") && has) H = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--time") && has) t = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--fish") && has) fish = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--weed") && has) weed = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--jelly") && has) jelly = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--anemone") && has) anem = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--starfish") && has) star = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--turtle") && has) turtle = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--sun") && has) sscanf(argv[++i], "%f,%f", &sun_x, &sun_y);
		else if (!strcmp(argv[i], "--day") && has) day = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--out") && has) out = argv[++i];
		else if (!strcmp(argv[i], "--bench") && has) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--pace") && has) pace_us = atol(argv[++i]);
		else if (!strcmp(argv[i], "--pipe")) pipe_mode = 1;
		else if (!strcmp(argv[i], "--shader") && has) shader = read_file(argv[++i]);
		else if (!strcmp(argv[i], "--theme")) palette_from_theme(&pal);
		else { fprintf(stderr, "preview: bad arg %s\n", argv[i]); return 1; }
	}

	setenv("EGL_PLATFORM", "surfaceless", 1);
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
		fprintf(stderr, "preview: eglInitialize failed\n");
		return 1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);

	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_NONE,
	};
	EGLConfig cfg;
	EGLint n = 0;
	if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n == 0) {
		fprintf(stderr, "preview: no EGL config\n");
		return 1;
	}
	static const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "preview: no surfaceless context (EGL 0x%x)\n", eglGetError());
		return 1;
	}

	GLuint rb, fb;
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, W, H);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "preview: incomplete framebuffer\n");
		return 1;
	}

	GLuint vs = compile(GL_VERTEX_SHADER, VERT, "vertex");
	GLuint fs = compile(GL_FRAGMENT_SHADER, shader ? shader : AQUARIUM_FRAG, "fragment");
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glBindAttribLocation(prog, 0, "aPos");
	glLinkProgram(prog);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetProgramInfoLog(prog, sizeof(log), NULL, log);
		fprintf(stderr, "preview: link failed:\n%s\n", log);
		return 1;
	}

	static const GLfloat tri[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);

	/* Bake the per-column floor LUT with the same shader source. */
	{
		const char *fsrc = shader ? shader : AQUARIUM_FRAG;
		size_t ln = strlen(fsrc) + 64;
		char *lsrc = malloc(ln);
		snprintf(lsrc, ln, "#define FLOOR_LUT_PASS\n%s", fsrc);
		GLuint lprog = glCreateProgram();
		glAttachShader(lprog, compile(GL_VERTEX_SHADER, VERT, "vertex"));
		glAttachShader(lprog, compile(GL_FRAGMENT_SHADER, lsrc, "floor-LUT"));
		free(lsrc);
		glBindAttribLocation(lprog, 0, "aPos");
		glLinkProgram(lprog);
		GLuint lut;
		glGenTextures(1, &lut);
		glBindTexture(GL_TEXTURE_2D, lut);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, 1, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		GLuint lfb;
		glGenFramebuffers(1, &lfb);
		glBindFramebuffer(GL_FRAMEBUFFER, lfb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D, lut, 0);
		glViewport(0, 0, W, 1);
		glUseProgram(lprog);
		glUniform2f(glGetUniformLocation(lprog, "uRes"), (float)W, 1.0f);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, lut);
	}

	/* Ray-strip bake pass, re-run for every frame time. */
	GLuint ray_prog, ray_tex, ray_fb;
	GLint ray_ut;
	{
		const char *fsrc = shader ? shader : AQUARIUM_FRAG;
		size_t ln = strlen(fsrc) + 64;
		char *rsrc = malloc(ln);
		snprintf(rsrc, ln, "#define RAY_LUT_PASS\n%s", fsrc);
		ray_prog = glCreateProgram();
		glAttachShader(ray_prog, compile(GL_VERTEX_SHADER, VERT, "vertex"));
		glAttachShader(ray_prog, compile(GL_FRAGMENT_SHADER, rsrc, "ray-LUT"));
		free(rsrc);
		glBindAttribLocation(ray_prog, 0, "aPos");
		glLinkProgram(ray_prog);
		glGenTextures(1, &ray_tex);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, ray_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2048, 1, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glActiveTexture(GL_TEXTURE0);
		glGenFramebuffers(1, &ray_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, ray_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D, ray_tex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		glUseProgram(ray_prog);
		glUniform2f(glGetUniformLocation(ray_prog, "uRes"), 2048.0f, 1.0f);
		ray_ut = glGetUniformLocation(ray_prog, "uTime");
	}

	/* Half-resolution background pass: the smooth layers, re-baked every
	 * frame because they move with uTime and the sun. Bilinear magnification
	 * is the whole point -- these layers carry nothing sharper than it. */
	GLuint bg_prog, bg_tex, bg_fb;
	GLint bg_ut;
	const int BW = (W + BG_DIV - 1) / BG_DIV, BH = (H + BG_DIV - 1) / BG_DIV;
	{
		const char *fsrc = shader ? shader : AQUARIUM_FRAG;
		size_t ln = strlen(fsrc) + 64;
		char *bsrc = malloc(ln);
		snprintf(bsrc, ln, "#define BG_PASS\n%s", fsrc);
		bg_prog = glCreateProgram();
		glAttachShader(bg_prog, compile(GL_VERTEX_SHADER, VERT, "vertex"));
		glAttachShader(bg_prog, compile(GL_FRAGMENT_SHADER, bsrc, "background"));
		free(bsrc);
		glBindAttribLocation(bg_prog, 0, "aPos");
		glLinkProgram(bg_prog);
		glGenTextures(1, &bg_tex);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, bg_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BW, BH, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glActiveTexture(GL_TEXTURE0);
		glGenFramebuffers(1, &bg_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, bg_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D, bg_tex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		glUseProgram(bg_prog);
		glUniform1i(glGetUniformLocation(bg_prog, "uRayLUT"), 1);
		glUniform2f(glGetUniformLocation(bg_prog, "uRes"), (float)BW, (float)BH);
		glUniform3fv(glGetUniformLocation(bg_prog, "uDeep"), 1, pal.deep);
		glUniform3fv(glGetUniformLocation(bg_prog, "uShallow"), 1, pal.shallow);
		glUniform2f(glGetUniformLocation(bg_prog, "uSun"), sun_x, sun_y);
		bg_ut = glGetUniformLocation(bg_prog, "uTime");
	}

/* The ray strip feeds the background pass, so it has to be baked first. */
#define DRAW_BG(tv) do { \
		glBindFramebuffer(GL_FRAMEBUFFER, bg_fb); \
		glViewport(0, 0, BW, BH); \
		glUseProgram(bg_prog); \
		glUniform1f(bg_ut, (tv)); \
		glDrawArrays(GL_TRIANGLES, 0, 3); \
	} while (0)

#define DRAW_RAY_LUT(tv) do { \
		glBindFramebuffer(GL_FRAMEBUFFER, ray_fb); \
		glViewport(0, 0, 2048, 1); \
		glUseProgram(ray_prog); \
		glUniform1f(ray_ut, (tv)); \
		glDrawArrays(GL_TRIANGLES, 0, 3); \
	} while (0)

	DRAW_RAY_LUT(t);
	DRAW_BG(t);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glViewport(0, 0, W, H);
	glUseProgram(prog);
	glUniform1i(glGetUniformLocation(prog, "uFloorLUT"), 0);
	glUniform1i(glGetUniformLocation(prog, "uRayLUT"), 1);
	glUniform1i(glGetUniformLocation(prog, "uBG"), 2);
	glUniform2f(glGetUniformLocation(prog, "uRes"), (float)W, (float)H);
	glUniform1f(glGetUniformLocation(prog, "uTime"), t);
	glUniform3fv(glGetUniformLocation(prog, "uDeep"), 1, pal.deep);
	glUniform3fv(glGetUniformLocation(prog, "uShallow"), 1, pal.shallow);
	glUniform3fv(glGetUniformLocation(prog, "uLight"), 1, pal.light);
	glUniform3fv(glGetUniformLocation(prog, "uAccent"), 1, pal.accent);
	glUniform1f(glGetUniformLocation(prog, "uFishCount"), fish);
	glUniform1f(glGetUniformLocation(prog, "uWeedCount"), weed);
	glUniform1f(glGetUniformLocation(prog, "uJellyCount"), jelly);
	struct seeds sd;
	struct anim an;
	seeds_compute(&sd);
	seeds_upload(prog, &sd);
	anim_compute(&an, &sd, t, (float)W / (float)H, fish, jelly);
	anim_upload(prog, &an);
	glUniform1f(glGetUniformLocation(prog, "uAnemCount"), anem);
	glUniform1f(glGetUniformLocation(prog, "uStarCount"), star);
	glUniform1f(glGetUniformLocation(prog, "uTurtle"), turtle);
	glUniform2f(glGetUniformLocation(prog, "uSun"), sun_x, sun_y);
	glUniform1f(glGetUniformLocation(prog, "uDay"), day);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFinish();

	if (frames > 0 && pipe_mode) {
		/* Deep pipeline: alternate two render targets so hidden-surface
		 * removal cannot collapse frames, never sync mid-run. Shows the
		 * throughput the GPU reaches when it is kept fully fed. */
		GLuint rb2, fb2;
		glGenRenderbuffers(1, &rb2);
		glBindRenderbuffer(GL_RENDERBUFFER, rb2);
		glRenderbufferStorage(GL_RENDERBUFFER, 0x8058, W, H);
		glGenFramebuffers(1, &fb2);
		glBindFramebuffer(GL_FRAMEBUFFER, fb2);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb2);
		GLint ut2 = glGetUniformLocation(prog, "uTime");
		struct timespec a2, b2;
		clock_gettime(CLOCK_MONOTONIC, &a2);
		for (int i = 0; i < frames; i++) {
			DRAW_RAY_LUT(t + (float)i / 60.0f);
			DRAW_BG(t + (float)i / 60.0f);
			glBindFramebuffer(GL_FRAMEBUFFER, i & 1 ? fb2 : fb);
			glViewport(0, 0, W, H);
			glUseProgram(prog);
			glUniform1f(ut2, t + (float)i / 60.0f);
			anim_compute(&an, &sd, t + (float)i / 60.0f, (float)W / (float)H, fish, jelly);
			anim_upload(prog, &an);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glFlush();
		}
		glFinish();
		clock_gettime(CLOCK_MONOTONIC, &b2);
		double ms2 = ((b2.tv_sec - a2.tv_sec) * 1e3 + (b2.tv_nsec - a2.tv_nsec) / 1e6) / frames;
		fprintf(stderr, "preview: pipelined %.3f ms/frame at %dx%d (%.0f fps ceiling)\n",
		        ms2, W, H, 1000.0 / ms2);
	} else if (frames > 0) {
		GLint ut = glGetUniformLocation(prog, "uTime");
		struct timespec a, b;
		if (pace_us > 0) {
			/* Paced like the real client: one frame, wait out the period.
			 * Shows what the shader costs at the clocks a light duty cycle
			 * actually earns, not at full boost. */
			double worst = 0.0, sum = 0.0;
			for (int i = 0; i < frames; i++) {
				clock_gettime(CLOCK_MONOTONIC, &a);
				DRAW_RAY_LUT(t + (float)i / 60.0f);
				DRAW_BG(t + (float)i / 60.0f);
				glBindFramebuffer(GL_FRAMEBUFFER, fb);
				glViewport(0, 0, W, H);
				glUseProgram(prog);
				glUniform1f(ut, t + (float)i / 60.0f);
				anim_compute(&an, &sd, t + (float)i / 60.0f, (float)W / (float)H, fish, jelly);
				anim_upload(prog, &an);
				glDrawArrays(GL_TRIANGLES, 0, 3);
				glFinish();
				clock_gettime(CLOCK_MONOTONIC, &b);
				double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
				sum += ms;
				if (ms > worst) worst = ms;
				double left = pace_us - ms * 1e3;
				if (left > 0) {
					struct timespec w = {0, (long)(left * 1e3)};
					nanosleep(&w, NULL);
				}
			}
			fprintf(stderr, "preview: paced %.0f fps: %.3f ms/frame avg, %.3f worst at %dx%d\n",
			        1e6 / pace_us, sum / frames, worst, W, H);
		} else {
			clock_gettime(CLOCK_MONOTONIC, &a);
			for (int i = 0; i < frames; i++) {
				DRAW_RAY_LUT(t + (float)i / 30.0f);
				DRAW_BG(t + (float)i / 30.0f);
				glBindFramebuffer(GL_FRAMEBUFFER, fb);
				glViewport(0, 0, W, H);
				glUseProgram(prog);
				glUniform1f(ut, t + (float)i / 30.0f);
				anim_compute(&an, &sd, t + (float)i / 30.0f, (float)W / (float)H, fish, jelly);
				anim_upload(prog, &an);
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
			glFinish();
			clock_gettime(CLOCK_MONOTONIC, &b);
			double ms = ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / frames;
			fprintf(stderr, "preview: %.3f ms/frame at %dx%d (%.0f fps ceiling)\n",
			        ms, W, H, 1000.0 / ms);
		}
	}

	unsigned char *px = malloc((size_t)W * H * 4);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);

	FILE *f = fopen(out, "wb");
	if (!f) { perror(out); return 1; }
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (int y = H - 1; y >= 0; y--) {          /* GL origin is bottom-left */
		for (int x = 0; x < W; x++) {
			unsigned char *p = px + ((size_t)y * W + x) * 4;
			fwrite(p, 1, 3, f);
		}
	}
	fclose(f);
	fprintf(stderr, "preview: wrote %s (%dx%d, t=%.2f)\n", out, W, H, t);
	return 0;
}
