/* Per-entity seed values for the aquarium scene, computed on the CPU.
 *
 * Every entity's hash has a compile-time-constant argument, and the GLSL
 * compiler used to constant-fold those sin() calls with the host libm while
 * the GPU's own sin disagrees wildly at arguments that large — so whether a
 * call folded depended on the exact loop shape, and touching control flow
 * silently rearranged the scene. The seeds are now computed here, once, with
 * the same libm the compiler folded with, and passed as uniform arrays: the
 * layout can never shift again and the per-pixel sins are gone. Spatial
 * noise (fbm, caustics, snow) keeps runtime hashes — its arguments vary per
 * pixel. */

#ifndef SEEDS_H
#define SEEDS_H

#define SEED_WEED 20   /* x: h.x, y: h.y, z: broad hash */
#define SEED_FISH 16   /* x: h.x, y: h.y, z: lay, w: dir hash; warm separate */
#define SEED_BUB 14    /* x: h.x, y: h.y */
#define SEED_SCHOOL 9  /* x: h.x, y: h.y */
#define SEED_ROCK 5    /* x: h.x, y: h.y, z: height hash, w: anemone len hash */
#define SEED_STAR 4    /* x: h.x, y: h.y, z: rot hash */
#define SEED_JELLY 5   /* x: h.x, y: h.y, z: lay */

struct seeds {
	float weed[SEED_WEED][4];
	float fish[SEED_FISH][4];
	float fish_warm[SEED_FISH];
	float bub[SEED_BUB][4];         /* sorted by x */
	float bub_grp[4][4];            /* column-band lo, hi per quad */
	float school[SEED_SCHOOL][4];
	float rock[SEED_ROCK][4];
	float star[SEED_STAR][4];
	float jelly[SEED_JELLY][4];
};

void seeds_compute(struct seeds *s);

/* Look up the uniform locations and upload every seed array; a no-op for
 * uniforms the shader does not declare (location -1). */
void seeds_upload(unsigned program, const struct seeds *s);

#endif
