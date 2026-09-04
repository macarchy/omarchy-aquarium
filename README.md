# omarchy-aquarium

An animated underwater scene as the desktop background on Hyprland/Omarchy.
The whole aquarium — water, light shafts, caustics, sand, weed, boulders,
bubbles and fish — is one fragment shader; there is no video, no image, and no
scene graph.

![the scene](docs/preview.png)

## Install

    git clone https://github.com/macarchy/omarchy-aquarium
    cd omarchy-aquarium
    ./install.sh

That builds the renderer into `~/.local/bin` and wires it into Omarchy: the
**SUPER + ALT + A** toggle, a login line that puts the tank back the way you
left it, and the `theme-set` hook that re-reads your colours. It writes into
`~/.config/hypr/bindings.lua` and `autostart.lua` on Omarchy 4, or into your
hyprlang config on earlier versions, fenced by markers and appended only once —
re-run it to update. `./install.sh --uninstall` takes all of it back out.

`make install` alone only places the three binaries; it wires nothing, so
SUPER + ALT + A will not exist.

## How it sits on the desktop

The renderer opens one `wlr-layer-shell` surface per output on the **bottom**
layer: above the wallpaper, below every window. Omarchy's own background plugin
keeps running underneath, so the solar wallpaper rotation, `omarchy theme bg`
and the lock screen are all untouched — turning the aquarium off falls straight
back to the normal wallpaper with nothing to restore.

The surface takes an empty input region, so clicks and touches pass through it.

## Usage

    omarchy-aquarium-toggle          # on/off  (bound to SUPER + ALT + A)
    omarchy-aquarium-toggle status
    omarchy-aquarium-toggle restore  # at login: back to how you left it
    omarchy-aquarium-toggle restart  # re-read the theme, keep on/off

On and off are remembered in `~/.local/state/omarchy-aquarium/enabled`, so a
session autostart can run `restore` and get the tank the user last chose —
started when the state is unwritten, left alone when they turned it off. Wire
it in `~/.config/hypr/autostart.lua`:

    o.exec_on_start("omarchy-aquarium-toggle restore")

