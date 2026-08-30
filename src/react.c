#include <math.h>

#include "react.h"

/* One shared startle clock: the pipe is one pipe, the flinch is one flinch. */
static double startle_at = -1e9;

void react_startle(double now) { startle_at = now; }

static float clampf(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

void react_step(struct react *r, struct anim *a, const struct seeds *s,
                double now) {
	float dt = (float)(now - r->last_now);
	r->last_now = now;
	/* First frame, or resuming after a suspend: no history to integrate. */
	if (dt <= 0.0f || dt > 0.5f) dt = 0.016f;

	float st = (float)(now - startle_at);
	float startle = (st >= 0.0f && st < 3.0f) ? expf(-st * 1.6f) : 0.0f;

	for (int i = 0; i < SEED_FISH; i++) {
		if (a->fish[i][0] > 1e8f) {   /* parked: beyond the fish count */
			r->off[i][0] = r->off[i][1] = 0.0f;
			continue;
		}
		float lay = s->fish[i][2];    /* 0 far .. 1 near */
		float px = a->fish[i][0] + r->off[i][0];
		float py = a->fish[i][1] + r->off[i][1];

		/* Target displacement: away from the cursor, within a radius that
		 * grows with nearness — close fish are big and shy, distant ones
		 * barely notice. */
		float tx = 0.0f, ty = 0.0f;
		if (r->cur_valid) {
			float dx = px - r->cur_x, dy = py - r->cur_y;
			float R = 0.15f + 0.21f * lay;
			float d2 = dx * dx + dy * dy;
			if (d2 < R * R) {
				float d = sqrtf(d2);
				float ux, uy;
				if (d > 1e-4f) {
					ux = dx / d;
					uy = dy / d;
				} else {           /* cursor dead on the fish: pick a side */
					ux = (i & 1) ? 1.0f : -1.0f;
					uy = 0.3f;
					d = 0.0f;
				}
				float push = (R - d) * 1.35f;
				tx = ux * push;
				ty = uy * push;
			}
		}

		/* Startle: a burst along the swimming direction, with a per-fish
		 * vertical scatter so the tank doesn't move as one rigid sheet. */
		if (startle > 0.0f) {
			float dir = s->fish[i][3] < 0.5f ? -1.0f : 1.0f;
			tx += dir * startle * (0.10f + 0.14f * lay);
			ty += (s->fish[i][1] - 0.5f) * startle * 0.12f;
		}

		/* Spring toward the target: dart fast, drift back slowly. */
		float ox = r->off[i][0], oy = r->off[i][1];
		float rate = (tx * tx + ty * ty > ox * ox + oy * oy) ? 7.0f : 1.4f;
		float k = 1.0f - expf(-rate * dt);
		float nx = ox + (tx - ox) * k;
		float ny = oy + (ty - oy) * k;

		/* Stay in the water column: off the sand, under the surface. */
		float fy = a->fish[i][1] + ny;
		if (fy > 0.30f) ny = 0.30f - a->fish[i][1];
		if (fy < -0.38f) ny = -0.38f - a->fish[i][1];

		/* Pitch the nose toward the motion. Coordinate rotation samples the
		 * image rotated the other way, so the delta is -k*vy — and the x-flip
		 * for left-swimmers flips both the nose and the apparent rotation, so
		 * one sign serves both directions. */
		float vy = (ny - oy) / dt;
		float dtilt = clampf(-vy * 0.8f, -0.45f, 0.45f);
		float cd = cosf(dtilt), sd = sinf(dtilt);
		float c0 = a->fish[i][2], s0 = a->fish[i][3];
		a->fish[i][2] = c0 * cd - s0 * sd;
		a->fish[i][3] = s0 * cd + c0 * sd;

		a->fish[i][0] += nx;
		a->fish[i][1] += ny;
		r->off[i][0] = nx;
		r->off[i][1] = ny;
	}

	/* The school moves as one: shoved by the cursor, bolting forward on a
	 * startle (it always crosses left to right). */
	{
		float tx = 0.0f, ty = 0.0f;
		if (r->cur_valid) {
			float dx = (a->school[0] + r->school[0]) - r->cur_x;
			float dy = (a->school[1] + r->school[1]) - r->cur_y;
			float R = 0.30f;
			float d2 = dx * dx + dy * dy;
			if (d2 < R * R && d2 > 1e-8f) {
				float d = sqrtf(d2);
				tx = dx / d * (R - d) * 1.1f;
				ty = dy / d * (R - d) * 1.1f;
			}
		}
		tx += startle * 0.30f;
		float ox = r->school[0], oy = r->school[1];
		float rate = (tx * tx + ty * ty > ox * ox + oy * oy) ? 6.0f : 1.2f;
		float k = 1.0f - expf(-rate * dt);
		r->school[0] = ox + (tx - ox) * k;
		r->school[1] = oy + (ty - oy) * k;
		a->school[0] += r->school[0];
		a->school[1] += clampf(r->school[1], -0.20f, 0.20f);
	}
}
