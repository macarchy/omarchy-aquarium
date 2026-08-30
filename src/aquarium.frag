// Underwater aquarium scene, drawn entirely in a fragment shader.
// GLES2 / GLSL ES 1.00: constant loop bounds, no dynamic indexing, no textures.

precision highp float;

uniform vec2  uRes;        // buffer size in pixels
uniform float uTime;       // seconds since start
uniform vec3  uDeep;       // water colour at the floor
uniform vec3  uShallow;    // water colour at the surface
uniform vec3  uLight;      // sunlight tint (rays, caustics, shimmer)
uniform vec3  uAccent;     // warm fish tint
uniform float uFishCount;  // 0..16
uniform float uWeedCount;  // 0..20 seaweed blades
uniform float uJellyCount; // 0..5 jellyfish
uniform float uAnemCount;  // 0..4 anemones
uniform float uStarCount;  // 0..4 starfish
uniform float uTurtle;     // 0/1: the occasional passing turtle
uniform vec2  uSun;        // sun anchor, p-space (x is pre-aspect, scaled here)
uniform float uDay;        // daylight factor: 0 night .. 1 full day

// Per-entity seeds, computed once on the CPU (see seeds.c for why they are
// not hashed here: the compiler folded these sins with the host libm, the
// GPU's sin disagrees at large arguments, and which one you got depended on
// the loop shape — layout roulette). Spatial noise still hashes at runtime.
uniform vec4  uWeedSeed[20];   // h.x, h.y, broad hash
uniform vec4  uFishSeed[16];   // h.x, h.y, lay, dir hash
uniform float uFishWarm[16];
uniform vec4  uBubSeed[14];    // h.x, h.y
uniform vec4  uSchoolSeed[9];  // h.x, h.y
uniform vec4  uRockSeed[5];    // h.x, h.y, height hash, anemone len hash
uniform vec4  uStarSeed[4];    // h.x, h.y, rot hash
uniform vec4  uJellySeed[5];   // h.x, h.y, lay

// Per-frame animation state, computed on the CPU (anim.c): the positions and
// phases of everything that moves. Per pixel, the entity loops keep only
// their bounding gates and the shading.
uniform vec4  uFishPos[16];    // x, y, cos(tilt), sin(tilt)
uniform vec4  uBubPos[14];     // x, y, rim fade
uniform vec4  uBubGrp[4];      // column band (lo, hi) per sorted quad
uniform vec4  uJellyPos[5];    // x, y, lifecycle, pulse
uniform float uJellyPh[5];     // pulse phase, for the tentacle sway
uniform vec2  uSchoolPos;      // school centre
uniform vec4  uTurtleA;        // x, y, direction (0 = off screen), flap
uniform vec4  uTurtleB;        // cos/sin of front and rear flipper sweep

// Substrate hues are fixed rather than derived from the theme: an accent
// colour is free to be anything, and blue sand does not read as sand.
const vec3 SAND = vec3(0.66, 0.56, 0.42);
const vec3 STONE = vec3(0.52, 0.51, 0.49);
const vec3 WARM_FISH = vec3(0.88, 0.62, 0.34);
const vec3 STAR_CORAL = vec3(0.80, 0.38, 0.28);  // dusky coral red
const vec3 ANEM_TIP = vec3(0.78, 0.52, 0.46);    // pale rose tentacle tips

// ---------------------------------------------------------------- noise

float hash11(float n) { return fract(sin(n * 127.1) * 43758.5453123); }

