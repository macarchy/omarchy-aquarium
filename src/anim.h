/* Per-frame entity animation, computed once on the CPU.
 *
 * Every fish, bubble and jelly position is a pure function of time and the
 * frozen seeds — evaluating that per pixel made the moving creatures cost
 * milliseconds of setup before their bounding tests even ran, and the
 * driver's once-per-draw preamble could not absorb it all (the uniform
 * register file caps at 512). Computed here instead, the per-pixel loops
 * shrink to a couple of compares per entity. */

#ifndef ANIM_H
#define ANIM_H

#include "seeds.h"

struct anim {
	float fish[SEED_FISH][4];   /* x, y, cos(tilt), sin(tilt) */
	float bub[SEED_BUB][4];     /* x, y, rim fade, 0 */
	float jelly[SEED_JELLY][4]; /* x, y, lifecycle, pulse */
	float jelly_ph[SEED_JELLY]; /* pulse phase, for the tentacle sway */
	float school[4];            /* centre x, y */
	float turtle_a[4];          /* x, y, direction (0 = off screen), flap */
	float turtle_b[4];          /* cos/sin of front and rear flipper sweep */
};

/* t is the shader's uTime (speed already applied); asp the output's aspect. */
void anim_compute(struct anim *a, const struct seeds *s, double t, float asp,
                  float fish_count, float jelly_count);
void anim_upload(unsigned program, const struct anim *a);

#endif
