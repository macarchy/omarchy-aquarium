#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "palette.h"

void palette_default(struct palette *p) {
	const float deep[3]    = {0.016f, 0.078f, 0.153f};
	const float shallow[3] = {0.086f, 0.372f, 0.478f};
	const float light[3]   = {0.678f, 0.949f, 1.000f};
	const float accent[3]  = {0.898f, 0.631f, 0.310f};
	memcpy(p->deep, deep, sizeof(deep));
	memcpy(p->shallow, shallow, sizeof(shallow));
	memcpy(p->light, light, sizeof(light));
	memcpy(p->accent, accent, sizeof(accent));
}

static int parse_hex(const char *s, float out[3]) {
	if (*s == '#') s++;
	if (strlen(s) < 6) return 0;
	for (int i = 0; i < 3; i++) {
		char buf[3] = {s[i * 2], s[i * 2 + 1], '\0'};
		char *end;
		long v = strtol(buf, &end, 16);
		if (end != buf + 2) return 0;
		out[i] = (float)v / 255.0f;
	}
	return 1;
}

/* Find a `key = "#rrggbb"` line in a colors.toml. */
static int theme_color(const char *toml, const char *key, float out[3]) {
	const char *p = toml;
	size_t klen = strlen(key);
	while ((p = strstr(p, key)) != NULL) {
		int at_line_start = (p == toml) || p[-1] == '\n';
		const char *after = p + klen;
		while (*after == ' ' || *after == '\t') after++;
		if (at_line_start && *after == '=') {
			const char *q = strchr(after, '"');
			if (q && parse_hex(q + 1, out)) return 1;
		}
		p += klen;
	}
	return 0;
}

static void mix3(float dst[3], const float a[3], const float b[3], float k) {
	for (int i = 0; i < 3; i++) dst[i] = a[i] * (1.0f - k) + b[i] * k;
}

int palette_from_theme(struct palette *p) {
	const char *home = getenv("HOME");
	if (!home) return 0;
	char path[512];
	snprintf(path, sizeof(path),
	         "%s/.local/state/omarchy/current/theme/colors.toml", home);
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "aquarium: no theme colours at %s, using defaults\n", path);
		return 0;
	}
	static char buf[65536];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);

	float blue[3], cyan[3], bg[3], accent[3], fg[3];
	int has_blue = theme_color(buf, "blue", blue);
	int has_cyan = theme_color(buf, "cyan", cyan);
	int has_bg = theme_color(buf, "darker_background", bg) ||
	             theme_color(buf, "background", bg);
	int has_accent = theme_color(buf, "accent", accent);
	int has_fg = theme_color(buf, "bright_foreground", fg) ||
	             theme_color(buf, "foreground", fg);

	/* The theme steers the hue; it does not get to make the ocean orange. */
	if (has_bg && has_blue) {
		mix3(p->deep, bg, blue, 0.30f);
		for (int i = 0; i < 3; i++) p->deep[i] *= 0.85f;
	}
	if (has_cyan && has_blue) {
		mix3(p->shallow, blue, cyan, 0.55f);
		for (int i = 0; i < 3; i++) p->shallow[i] *= 0.72f;
	}
	if (has_fg && has_cyan) mix3(p->light, fg, cyan, 0.45f);
	if (has_accent) memcpy(p->accent, accent, sizeof(p->accent));
	return 1;
}