vec2 hash21(float n) {
    return fract(sin(vec2(n * 127.1, n * 311.7)) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash11(i.x + i.y * 57.0);
    float b = hash11(i.x + 1.0 + i.y * 57.0);
    float c = hash11(i.x + (i.y + 1.0) * 57.0);
    float d = hash11(i.x + 1.0 + (i.y + 1.0) * 57.0);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

// fbm whose first octave sits on an integer lattice row: fract(p.y) is 0
// there, the second lattice pair is multiplied by exactly zero, and half the
// first octave's hashes vanish. Bit-identical to fbm(vec2(x, row)).
float fbm_row(float x, float row) {
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float v = 0.5 * mix(hash11(i + row * 57.0), hash11(i + 1.0 + row * 57.0), f);
    vec2 p = vec2(x, row) * 2.03;
    float a = 0.25;
    for (int o = 0; o < 3; o++) {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

// Three octaves plus the missing octave's mean: for fields whose fourth
// octave lands under half an 8-bit step, provably invisible after dither.
float fbm3c(vec2 p) {
    float v = 0.03125;
    float a = 0.5;
    for (int i = 0; i < 3; i++) {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

// ------------------------------------------------------------- caustics

// Voronoi cell edges, animated: the bright net that dances on a pool floor.
// Domain-warped first, so the cells read as water rather than as stained glass.
float caustics(vec2 p, float t) {
    p += 0.55 * vec2(fbm(p * 0.45 + vec2(0.0, t * 0.05)),
                     fbm(p * 0.45 + vec2(5.2, t * 0.04 + 1.3)));
    float best = 1e9;
    float second = 1e9;
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 g = vec2(float(x), float(y));
            vec2 o = hash21((ip.x + g.x) * 1.7 + (ip.y + g.y) * 113.3);
            o = 0.5 + 0.42 * sin(t * 0.9 + 6.2831 * o);
            float d = length(g + o - fp);
            if (d < best) { second = best; best = d; }
            else if (d < second) { second = d; }
        }
    }
    // Bright where two cells meet. Wider and softer than the classic thin
    // net: through frosted glass a hairline filament aliases into noise,
    // a soft band still reads as moving light.
    return pow(clamp(1.0 - (second - best) * 3.8, 0.0, 1.0), 2.4);
}

// ------------------------------------------------------------- god rays

// Shafts radiate from the sun, so the noise is sampled over the angle seen
// from the sun only — constant along each beam. Sampling in both axes gives
// smoke, not light. The sun sits above the surface, so every in-water pixel
// looks up at it and the angle never wraps.
// The beam-strength profile is one-dimensional in the angle seen from the
// sun, so its two fbms are baked per frame into a 2048 x 1 strip (the
// RAY_LUT_PASS below) and sampled here — the modulation fbm stays per pixel,
// it varies along the beam too. The sun sits above the surface, so the angle
// never leaves (-pi/2, pi/2) and the strip maps that range over its width.
uniform sampler2D uRayLUT;

float rays(vec2 p, vec2 sun, float uvy, float t) {
    vec2 d = p - sun;
    float r = length(d);
    float ang = atan(d.x, -d.y);
    float s = texture2D(uRayLUT, vec2(ang * 0.3183098862 + 0.5, 0.5)).r * 1.45;
    // Between the beams s is exactly 0 and the modulation fbm is a dead factor.
    if (s <= 0.0) return 0.0;
    // Beams thin out with distance from the sun and die before the floor.
    // Reaches exactly zero at uvy 0.08 so the caller can skip below there
    // without leaving a step in the gradient.
    float fade = smoothstep(0.08, 0.50, uvy) * exp(-r * 1.05);
    return s * fade * (0.75 + 0.25 * fbm(vec2(ang * 2.4, r * 1.1 - t * 0.04)));
}

// ------------------------------------------------------------- sea floor

float floor_height(float x) {
    return 0.035 + 0.13 * fbm_row(x * 3.6 + 7.0, 3.0)
                 + 0.03 * fbm_row(x * 11.0 + 2.0, 9.0);
}

// Rounded boulders resting on the sand. Returns (mask, lit): lit is how
// squarely the winning boulder's surface faces the sun, up and to the left.
vec2 rocks(vec2 uv, float asp) {
    float m = 0.0;
    float lit = 0.5;
    //#unroll
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        vec2 h = uRockSeed[i].xy;
        float cx = h.x;
        float rw = 0.022 + h.y * 0.032;
        // The wobbled radius never exceeds 1.16, so this far out in x the
        // mask is exactly zero: skip before the height fbm and the atans.
        if (abs(uv.x - cx) >= rw * 1.16) continue;
        float rh = rw * asp * (0.62 + 0.30 * uRockSeed[i].z);
        float cy = floor_height(cx) - rh * 0.45;
        vec2 d = vec2((uv.x - cx) / rw, (uv.y - cy) / rh);
        if (abs(d.y) >= 1.16) continue;
        float wob = 1.0 + 0.10 * sin(atan(d.y, d.x) * 3.0 + fi * 2.0)
                        + 0.06 * sin(atan(d.y, d.x) * 7.0 - fi);
        float a = smoothstep(wob, wob * 0.93, length(d));
        if (a > m) {
            m = a;
            lit = clamp(0.55 + 0.50 * d.y - 0.28 * d.x, 0.0, 1.25);
        }
    }
    return vec2(m, lit);
}

// -------------------------------------------------------------- anemones
//
// Tentacled anemones seated on the boulder crowns — they reuse the boulder
// hashes so each one actually sits on its rock. Returns (mask, glow) where
// glow runs 0 at the base to 1 at the tentacle tips.

vec2 anemones(vec2 uv, float t, float asp, float count) {
    float m = 0.0;
    float glow = 0.0;
    //#unroll
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        if (fi + 0.5 > count) continue;
        vec2 h = uRockSeed[i].xy;
        float cx = h.x;
        float len = 0.055 + 0.025 * uRockSeed[i].w;
        // The x-distance alone already fails the radius test out here; skip
        // before paying for the floor fbm that seats the anemone.
        if (abs(uv.x - cx) * asp > len * 1.6) continue;
        float rw = 0.022 + h.y * 0.032;
        float rh = rw * asp * (0.62 + 0.30 * uRockSeed[i].z);
        vec2 base = vec2(cx, floor_height(cx) - rh * 0.45 + rh * 0.86);
        vec2 rel = vec2((uv.x - base.x) * asp, uv.y - base.y);
        if (dot(rel, rel) > (len * 1.6) * (len * 1.6)) continue;
        for (int j = 0; j < 7; j++) {
            float fj = float(j);
            // Fan of tentacles, each swaying on its own beat.
            float a = (fj - 3.0) * 0.30
                    + 0.16 * sin(t * 0.7 + fi * 2.3 + fj * 1.9);
            float ca = cos(a), sa = sin(a);
            vec2 q = vec2(ca * rel.x - sa * rel.y, sa * rel.x + ca * rel.y);
            float L = len * (0.75 + 0.25 * hash11(fi * 13.1 + fj * 3.7));
            float k = q.y / L;
            if (k < 0.0 || k > 1.0) continue;
            float wob = 0.010 * sin(k * 4.5 + t * 0.9 + fj) * k;
            // Slim stalk swelling into a bulbed tip.
            float w = 0.0042 * (1.0 - k * 0.45)
                    + 0.0038 * smoothstep(0.62, 0.90, k) * smoothstep(1.0, 0.90, k);
            float a2 = smoothstep(w, w * 0.35, abs(q.x - wob));
            if (a2 > m) { m = a2; glow = k; }
        }
    }
    return vec2(m, glow);
}

// -------------------------------------------------------------- starfish
//
// Still accents resting on the sand face. Returns (mask, arm ridge).

vec2 starfish(vec2 uv, float asp, float count) {
    float m = 0.0;
    float ridge = 0.5;
    //#unroll
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        if (fi + 0.5 > count) continue;
        vec2 h = uStarSeed[i].xy;
        float cx = 0.08 + 0.84 * h.x;
        float R = 0.019 + 0.011 * h.y;
        // The star reaches at most rr + 0.10 = 1.10 units from its centre, so
        // beyond 1.15 in x alone the mask is zero: skip before the floor fbm.
        if (abs(uv.x - cx) * asp >= R * 1.15) continue;
        float cy = floor_height(cx) - 0.030 - h.y * 0.025;
        vec2 q = vec2((uv.x - cx) * asp, uv.y - cy) / R;
        if (dot(q, q) > 1.7) continue;
        float th = atan(q.y, q.x);
        float rot = uStarSeed[i].z * 6.2831;
        float rr = 0.66 + 0.34 * cos(th * 5.0 + rot);
        float a = smoothstep(0.10, -0.10, length(q) - rr);
        if (a > m) {
            m = a;
            // Bright along each arm's spine, darker in the notches.
            ridge = 0.55 + 0.45 * cos(th * 5.0 + rot);
        }
    }
    return vec2(m, ridge);
}

// --------------------------------------------------------------- seaweed

// Groves, not a lawn: every blade belongs to one of three clumps, rooted
// where the boulders sit, and a few blades in each clump are broad kelp
// ribbons with lobed edges rather than grass.
float seaweed(vec2 uv, float t, float asp, float count) {
    float m = 0.0;
    float wbound = 0.056 + 0.078 / asp;
    // Blades belong to one of three groves and can never reach further than
    // the grove's jitter plus a blade's own reach; a pixel outside the band
    // skips that grove's blades wholesale. Blade indices stay fi = 3k + g,
    // so each blade keeps the exact seed and shape it has always had.
    if (abs(uv.x - 0.15) < 0.08 + wbound) {
        //#unroll
        for (int i = 0; i < 7; i++) {
            float fi = float(i) * 3.0 + 0.0;
            if (fi + 0.5 > count) continue;
            vec2 h = uWeedSeed[i * 3 + 0].xy;
            float base = 0.15 + (h.x - 0.5) * 0.16;
            if (abs(uv.x - base) >= wbound) continue;
            // Rooted well below the local crest: the dune wanders under a
            // swaying blade, and a root at the crest leaves the blade hanging
            // in open water on a hard cut wherever the sand dips.
            float root = floor_height(base) - 0.07;
            float broad = step(0.72, uWeedSeed[i * 3 + 0].z);
            float hgt = 0.11 + h.y * 0.34 + broad * 0.10;
            float top = root + hgt;
            if (uv.y > root && uv.y < top) {
                float k = (uv.y - root) / hgt;
                float sway = (sin(uv.y * 6.0 + t * 0.55 + fi * 2.1) * 0.026 +
                              sin(uv.y * 13.0 - t * 0.31 + fi) * 0.008) * k * k
                           * (1.0 + broad * 0.6);
                float w = (0.017 * (1.0 - k) * (1.0 - k * 0.25) + 0.0009) / asp;
                // Kelp: narrow stipe at the holdfast, widening into a ribbon
                // whose edges swell and pinch as it catches the water.
                w *= 1.0 + broad * (2.2 + 1.1 * sin(uv.y * 42.0 + fi * 2.7 + t * 0.2))
                         * smoothstep(0.0, 0.35, k);
                float d = abs(uv.x - (base + sway));
                // The soft edge is a fraction of the width, so a broad ribbon
                // needs a tighter fraction or it blurs into smoke.
                float inner = mix(0.35, 0.72, broad);
                // Emerge from the sand rather than ending on a flat cut.
                m = max(m, smoothstep(w, w * inner, d) * (0.75 + 0.25 * k)
                           * smoothstep(0.0, 0.12, k));
            }
        }
    }
    if (abs(uv.x - 0.54) < 0.08 + wbound) {
        //#unroll
        for (int i = 0; i < 7; i++) {
            float fi = float(i) * 3.0 + 1.0;
            if (fi + 0.5 > count) continue;
            vec2 h = uWeedSeed[i * 3 + 1].xy;
            float base = 0.54 + (h.x - 0.5) * 0.16;
            if (abs(uv.x - base) >= wbound) continue;
            // Rooted well below the local crest: the dune wanders under a
            // swaying blade, and a root at the crest leaves the blade hanging
            // in open water on a hard cut wherever the sand dips.
            float root = floor_height(base) - 0.07;
            float broad = step(0.72, uWeedSeed[i * 3 + 1].z);
            float hgt = 0.11 + h.y * 0.34 + broad * 0.10;
            float top = root + hgt;
            if (uv.y > root && uv.y < top) {
                float k = (uv.y - root) / hgt;
                float sway = (sin(uv.y * 6.0 + t * 0.55 + fi * 2.1) * 0.026 +
                              sin(uv.y * 13.0 - t * 0.31 + fi) * 0.008) * k * k
                           * (1.0 + broad * 0.6);
                float w = (0.017 * (1.0 - k) * (1.0 - k * 0.25) + 0.0009) / asp;
                // Kelp: narrow stipe at the holdfast, widening into a ribbon
                // whose edges swell and pinch as it catches the water.
                w *= 1.0 + broad * (2.2 + 1.1 * sin(uv.y * 42.0 + fi * 2.7 + t * 0.2))
                         * smoothstep(0.0, 0.35, k);
                float d = abs(uv.x - (base + sway));
                // The soft edge is a fraction of the width, so a broad ribbon
                // needs a tighter fraction or it blurs into smoke.
                float inner = mix(0.35, 0.72, broad);
                // Emerge from the sand rather than ending on a flat cut.
                m = max(m, smoothstep(w, w * inner, d) * (0.75 + 0.25 * k)
                           * smoothstep(0.0, 0.12, k));
            }
        }
    }
    if (abs(uv.x - 0.88) < 0.08 + wbound) {
        //#unroll
        for (int i = 0; i < 6; i++) {
            float fi = float(i) * 3.0 + 2.0;
            if (fi + 0.5 > count) continue;
            vec2 h = uWeedSeed[i * 3 + 2].xy;
            float base = 0.88 + (h.x - 0.5) * 0.16;
            if (abs(uv.x - base) >= wbound) continue;
            // Rooted well below the local crest: the dune wanders under a
            // swaying blade, and a root at the crest leaves the blade hanging
            // in open water on a hard cut wherever the sand dips.
            float root = floor_height(base) - 0.07;
            float broad = step(0.72, uWeedSeed[i * 3 + 2].z);
            float hgt = 0.11 + h.y * 0.34 + broad * 0.10;
            float top = root + hgt;
            if (uv.y > root && uv.y < top) {
                float k = (uv.y - root) / hgt;
                float sway = (sin(uv.y * 6.0 + t * 0.55 + fi * 2.1) * 0.026 +
                              sin(uv.y * 13.0 - t * 0.31 + fi) * 0.008) * k * k
                           * (1.0 + broad * 0.6);
                float w = (0.017 * (1.0 - k) * (1.0 - k * 0.25) + 0.0009) / asp;
                // Kelp: narrow stipe at the holdfast, widening into a ribbon
                // whose edges swell and pinch as it catches the water.
                w *= 1.0 + broad * (2.2 + 1.1 * sin(uv.y * 42.0 + fi * 2.7 + t * 0.2))
                         * smoothstep(0.0, 0.35, k);
                float d = abs(uv.x - (base + sway));
                // The soft edge is a fraction of the width, so a broad ribbon
                // needs a tighter fraction or it blurs into smoke.
                float inner = mix(0.35, 0.72, broad);
                // Emerge from the sand rather than ending on a flat cut.
                m = max(m, smoothstep(w, w * inner, d) * (0.75 + 0.25 * k)
                           * smoothstep(0.0, 0.12, k));
            }
        }
    }
    return m;
}

// --------------------------------------------------------------- bubbles

float bubbles(vec2 uv, float t, float asp) {
    float b = 0.0;
    // Seeds are sorted by column; each quad of neighbours shares one
    // column-band gate, so most pixels test four bands and stop.
    if (uv.x >= uBubGrp[0].x && uv.x <= uBubGrp[0].y) {
        //#unroll
        for (int i = 0; i < 4; i++) {
            float rad = 0.0024 + uBubSeed[i].y * 0.0042;
            vec2 d = uv - uBubPos[i].xy;
            d.x *= asp;
            // Both the ring and the highlight live within one radius in x.
            if (abs(d.x) >= rad) continue;
            float dd = length(d);
            float ring = smoothstep(rad, rad * 0.45, dd) * smoothstep(rad * 0.40, rad * 0.88, dd);
            float hi = smoothstep(rad * 0.5, 0.0, length(d - vec2(-rad * 0.3, rad * 0.3)));
            b += (ring + hi * 0.8) * uBubPos[i].z;
        }
    }
    if (uv.x >= uBubGrp[1].x && uv.x <= uBubGrp[1].y) {
        //#unroll
        for (int i = 4; i < 8; i++) {
            float rad = 0.0024 + uBubSeed[i].y * 0.0042;
            vec2 d = uv - uBubPos[i].xy;
            d.x *= asp;
            // Both the ring and the highlight live within one radius in x.
            if (abs(d.x) >= rad) continue;
            float dd = length(d);
            float ring = smoothstep(rad, rad * 0.45, dd) * smoothstep(rad * 0.40, rad * 0.88, dd);
            float hi = smoothstep(rad * 0.5, 0.0, length(d - vec2(-rad * 0.3, rad * 0.3)));
            b += (ring + hi * 0.8) * uBubPos[i].z;
        }
    }
    if (uv.x >= uBubGrp[2].x && uv.x <= uBubGrp[2].y) {
        //#unroll
        for (int i = 8; i < 11; i++) {
            float rad = 0.0024 + uBubSeed[i].y * 0.0042;
            vec2 d = uv - uBubPos[i].xy;
            d.x *= asp;
            // Both the ring and the highlight live within one radius in x.
            if (abs(d.x) >= rad) continue;
            float dd = length(d);
            float ring = smoothstep(rad, rad * 0.45, dd) * smoothstep(rad * 0.40, rad * 0.88, dd);
            float hi = smoothstep(rad * 0.5, 0.0, length(d - vec2(-rad * 0.3, rad * 0.3)));
            b += (ring + hi * 0.8) * uBubPos[i].z;
        }
    }
    if (uv.x >= uBubGrp[3].x && uv.x <= uBubGrp[3].y) {
        //#unroll
        for (int i = 11; i < 14; i++) {
            float rad = 0.0024 + uBubSeed[i].y * 0.0042;
            vec2 d = uv - uBubPos[i].xy;
            d.x *= asp;
            // Both the ring and the highlight live within one radius in x.
            if (abs(d.x) >= rad) continue;
            float dd = length(d);
            float ring = smoothstep(rad, rad * 0.45, dd) * smoothstep(rad * 0.40, rad * 0.88, dd);
            float hi = smoothstep(rad * 0.5, 0.0, length(d - vec2(-rad * 0.3, rad * 0.3)));
            b += (ring + hi * 0.8) * uBubPos[i].z;
        }
    }
    return b;
}

// ----------------------------------------------------------- marine snow

float snow(vec2 uv, float t, float asp) {
    vec2 g = vec2(uv.x * asp, uv.y) * 15.0;
    g.y += t * 0.05;
    g.x += sin(t * 0.1 + g.y * 0.5) * 0.1;
    vec2 id = floor(g);
    vec2 f = fract(g);
    vec2 o = hash21(id.x + id.y * 57.0);
    float d = length(f - o);
    return smoothstep(0.07, 0.0, d) * step(0.76, hash11(id.x * 3.1 + id.y * 7.7));
}

// ------------------------------------------------------------------ fish
//
// Local space: fish faces +x, nose near x = 0.55, tail tip near x = -1.0.
// Returns (body distance, tail/fin distance) so the two can be shaded apart.

vec2 fish_sdf(vec2 p, float wag) {
    // Whole body undulates, tail end swinging most.
    float bend = smoothstep(0.55, -1.0, p.x);
    p.y += sin(p.x * 2.6 - wag) * 0.13 * bend;

    // Body: a teardrop — blunt nose, tapering tail stock.
    float taper = 1.0 - 0.45 * smoothstep(0.0, -0.75, p.x);
    float body = length(vec2(p.x / 0.52, p.y / (0.19 * taper))) - 1.0;

    // Caudal fin: wedge with a forked rear edge.
    vec2 q = vec2(-p.x - 0.62, abs(p.y));
    float tail = max(max(-0.42 - q.x * -1.0, q.x - 0.40),
                     q.y - (0.03 + q.x * 0.62));
    tail = max(tail, 0.30 - length(vec2(p.x + 1.06, p.y * 0.85)));

    // Dorsal fin.
    vec2 dp = p - vec2(-0.02, 0.16);
    float dorsal = max(max(abs(dp.x) - 0.20, -dp.y),
                       dp.y - 0.13 + abs(dp.x) * 0.55);

    // Pectoral fin.
    vec2 pp = p - vec2(0.14, -0.10);
    float pect = length(vec2(pp.x / 0.15, pp.y / 0.07)) - 1.0;

    float fins = min(min(tail, dorsal), pect);
    return vec2(body, fins);
}

// ------------------------------------------------------------- floor LUT
//
// floor_height depends on nothing but uv.x, so it is baked once per output
// into a width x 1 texture — one texel per pixel column, computed by this
// same shader under FLOOR_LUT_PASS, so the values are the very ones the
// per-pixel evaluation produced. 16-bit fixed point per channel pair keeps
// the dune line within 4e-6 of the analytic value.

uniform sampler2D uFloorLUT;

const float LUT_DEC = 0.25 / 65535.0;

#ifdef RAY_LUT_PASS

void main() {
    float ang = (gl_FragCoord.x / uRes.x - 0.5) * 3.14159265;
    float a = fbm_row(ang * 4.6 + uTime * 0.026, 11.0);
    float b = fbm_row(ang * 11.0 - uTime * 0.019, 41.0);
    float s = smoothstep(0.46, 0.95, a) + 0.45 * smoothstep(0.55, 0.95, b);
    gl_FragColor = vec4(s / 1.45, 0.0, 0.0, 1.0);
}

#elif defined(FLOOR_LUT_PASS)

vec2 lut_enc(float v01) {
    float q = floor(clamp(v01, 0.0, 1.0) * 65535.0 + 0.5);
    float hi = floor(q / 256.0);
    return vec2(hi, q - hi * 256.0) / 255.0;
}

void main() {
    float x = gl_FragCoord.x / uRes.x;
    float fh = floor_height(x);
    float slope = floor_height(x + 0.01) - floor_height(x - 0.01);
    gl_FragColor = vec4(lut_enc(fh * 4.0), lut_enc((slope + 0.125) * 4.0));
}

#else

// ------------------------------------------------------------------ main

void main() {
    vec2 fc = gl_FragCoord.xy;
    vec2 uv = fc / uRes;                       // 0..1, y up (1 = surface)
    float asp = uRes.x / uRes.y;
    vec2 p = vec2((uv.x - 0.5) * asp, uv.y - 0.5);
    float t = uTime;
    float depth = 1.0 - uv.y;                  // 0 surface .. 1 floor

    // The sun: just above the surface. Everything bright in the scene — rays,
    // glow, caustic weighting — hangs off this point. Its position tracks the
    // real sun over the day (east left, west right, height from elevation);
    // at night the same anchor carries the moon.
    vec2 sun = vec2(uSun.x * asp, uSun.y);

    // ---- water column ------------------------------------------------
    float grad = smoothstep(0.0, 1.0, depth * depth * 0.6 + depth * 0.4);
    vec3 col = mix(uShallow, uDeep, grad);
    col *= 1.0 + 0.05 * (fbm3c(vec2(uv.x * 2.2, uv.y * 2.2 - t * 0.02)) - 0.5);
    // Big, slow masses of water. Almost too subtle to name, but without them
    // the column is a flat fill and the scene has no middle distance.
    float body = fbm(vec2(p.x * 0.55 - t * 0.006, uv.y * 1.15 + t * 0.004));
    col *= 0.91 + 0.19 * body;
    // Water brightens towards the sun and falls into shadow away from it.
    float sunAmt = exp(-length((p - sun) * vec2(0.55, 0.75)) * 1.1);
    col = mix(col, uShallow * 1.05, sunAmt * 0.30 * (1.0 - grad * 0.5));
    col *= 1.0 - 0.11 * smoothstep(0.5, 2.3, length(p - sun));

    // The caustic net only ever contributes near the surface, so the voronoi
    // is skipped below it. The weight ramps in, so there is no seam where the
    // branch flips.
    float causW = smoothstep(0.34, 0.56, uv.y);
    // The net belongs to the sun: full strength under it, embers away from it.
    float sunNear = 0.30 + 0.70 * exp(-abs(p.x - sun.x) * 0.55);
    // Of everything the mid-water net feeds, only the surface terms survive
    // 8-bit quantisation: the pow(uv.y, 11) film is under half an LSB below
    // uv.y 0.75. Fish do read the net lower down — they compute it themselves
    // inside their own disc, where it is nearly free.
    float caus = 0.0;
    if (uv.y > 0.75) caus = caustics(vec2(uv.x * asp, uv.y) * 7.0, t) * causW * sunNear;

    // ---- sea floor ---------------------------------------------------
    // The dunes top out at fh 0.195, and every floor mask below reaches at
    // most 0.20 above them; past uv.y 0.40 the whole block is a no-op, so the
    // two-fbm height field is not worth evaluating.
    float fh = 0.0;
    float nearFloor = 0.0;
    vec2 rock = vec2(0.0, 0.5);
    if (uv.y < 0.40) {
    vec4 flut = texture2D(uFloorLUT, vec2(uv.x, 0.5));
    fh = dot(flut.rg, vec2(65280.0, 255.0)) * LUT_DEC;
    nearFloor = step(uv.y, fh + 0.20);
    float sandMask = smoothstep(fh + 0.012, fh - 0.012, uv.y);
    if (sandMask > 0.001) {
        // Sand lit through the water: its own hue, dimmed and tinted by depth.
        vec3 sand = SAND * (0.55 + 0.45 * uShallow) + uDeep * 0.55;
        sand = mix(sand, sand * mix(vec3(1.0), uAccent + 0.5, 0.35), 0.25);
        float grain = fbm(vec2(uv.x * 90.0, uv.y * 130.0));
        sand *= 0.85 + 0.30 * grain;
        // Dune faces catch the light unevenly.
        float slope = dot(flut.ba, vec2(65280.0, 255.0)) * LUT_DEC - 0.125;
        sand *= 1.0 + clamp(slope * 6.0, -0.35, 0.35);
        // Caustic net dancing on the sand, foreshortened by the viewing angle.
        float floorCaus = caustics(vec2(uv.x * asp, uv.y * 0.35) * 9.0, t * 0.8);
        sand += uLight * floorCaus * 0.10 * (0.4 + 0.6 * sunNear);
        // Shade the sand away from the ridge line.
        sand *= mix(0.55, 1.0, smoothstep(fh - 0.09, fh, uv.y));
        col = mix(col, sand, sandMask);
    }
    // Boulders.
    if (nearFloor > 0.5) rock = rocks(uv, asp);
    if (rock.x > 0.0) {
        // Wet grey stone: close to the sand in value, desaturated so it reads
        // as rock and not as painted plastic.
        vec3 rockCol = STONE * (0.45 + 0.55 * uShallow) + uDeep * 0.60;
        rockCol *= 0.92 * (0.82 + 0.36 * fbm(vec2(uv.x * 55.0, uv.y * 55.0)));
        // Modelled by the sun: the face towards it is bright, the far side
        // and the seat in the sand fall into shadow.
        rockCol *= mix(0.55, 1.22, rock.y);
        rockCol *= mix(0.75, 1.10, smoothstep(fh - 0.05, fh + 0.07, uv.y));
        rockCol += uLight * 0.04 * caus * smoothstep(fh - 0.02, fh + 0.06, uv.y);
        col = mix(col, rockCol, rock.x);
    }

    // Suspended silt softens the floor line — but not over the boulders,
    // or they detach from the sand and float.
    col = mix(col, uDeep * 1.1,
              smoothstep(fh + 0.16, fh - 0.02, uv.y) * 0.32 * (1.0 - rock.x * 0.75));
    }

    // ---- starfish, resting on the sand -------------------------------
    if (nearFloor > 0.5 && uStarCount > 0.5) {
        vec2 star = starfish(uv, asp, uStarCount);
        if (star.x > 0.0) {
            // A fixed coral hue, not the theme accent: an accent is free to
            // be any colour, and a blue starfish reads as a sticker. Dimmed
            // hard towards the deep — a red thing seen through metres of blue.
            vec3 starCol = STAR_CORAL * 0.50 + uDeep * 0.50;
            starCol *= 0.80 + 0.35 * star.y;
            starCol *= 0.9 + 0.3 * fbm(vec2(uv.x * 140.0, uv.y * 140.0));
            col = mix(col, starCol, star.x * 0.9);
        }
    }

    // ---- anemones, on the boulder crowns ------------------------------
    if (nearFloor > 0.5 && uAnemCount > 0.5) {
        vec2 anem = anemones(uv, t, asp, uAnemCount);
        // Sea-green body warming to pale rose at the tips — natural hues,
        // tinted by the water, never the raw theme accent.
        vec3 anemCol = uShallow * vec3(0.45, 0.72, 0.58) + uDeep * 0.40;
        anemCol = mix(anemCol, ANEM_TIP * 0.55 + uDeep * 0.40,
                      smoothstep(0.45, 1.0, anem.y));
        anemCol *= 0.65 + 0.60 * anem.y;
        anemCol += uLight * 0.06 * anem.y * sunNear;
        col = mix(col, anemCol, anem.x * 0.85);
    }

    // ---- seaweed -----------------------------------------------------
    float weed = 0.0;
    if (uv.y < 0.66 && uWeedCount > 0.5) weed = seaweed(uv, t, asp, uWeedCount);
    if (weed > 0.0) {
        // Push the water hue towards green rather than inventing a colour.
        vec3 weedCol = uShallow * vec3(0.30, 0.62, 0.42) + uDeep * 0.55;
        weedCol *= mix(0.55, 1.15, smoothstep(0.04, 0.34, uv.y));
        weedCol += uLight * 0.035 * smoothstep(0.12, 0.36, uv.y);
        col = mix(col, weedCol, weed * 0.9);
    }

    // ---- shafts of light --------------------------------------------
    // Below this the shaft falloff has already taken the contribution to
    // roughly a thousandth; three fbm octaves are not worth it.
    if (uv.y > 0.08) col += uLight * rays(p, sun, uv.y, t) * 0.22;

    // ---- the sun well -----------------------------------------------
    // A soft bloom where the sun sits behind the surface: a tight core and
    // a wide halo, both squashed vertically so it reads as light spreading
    // along the surface rather than a bulb in the water.
    vec2 sd = p - sun;
    sd.y *= 1.5;
    float core = exp(-dot(sd, sd) * 4.5);
    float halo = exp(-length(sd) * 2.1);
    col += uLight * (core * 0.34 + halo * 0.12);

    // ---- caustic net, only just under the surface --------------------
    col += uLight * caus * 0.035 * pow(uv.y, 11.0);

    // ---- the underside of the surface itself -------------------------
    // Brightest under the sun, dimming away from it.
    float surfSun = 0.45 + 0.55 * exp(-abs(p.x - sun.x) * 0.75);
    float ripple = sin(uv.x * 34.0 + t * 0.9) * 0.004 +
                   sin(uv.x * 71.0 - t * 0.6) * 0.002;
    float surf = smoothstep(0.955, 0.995, uv.y + ripple);
    col += uLight * surf * (0.11 + 0.32 * caus) * surfSun;
    col += uLight * smoothstep(0.86, 1.0, uv.y) * 0.055 * surfSun;

    // ---- a turtle, passing through rarely ------------------------------
    // One slow crossing roughly every three and a half minutes, then gone.
    // It stays a soft distant silhouette: an event, not a fixture.
    if (uTurtle > 0.5 && uTurtleA.z != 0.0) {
        {
            vec2 rel = p - uTurtleA.xy;
            float scT = 0.15;
            if (dot(rel, rel) < (1.7 * scT) * (1.7 * scT)) {
                vec2 q = rel / scT;
                q.x *= uTurtleA.z;
                float flap = uTurtleA.w;
                q.y += 0.05 * flap;                  // whole body eases up and down
                // Shell, head, tail: one rounded silhouette.
                float d = length(vec2(q.x / 0.68, (q.y - 0.06) / 0.40)) - 1.0;
                d = min(d, length(vec2((q.x - 0.72) / 0.22, (q.y - 0.10) / 0.13)) - 1.0);
                d = min(d, length(vec2((q.x + 0.74) / 0.16, q.y / 0.09)) - 1.0);
                // Front and rear flippers, sweeping on the same slow beat.
                vec2 qf = q - vec2(0.18, -0.16);
                qf = mat2(uTurtleB.x, -uTurtleB.y, uTurtleB.y, uTurtleB.x) * qf;
                d = min(d, length(vec2(qf.x / 0.16, qf.y / 0.52)) - 1.0);
                vec2 qr = q - vec2(-0.52, -0.12);
                qr = mat2(uTurtleB.z, -uTurtleB.w, uTurtleB.w, uTurtleB.z) * qr;
                d = min(d, length(vec2(qr.x / 0.10, qr.y / 0.30)) - 1.0);
                float aT = smoothstep(0.12, -0.06, d);
                // Far away: barely darker than the water, washed towards it.
                vec3 tCol = mix(col * 0.82, uDeep * 1.3 + uShallow * 0.12, 0.45);
                col = mix(col, tCol, aT * 0.68);
            }
        }
    }

    // ---- fish ---------------------------------------------------------
    // Below the caustic cutoff the net is recomputed lazily, at most once,
    // only for pixels actually inside some fish's disc.
    float fishCaus = uv.y > 0.75 ? caus : -1.0;
    //#unroll
    for (int i = 0; i < 16; i++) {
        float fi = float(i);
        float lay = uFishSeed[i].z;               // 0 far .. 1 near
        float sc = mix(0.038, 0.115, lay * lay);

        // A fish covers a disc of about 1.5 local units including the soft
        // edge. Nearly every pixel is outside every fish, and the test is
        // spatially coherent, so these two gates are most of what the loop
        // costs. Positions come from the CPU, which parks inactive fish far
        // off screen — no count test needed. The gate is the bounding square:
        // its corners evaluate the SDF to an alpha of exactly zero, so the
        // circle test bought nothing.
        vec2 rel = p - uFishPos[i].xy;
        float bound = 1.5 * sc;
        if (abs(rel.y) > bound || abs(rel.x) > bound) continue;

        float dir = uFishSeed[i].w < 0.5 ? -1.0 : 1.0;
        float warm = uFishWarm[i];                // colour variation
        if (fishCaus < 0.0)
            fishCaus = causW > 0.001
                ? caustics(vec2(uv.x * asp, uv.y) * 7.0, t) * causW * sunNear
                : 0.0;

        vec2 fp = rel / sc;
        fp.x *= dir;
        // Tilt slightly as they rise and fall.
        fp = mat2(uFishPos[i].z, -uFishPos[i].w,
                  uFishPos[i].w, uFishPos[i].z) * fp;

        vec2 d = fish_sdf(fp, t * (3.2 + lay * 3.4) + fi);
        float edge = mix(0.30, 0.07, lay);             // distant fish are softer
        float aBody = smoothstep(edge, -edge * 0.3, d.x);
        float aFin  = smoothstep(edge, -edge * 0.3, d.y);

        // Countershading: dark back, pale belly. The accent share stays
        // small: an accent can be any hue, and a cold accent would strip
        // the warm fish of their warmth.
        vec3 warmCol = mix(WARM_FISH, uAccent, 0.25);
        warmCol = mix(warmCol, vec3(0.85, 0.86, 0.90), 0.20);
        vec3 coolCol = mix(uShallow * 1.5, vec3(0.80, 0.86, 0.92), 0.45);
        vec3 base = mix(coolCol, warmCol, step(0.55, warm));
        vec3 fishCol = base * mix(0.30, 1.15, smoothstep(0.30, -0.22, fp.y));
        fishCol += uLight * (0.06 + 0.25 * fishCaus) * smoothstep(0.0, 0.22, fp.y) * lay;

        // Fins are thinner and more translucent than the body.
        vec3 finCol = mix(col, base * 0.75, 0.55);

        // Water between viewer and fish washes the distant ones out.
        float clarity = mix(0.22, 0.96, lay * lay);
        fishCol = mix(col * 0.80, fishCol, clarity);
        finCol = mix(col * 0.88, finCol, clarity);

        col = mix(col, finCol, aFin * 0.85);
        col = mix(col, fishCol, aBody);

        // Eye, on near fish only.
        float eye = smoothstep(0.075, 0.045, length(fp - vec2(0.33, 0.055)));
        col = mix(col, vec3(0.03, 0.04, 0.06), eye * aBody * smoothstep(0.35, 0.75, lay));
    }

    // ---- a small school drifting past --------------------------------
    {
        float sdir = 1.0;
        vec2 srel = p - uSchoolPos;
        if (dot(srel, srel) < 0.30 * 0.30)
        //#unroll
        for (int i = 0; i < 9; i++) {
            float fi = float(i);
            vec2 h = uSchoolSeed[i].xy;
            vec2 off = (h - 0.5) * vec2(0.42, 0.16);
            vec2 rel2 = srel - off;
            if (dot(rel2, rel2) > 0.042 * 0.042) continue;
            vec2 fp = rel2 / 0.026;
            fp.x *= sdir;
            vec2 d = fish_sdf(fp, t * 7.0 + fi * 1.9);
            float a = smoothstep(0.45, -0.1, min(d.x, d.y));
            vec3 sc = mix(col * 0.85, uShallow * 1.25 + uLight * 0.05, 0.55);
            col = mix(col, sc, a * 0.75);
        }
    }

    // ---- jellyfish -----------------------------------------------------
    // Translucent bells riding their own pulse upward. They are glass in
    // water: they mostly borrow the colour behind them, and their rims only
    // truly light up on the sunward side of the scene.
    //#unroll
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float lay = uJellySeed[i].z;
        float sc = mix(0.05, 0.105, lay);
        float bound = 2.7 * sc;
        // The bell drifts in x far more slowly than it rises; the column
        // test alone clears most pixels.
        if (abs(p.x - uJellyPos[i].x) > bound) continue;
        // Materialise out of the gloom and dissolve near the surface,
        // instead of popping in below the sand.
        float lifecyc = uJellyPos[i].z;
        vec2 rel = p - uJellyPos[i].xy;
        if (dot(rel, rel) > bound * bound) continue;
        vec2 q = rel / sc;
        float ph = uJellyPh[i];
        float pulse = uJellyPos[i].w;
        // The bell squeezes tall then relaxes wide.
        vec2 qb = q * vec2(1.0 + 0.09 * pulse, 1.0 - 0.11 * pulse);
        float dome = length(vec2(qb.x, max(qb.y, 0.0) * 1.28)) - 1.0;
        float skirt = qb.y + 0.52 + 0.13 * cos(qb.x * 6.0 + 0.8 * pulse);
        float bell = max(dome, -skirt);
        float aBell = smoothstep(0.06, -0.10, bell);
        // Four trailing tentacles, swaying more the further they hang.
        float ten = 0.0;
        if (q.y < -0.30) {
            for (int j = 0; j < 4; j++) {
                float fj = float(j);
                float xo = (fj - 1.5) * 0.40;
                float sway = 0.14 * sin(q.y * 1.9 - ph * 0.8 + fj * 1.9)
                           * clamp(-q.y * 0.6, 0.0, 1.4);
                float dt2 = abs(q.x - (xo + sway));
                ten = max(ten, smoothstep(0.055, 0.015, dt2));
            }
            ten *= smoothstep(-2.5, -1.0, q.y);
        }
        float sunB = exp(-length(p - sun) * 1.1);
        float clarity = mix(0.35, 0.9, lay) * lifecyc;
        vec3 jCol = mix(uShallow * 1.3, uLight, 0.22);
        // The veil: barely-there body, a luminous rim, a soft core.
        col = mix(col, jCol, aBell * 0.26 * clarity);
        float rim = smoothstep(-0.55, -0.04, dome) * aBell;
        col += jCol * rim * (0.08 + 0.30 * sunB) * clarity;
        vec2 qc = q - vec2(0.0, 0.18);
        col += (uAccent * 0.45 + uLight * 0.55)
             * exp(-dot(qc, qc) * 2.6) * aBell * 0.10 * clarity;
        col = mix(col, jCol, ten * 0.20 * clarity);

        // Bioluminescence: once the sun is gone the bells stop borrowing
        // light and make their own — a cold cyan that breathes with the
        // pulse. Added before the grade, so the factors are boosted to
        // survive the night pulldown (0.34x and a blue shift) below.
        float night = 1.0 - uDay;
        if (night > 0.01) {
            // Green-heavy on purpose: the grade's blue shift cools it to the
            // cold cyan real biolume has; a balanced cyan here reads white.
            vec3 glowC = vec3(0.16, 1.0, 0.52);
            float breathe = 0.65 + 0.35 * pulse;
            col += glowC * rim * night * breathe * 0.65 * clarity;
            col += glowC * exp(-dot(qc, qc) * 2.4)
                 * aBell * night * breathe * 0.30 * clarity;
            // A soft aura past the rim: light bleeding into the water. It
            // fades to nothing well inside the bounding disc, so the entity
            // gate never shows a seam.
            col += glowC * exp(-dot(q, q) * 0.60)
                 * (1.0 - aBell) * night * breathe * 0.10 * clarity;
            col += glowC * ten * night * 0.20 * clarity;
        }
    }

    // ---- bubbles and drifting particles -------------------------------
    col += uLight * bubbles(uv, t, asp) * 0.45;
    col += uLight * snow(uv, t, asp) * 0.09 * (1.0 - depth * 0.4);

    // ---- grade ---------------------------------------------------------
    // A quiet S-curve, and depths that drift teal instead of staying the
    // same hue as the shallows: cheap density.
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.15);
    col *= mix(vec3(1.0), vec3(0.95, 1.01, 1.03), grad * 0.7);

    // ---- time of day ---------------------------------------------------
    // uDay rides the real sun through civil twilight. Night pulls the whole
    // scene down and blue in one place, so the sand and stone constants dim
    // with the water and the sun well reads as moonlight.
    col *= mix(0.34, 1.0, uDay);
    col = mix(col * vec3(0.72, 0.84, 1.18), col, uDay);
    // Golden hour: a warm cast that peaks mid-twilight and lives near the
    // surface — the floor stays cool, which is what dusk looks like from
    // under water.
    float gold = smoothstep(0.05, 0.35, uDay) * (1.0 - smoothstep(0.45, 0.85, uDay));
    col = mix(col, col * vec3(1.42, 0.96, 0.55), gold * (0.25 + 0.55 * uv.y));
    // ...and an amber halo hanging around the low sun itself.
    col += vec3(0.95, 0.45, 0.18) * gold * exp(-length(p - sun) * 1.2) * 0.20;

    // ---- vignette ------------------------------------------------------
    float vig = smoothstep(1.30, 0.20, length(p * vec2(0.85, 1.15)));
    col *= mix(0.72, 1.0, vig);

    // Dither, otherwise the gradient bands badly at 8 bits.
    float dth = (hash11(fc.x + fc.y * 1000.0 + floor(t * 24.0)) - 0.5) / 255.0;

    gl_FragColor = vec4(max(col + dth, 0.0), 1.0);
}

#endif
