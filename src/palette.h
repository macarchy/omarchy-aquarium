#ifndef AQUARIUM_PALETTE_H
#define AQUARIUM_PALETTE_H

struct palette {
	float deep[3];
	float shallow[3];
	float light[3];
	float accent[3];
};

/* Curated default aquarium colours. */
void palette_default(struct palette *p);

/* Tint `p` from the current Omarchy theme's colors.toml. Leaves `p` alone and
 * returns false if the theme cannot be read. */
int palette_from_theme(struct palette *p);

#endif
