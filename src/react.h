/* Reactivity: the scene notices the desktop.
 *
 * Two inputs — the cursor (fish keep a polite distance, darting away when
 * it comes close) and a "startle" pulse from the control pipe (a desktop
 * notification makes the whole tank flinch, then settle). Both are pure
 * CPU-side offsets applied to the positions anim.c computed: the shader is
 * untouched, so reactivity costs nothing per pixel.
 *
 * The offsets are stateful (a spring per fish: fast dart, slow return), so
 * each output keeps its own struct react — the cursor is somewhere on one
 * output at a time, and fish on another screen shouldn't feel it. */

#ifndef REACT_H
#define REACT_H

#include <stdbool.h>

#include "anim.h"
#include "seeds.h"

struct react {
	bool cur_valid;              /* cursor is on this output */
	float cur_x, cur_y;          /* cursor in the shader's p-space */
	double last_now;             /* wall clock of the previous step */
	float off[SEED_FISH][2];     /* smoothed scatter offset per fish */
	float school[2];             /* smoothed offset for the school centre */
};

/* Record a startle at wall time `now` (from the control pipe). Global: every
 * output's tank flinches together. */
void react_startle(double now);

/* Displace this frame's animation state: call after anim_compute, before
 * anim_upload. `now` is the wall-clock time the frame targets. */
void react_step(struct react *r, struct anim *a, const struct seeds *s,
                double now);

#endif
