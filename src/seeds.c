#include <string.h>
#include <GLES2/gl2.h>

#include "seeds.h"

/* The scene's entity seeds, frozen as data. Historically each was
 * fract(sin(n * K) * 43758.5453123) evaluated inside the shader; whether a
 * given call ran on the GPU or was constant-folded by the compiler with the
 * host libm depended on the exact loop shape, and the two disagree wildly at
 * arguments that large — so touching control flow silently rearranged the
 * scene. These are the libm values, identical to what tools/gen_shader.py
 * inlines into the embedded build; regenerate both together or not at all.
 * Bubbles are sorted by column so each quad shares one column-band gate
 * (bounds in BUB_GRP). */

static const float WEED[][4] = {
	{0.3203125f, 0.404296875f, 0.283203125f, 0.0f},
	{0.118164062f, 0.4765625f, 0.18359375f, 0.0f},
	{0.91796875f, 0.0483398438f, 0.209960938f, 0.0f},
	{0.947265625f, 0.140625f, 0.00390625f, 0.0f},
	{0.9609375f, 0.7265625f, 0.934814453f, 0.0f},
	{0.037109375f, 0.51953125f, 0.5546875f, 0.0f},
	{0.533203125f, 0.53515625f, 0.591796875f, 0.0f},
	{0.09375f, 0.19921875f, 0.38671875f, 0.0f},
	{0.193359375f, 0.8125f, 0.10546875f, 0.0f},
	{0.4375f, 0.988037109f, 0.88671875f, 0.0f},
	{0.280761719f, 0.12109375f, 0.69140625f, 0.0f},
	{0.24609375f, 0.76171875f, 0.583496094f, 0.0f},
	{0.231933594f, 0.138671875f, 0.75f, 0.0f},
	{0.0f, 0.93359375f, 0.390625f, 0.0f},
	{0.307617188f, 0.0166015625f, 0.05859375f, 0.0f},
	{0.75f, 0.93359375f, 0.2890625f, 0.0f},
	{0.111328125f, 0.120117188f, 0.90625f, 0.0f},
	{0.26953125f, 0.609375f, 0.59765625f, 0.0f},
	{0.87890625f, 0.166015625f, 0.486816406f, 0.0f},
	{0.19140625f, 0.19921875f, 0.34765625f, 0.0f},
};

static const float FISH[][4] = {
	{0.51953125f, 0.627929688f, 0.80078125f, 0.96875f},
	{0.02734375f, 0.01171875f, 0.21484375f, 0.63671875f},
	{0.07421875f, 0.73828125f, 0.171875f, 0.1875f},
	{0.927734375f, 0.25390625f, 0.29296875f, 0.005859375f},
	{0.736328125f, 0.34375f, 0.904296875f, 0.328125f},
	{0.515625f, 0.037109375f, 0.59765625f, 0.50390625f},
	{0.712890625f, 0.7890625f, 0.111328125f, 0.3828125f},
	{0.26171875f, 0.693359375f, 0.734375f, 0.751953125f},
	{0.109375f, 0.68359375f, 0.998046875f, 0.7265625f},
	{0.44140625f, 0.254882812f, 0.41796875f, 0.51953125f},
	{0.23828125f, 0.41796875f, 0.296875f, 0.68359375f},
	{0.1953125f, 0.015625f, 0.775390625f, 0.76171875f},
	{0.755859375f, 0.55859375f, 0.06640625f, 0.259765625f},
	{0.625f, 0.416259766f, 0.998046875f, 0.959960938f},
	{0.469726562f, 0.015625f, 0.98828125f, 0.9609375f},
	{0.872558594f, 0.829101562f, 0.65625f, 0.97265625f},
};

static const float FISH_WARM[] = {
	0.8203125f, 0.00732421875f, 0.1328125f, 0.943359375f, 0.5546875f, 0.1875f, 0.05078125f, 0.7734375f, 0.03515625f, 0.60546875f, 0.436523438f, 0.123046875f, 0.328125f, 0.3203125f, 0.5078125f, 0.7265625f,
};

static const float BUB[][4] = {
	{0.017578125f, 0.00390625f, 0.0f, 0.0f},
	{0.0234375f, 0.173828125f, 0.0f, 0.0f},
	{0.146484375f, 0.431640625f, 0.0f, 0.0f},
	{0.150390625f, 0.0336914062f, 0.0f, 0.0f},
	{0.1875f, 0.966796875f, 0.0f, 0.0f},
	{0.228515625f, 0.19921875f, 0.0f, 0.0f},
	{0.41015625f, 0.80859375f, 0.0f, 0.0f},
	{0.587890625f, 0.3828125f, 0.0f, 0.0f},
	{0.73046875f, 0.2265625f, 0.0f, 0.0f},
	{0.8046875f, 0.262695312f, 0.0f, 0.0f},
	{0.841796875f, 0.767578125f, 0.0f, 0.0f},
	{0.8671875f, 0.384765625f, 0.0f, 0.0f},
	{0.9296875f, 0.990722656f, 0.0f, 0.0f},
	{0.93359375f, 0.33203125f, 0.0f, 0.0f},
};