Renderer flags:

    --fps N          frames per second (default 60; at the display's own rate
                     frames lock to the compositor's vblank)
    --speed X        animation speed multiplier
    --fish N         number of fish, 0-16 (default 11)
    --weed N         seaweed and kelp blades, 0-20 (default 14)
    --jelly N        jellyfish, 0-5 (default 3)
    --anemone N      anemones on the boulders, 0-4 (default 2)
    --starfish N     starfish on the sand, 0-4 (default 2)
    --turtle 0|1     a turtle passes by every few minutes (default 1)
    --theme          tint the water from the current Omarchy theme
    --output NAME    only draw on this output
    --layer L        bottom (default) or background
    --buffer-scale N render at scale N instead of the output's
    --no-react       ignore the cursor and the control pipe
    --no-suspend     keep drawing even behind a fullscreen window
    --stats          log frames per second every 5s
    --shader FILE    load a fragment shader from FILE instead of the built-in

The toggle script runs `--theme --fps 60`. To change that, put flags in
`~/.config/omarchy-aquarium/options`, one line or space separated:

    --fish 14 --jelly 5 --turtle 0 --speed 0.8 --fps 24

`--theme` reads `~/.local/state/omarchy/current/theme/colors.toml` and steers
the water hue from it, while sand, stone and fish keep fixed hues — an accent
colour is allowed to be anything, and blue sand does not read as sand. An
installed `theme-set` hook restarts the aquarium when the theme changes.

### Hooks

Executables in `~/.config/omarchy-aquarium/hooks/` run whenever the tank goes on
or off, detached, with `on` or `off` as their argument. Turning it on or off
repaints the whole screen and nothing else can tell that happened — on a desktop
whose bar picks its text colour by sampling the screen, that is the difference
between white text over the water and black text over the water until some timer
notices. An internal restart (the theme hook) is not a state change and fires
nothing.

## Reactivity

The tank notices the desktop, and all of it is CPU-side — the shader gets the
same uniforms either way, so reacting costs nothing per pixel:

- **The cursor.** Fish keep a polite distance: bring the pointer near one and
  it darts away, nose pitched into the motion, then drifts back to its path.
  Close (large) fish are shyer than distant ones. The cursor is polled from
  Hyprland at 15 Hz; the layer surface itself still takes no input.
- **Notifications.** `omarchy-aquarium-notify` (started by the toggle) watches
  the session bus read-only; a desktop notification writes `startle` to the
  control pipe at `$XDG_RUNTIME_DIR/omarchy-aquarium.ctl` and the whole tank
  flinches — each fish bolts along its swimming direction and settles over a
  few seconds. Try it: `omarchy-aquarium-toggle startle`. Anything else can
  poke the same pipe.
- **Night.** After civil twilight (the same solar tracking that moves the
  sun), the jellyfish become faintly bioluminescent: a cold cyan glow that
  breathes with the bell's pulse. This is the one shader-side piece, gated to
  jelly pixels; the daytime image is bit-identical to before.

`--no-react` turns off the cursor and the pipe (the night glow rides `--day`
/ solar state, not reactivity).

## Cost

At the native 2560x1600 buffer on an M2 (Asahi, Mesa) the full scene renders in
**~9.7 ms/frame with the GPU saturated**, and more slowly at the low clocks the
firmware grants a light duty cycle.

Two traps, both of which have cost real time here:

**Do not benchmark with back-to-back draws into one framebuffer.** Apple GPUs
hidden-surface-remove every opaque draw but the last, and that bench reports a
fantasy (0.4 ms). `--bench N --pipe` alternates render targets instead.

**Two things the AGX compiler does that change what is worth optimising.** It
predicates short loop bodies instead of branching -- the shader reports zero
loops and zero spills, everything is flattened -- so a `continue` guarding a
few instructions does not skip them, it only adds the test. Gating cheap work
is therefore worthless; a bound is only worth tightening when its body is big
enough to become a real branch (the seaweed blade and the anemone tentacle fan
are, a bubble's ring is not). And it packs two 16-bit values per register at
double issue rate, so `precision` is a first-class lever: this shader is
register-width-bound, and compiling it wholly `mediump` reaches 47 GPRs and
1.835x on an image that is wrong. Check `AGX_MESA_DEBUG=shaderdb` and expect
the instruction count to fall, not rise.

**Do not compare an absolute timing against one taken earlier.** The GPU's
clock state is a hidden variable: this renderer holds clocks up with a
permanent trickle draw, and sustained benching starves it into its even-30
lock, after which everything measures ~25% slower until it recovers -- which it
cannot while the benching continues. It is not thermal and cooling down does
not fix it. Byte-identical code measured 10.90 ms and 13.50 ms two hours apart
in one session. Only an A/B taken minutes apart means anything, which is what
`bench/eval.py` does:

    git worktree add ../omarchy-aquarium-ref <baseline-commit>
    make -C ../omarchy-aquarium-ref all preview
    python3 bench/eval.py --record        # golden frames, once
    python3 bench/eval.py --split dev     # score a change

It renders ten golden frames and gates on the 99.9th percentile of the
per-channel delta rather than the mean -- dropping three of five jellyfish
moves the mean by 0.22 of 255 and sails past any mean threshold, but spikes
p999 to 29 -- then benches candidate against reference alternately and scores
the ratio of the minima. `--split test` is the held-out half; keep it for the
final check.

What keeps the cost down:

**It stops completely when it cannot be seen.** Wayland has no occlusion signal
for layer surfaces, and Hyprland keeps sending frame callbacks to a background
layer nobody can see. So the renderer watches Hyprland's event socket and
suspends whenever the active workspace has a fullscreen window — and tears the
whole EGL surface down with it, handing ~50 MB of swapchain buffers back to the
system right when the fullscreen app is what wants the memory. The layer
surface unmaps and re-maps on resume. `--no-suspend` opts out.

**Presentation locks to the compositor.** At the display's own rate, frames are
paced by frame callbacks (no wall-clock beat against the vblank), the swap
never blocks the event loop, and the next frame is drawn while the previous
one waits for its vblank — half-idle GPUs look like light load to the DVFS
governor, which then never grants the clocks the deadline needs. Below the
display rate the idle gaps are deliberate: that is where the battery savings
live.

**It climbs out of the DVFS trap.** The GPU's clock governor makes the full
display rate bistable: miss the deadline once and the clocks sag, guaranteeing
the next miss. Three things break the loop: commits keep flowing on the wall
clock with up to two frames in flight (so the flip up to full rate does not
wait on the frame-callback chain it is trying to accelerate), a two-second
full-resolution burst warms the clocks whenever a full-rate attempt starts,
and while at full rate a ninth of a frame of throwaway work per cycle keeps
the GPU saturated enough that the governor holds the clocks. Delivered on an
M2: 53–58 fps with glass-blurred windows on screen. If the rate truly
collapses (under 75% for fifteen seconds), the renderer locks the exact half
rate — a perfectly even 30 beats an uneven 40 — and retries every two
minutes.

**Everything that moves is placed on the CPU.** Fish, bubble, jelly, school
and turtle positions are pure functions of time; `anim.c` computes them once
per frame and uploads them as uniforms, so per pixel the loops are two
compares per entity. Bubbles are seed-sorted by column and gated per quad.

**The entity seeds are data, not hashes.** Every fish, blade, boulder and jelly
used to derive its place from `fract(sin(big) * 43758…)` — and whether that
ran on the GPU or was constant-folded by the shader compiler depended on the
exact loop shape, so touching control flow silently rearranged the scene. The
seeds now live in `seeds.c` and are passed as uniforms; the embedded build
(`tools/gen_shader.py`) additionally unrolls the entity loops and inlines the
seeds as literals so per-entity positions hoist into the driver's once-per-draw
preamble. Per pixel, an open-water fragment runs a handful of compares per
entity and none of the heavy shading.

**The shader gates by region.** Caustics only exist where they survive 8-bit
quantisation, the sea-floor height field is skipped above the dunes' reach,
seaweed is tested per grove before per blade, and every entity has a cheap
bounding test before its real math.

## Building

    make            # build/omarchy-aquarium
    make install    # binaries into ~/.local/bin, nothing wired (see Install)
    make preview    # build/aquarium-preview, the offscreen renderer

`aquarium-preview` renders single frames without touching the desktop, which is
how the scene was tuned:

    ./build/aquarium-preview --time 33 --theme --out frame.ppm
    ./build/aquarium-preview --bench 200 --pipe --out /dev/null

    bash tests/test_toggle.sh    # the toggle's state, restore and hooks

Editing `src/aquarium.frag` and rerunning `make` is the whole iteration loop;
`--shader src/aquarium.frag` skips the rebuild.

Dependencies: `wayland-client`, `wayland-egl`, `egl`, `glesv2`,
`wayland-protocols` and `wayland-scanner`. The layer-shell protocol XML is
vendored in `protocol/`, so `wlr-protocols` need not be installed.
