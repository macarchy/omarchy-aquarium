#include <math.h>
#include <GLES2/gl2.h>

#include "anim.h"

/* Compiled with -ffp-contract=off: the formulas mirror the shader's own
 * float math, one exactly-rounded operation at a time. The sines here are
 * the host's — for moving entities a sub-pixel phase difference against the
 * GPU's sin is invisible, and the CPU value is authoritative from now on. */

static float fractf(float x) { return x - floorf(x); }
static float mixf(float a, float b, float k) { return a + (b - a) * k; }

static float smoothstepf(float e0, float e1, float x) {
	float k = (x - e0) / (e1 - e0);
	if (k < 0.0f) k = 0.0f;
	if (k > 1.0f) k = 1.0f;
	return k * k * (3.0f - 2.0f * k);
}

static float hash11f(float n) {
	float v = (float)sin((double)(n * 127.1f)) * 43758.5453123f;
	return v - floorf(v);
}

void anim_compute(struct anim *a, const struct seeds *s, double td, float asp,
                  float fish_count, float jelly_count) {
	float t = (float)td;
	float span = asp + 0.9f;

	for (int i = 0; i < SEED_FISH; i++) {
		float fi = (float)i;
		if (fi + 0.5f > fish_count) {
			/* Parked far off screen: the shader's gate never passes and
			 * needs no count test of its own. */
			a->fish[i][0] = 1e9f;
			a->fish[i][1] = 1e9f;
			a->fish[i][2] = 1.0f;
			a->fish[i][3] = 0.0f;
			continue;
		}
		float hx = s->fish[i][0], hy = s->fish[i][1];
		float lay = s->fish[i][2];
		float dir = s->fish[i][3] < 0.5f ? -1.0f : 1.0f;
		float spd = mixf(0.008f, 0.030f, lay);
		float prog = fractf(hx + t * spd);
		float x = mixf(-span * 0.5f, span * 0.5f, dir > 0.0f ? prog : 1.0f - prog);
		float y = (hy - 0.42f) * 0.80f + sinf(t * 0.23f + fi * 1.7f) * 0.030f;
		float tilt = cosf(t * 0.23f + fi * 1.7f) * 0.14f;
		a->fish[i][0] = x;
		a->fish[i][1] = y;
		a->fish[i][2] = cosf(tilt);
		a->fish[i][3] = sinf(tilt);
	}

	for (int i = 0; i < SEED_BUB; i++) {
		float fi = (float)i;
		float hx = s->bub[i][0], hy = s->bub[i][1];
		float sp = 0.030f + hy * 0.060f;
		float y = fractf(hy * 0.9f + t * sp);
		float x = hx + sinf(y * 9.0f + fi) * 0.012f;
		a->bub[i][0] = x;
		a->bub[i][1] = y;
		a->bub[i][2] = smoothstepf(0.0f, 0.12f, y) * smoothstepf(1.0f, 0.86f, y);
		a->bub[i][3] = 0.0f;
	}

	for (int i = 0; i < SEED_JELLY; i++) {
		float fi = (float)i;
		if (fi + 0.5f > jelly_count) {
			a->jelly[i][0] = 1e9f;
			a->jelly[i][1] = 1e9f;
			a->jelly[i][2] = a->jelly[i][3] = 0.0f;
			a->jelly_ph[i] = 0.0f;
			continue;
		}
		float hx = s->jelly[i][0], hy = s->jelly[i][1];
		float lay = s->jelly[i][2];
		float rise = fractf(hy + t * (0.0045f + 0.0045f * lay));
		float ph = t * (0.9f + lay * 0.5f) + fi * 2.4f;
		a->jelly[i][0] = (hx - 0.5f) * span * 0.92f + 0.05f * sinf(t * 0.11f + fi * 2.1f);
		a->jelly[i][1] = mixf(-0.30f, 0.62f, rise);
		a->jelly[i][2] = smoothstepf(0.0f, 0.10f, rise) * smoothstepf(1.0f, 0.90f, rise);
		a->jelly[i][3] = sinf(ph);
		a->jelly_ph[i] = ph;
	}

	a->school[0] = mixf(-span * 0.5f, span * 0.5f, fractf(t * 0.014f));
	a->school[1] = -0.14f + sinf(t * 0.18f) * 0.05f;
	a->school[2] = a->school[3] = 0.0f;

	/* One slow crossing roughly every three and a half minutes. */
	{
		float cyc = t / 210.0f;
		float phT = fractf(cyc);
		float pass = floorf(cyc);
		float travel = phT * 210.0f / 55.0f;
		if (travel < 1.0f) {
			float dirT = hash11f(pass * 7.7f + 3.0f) < 0.5f ? -1.0f : 1.0f;
			float flap = sinf(t * 1.15f + pass);
			float af = 0.55f + 0.45f * flap;
			float ar = 0.35f + 0.30f * sinf(t * 1.15f + pass + 1.3f);
			a->turtle_a[0] = mixf(-span * 0.60f, span * 0.60f,
			                      dirT > 0.0f ? travel : 1.0f - travel);
			a->turtle_a[1] = 0.14f + 0.16f * (hash11f(pass * 3.1f + 5.0f) - 0.5f)
			               + 0.04f * sinf(t * 0.45f);
			a->turtle_a[2] = dirT;
			a->turtle_a[3] = flap;
			a->turtle_b[0] = cosf(af);
			a->turtle_b[1] = sinf(af);
			a->turtle_b[2] = cosf(ar);
			a->turtle_b[3] = sinf(ar);
		} else {
			a->turtle_a[0] = a->turtle_a[1] = a->turtle_a[3] = 0.0f;
			a->turtle_a[2] = 0.0f;   /* direction 0: off screen */
			a->turtle_b[0] = a->turtle_b[1] = a->turtle_b[2] = a->turtle_b[3] = 0.0f;
		}
	}
}

void anim_upload(unsigned program, const struct anim *a) {
	GLint loc;
	loc = glGetUniformLocation(program, "uFishPos");
	if (loc >= 0) glUniform4fv(loc, SEED_FISH, &a->fish[0][0]);
	loc = glGetUniformLocation(program, "uBubPos");
	if (loc >= 0) glUniform4fv(loc, SEED_BUB, &a->bub[0][0]);
	loc = glGetUniformLocation(program, "uJellyPos");
	if (loc >= 0) glUniform4fv(loc, SEED_JELLY, &a->jelly[0][0]);
	loc = glGetUniformLocation(program, "uJellyPh");
	if (loc >= 0) glUniform1fv(loc, SEED_JELLY, a->jelly_ph);
	loc = glGetUniformLocation(program, "uSchoolPos");
	if (loc >= 0) glUniform2fv(loc, 1, a->school);
	loc = glGetUniformLocation(program, "uTurtleA");
	if (loc >= 0) glUniform4fv(loc, 1, a->turtle_a);
	loc = glGetUniformLocation(program, "uTurtleB");
	if (loc >= 0) glUniform4fv(loc, 1, a->turtle_b);
}