static const float BUB_GRP[][4] = {
	{-0.008421875f, 0.176390625f, 0.0f, 0.0f},
	{0.1615f, 0.613890625f, 0.0f, 0.0f},
	{0.70446875f, 0.867796875f, 0.0f, 0.0f},
	{0.8411875f, 0.95959375f, 0.0f, 0.0f},
};

static const float SCHOOL[][4] = {
	{0.91796875f, 0.2109375f, 0.0f, 0.0f},
	{0.447265625f, 0.12109375f, 0.0f, 0.0f},
	{0.0390625f, 0.7578125f, 0.0f, 0.0f},
	{0.79296875f, 0.30078125f, 0.0f, 0.0f},
	{0.294921875f, 0.375f, 0.0f, 0.0f},
	{0.640625f, 0.0785522461f, 0.0f, 0.0f},
	{0.609375f, 0.580078125f, 0.0f, 0.0f},
	{0.98046875f, 0.109375f, 0.0f, 0.0f},
	{0.046875f, 0.203125f, 0.0f, 0.0f},
};

static const float ROCK[][4] = {
	{0.859375f, 0.005859375f, 0.938476562f, 0.94921875f},
	{0.583984375f, 0.790039062f, 0.44140625f, 0.76953125f},
	{0.54296875f, 0.640625f, 0.078125f, 0.610839844f},
	{0.159667969f, 0.671875f, 0.28515625f, 0.390625f},
	{0.170898438f, 0.73828125f, 0.528320312f, 0.0f},
};

static const float STAR[][4] = {
	{0.232421875f, 0.118652344f, 0.6640625f, 0.0f},
	{0.42578125f, 0.599609375f, 0.6796875f, 0.0f},
	{0.37109375f, 0.419921875f, 0.32421875f, 0.0f},
	{0.2578125f, 0.2578125f, 0.072265625f, 0.0f},
};

static const float JELLY[][4] = {
	{0.93359375f, 0.25f, 0.8046875f, 0.0f},
	{0.32421875f, 0.9375f, 0.732421875f, 0.0f},
	{0.837890625f, 0.716796875f, 0.80078125f, 0.0f},
	{0.859375f, 0.37890625f, 0.00390625f, 0.0f},
	{0.35546875f, 0.741455078f, 0.78125f, 0.0f},
};

void seeds_compute(struct seeds *s) {
	memcpy(s->weed, WEED, sizeof s->weed);
	memcpy(s->fish, FISH, sizeof s->fish);
	memcpy(s->fish_warm, FISH_WARM, sizeof s->fish_warm);
	memcpy(s->bub, BUB, sizeof s->bub);
	memcpy(s->bub_grp, BUB_GRP, sizeof s->bub_grp);
	memcpy(s->school, SCHOOL, sizeof s->school);
	memcpy(s->rock, ROCK, sizeof s->rock);
	memcpy(s->star, STAR, sizeof s->star);
	memcpy(s->jelly, JELLY, sizeof s->jelly);
}

void seeds_upload(unsigned program, const struct seeds *s) {
	GLint loc;
	loc = glGetUniformLocation(program, "uWeedSeed");
	if (loc >= 0) glUniform4fv(loc, SEED_WEED, &s->weed[0][0]);
	loc = glGetUniformLocation(program, "uFishSeed");
	if (loc >= 0) glUniform4fv(loc, SEED_FISH, &s->fish[0][0]);
	loc = glGetUniformLocation(program, "uFishWarm");
	if (loc >= 0) glUniform1fv(loc, SEED_FISH, s->fish_warm);
	loc = glGetUniformLocation(program, "uBubSeed");
	if (loc >= 0) glUniform4fv(loc, SEED_BUB, &s->bub[0][0]);
	loc = glGetUniformLocation(program, "uBubGrp");
	if (loc >= 0) glUniform4fv(loc, 4, &s->bub_grp[0][0]);
	loc = glGetUniformLocation(program, "uSchoolSeed");
	if (loc >= 0) glUniform4fv(loc, SEED_SCHOOL, &s->school[0][0]);
	loc = glGetUniformLocation(program, "uRockSeed");
	if (loc >= 0) glUniform4fv(loc, SEED_ROCK, &s->rock[0][0]);
	loc = glGetUniformLocation(program, "uStarSeed");
	if (loc >= 0) glUniform4fv(loc, SEED_STAR, &s->star[0][0]);
	loc = glGetUniformLocation(program, "uJellySeed");
	if (loc >= 0) glUniform4fv(loc, SEED_JELLY, &s->jelly[0][0]);
}
