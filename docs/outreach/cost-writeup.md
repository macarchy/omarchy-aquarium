# Draft: four ways an Apple GPU lies about your fragment shader

**Not published.** A standalone write-up pulled out of the omarchy-aquarium
README's "Cost" section, so it stands on its own for anyone measuring a
full-screen fragment shader on Apple Silicon under Asahi Linux and Mesa. Every
number here was measured on an M2 while tuning one shader; none is a
manufacturer figure, and none of it is a benchmark of the GPU itself.

The shader in question draws a whole underwater scene — water, light shafts,
caustics, sand, seaweed, boulders, bubbles, fish — in one pass, full screen. At
the native 2560x1600 buffer it renders in **~9.7 ms/frame with the GPU
saturated**, and more slowly at the low clocks the firmware grants a light duty
cycle. Getting to a number you can trust at all turned out to be most of the
work.

## 1. Hidden-surface removal deletes your benchmark

The obvious way to time a fragment shader is to draw it a few hundred times
back-to-back into one framebuffer and divide. On an Apple GPU that measures
nothing. The GPU hidden-surface-removes every opaque draw but the last, so all
but one of them is never shaded, and the bench reports a **fantasy 0.4 ms** —
stable, repeatable, and entirely fictional.

The fix is to give the GPU no way to prove a draw is invisible: alternate render
targets between draws. Here that is what `--bench N --pipe` does. Any harness
that reports a suspiciously round, suspiciously small number on this hardware
should be suspected of measuring the same thing.

## 2. The compiler predicates short loops instead of branching

The AGX compiler flattens control flow aggressively. Compile the shader and ask
for its statistics: **zero loops, zero spills** — everything is predicated.

That inverts the usual optimisation instinct. A `continue` guarding a handful of
instructions does not skip them; it adds the test to them. Gating cheap work is
therefore worthless, and a "cheap early-out" can be a net loss. A bound is only
worth tightening when the body it guards is large enough to survive as a real
branch — in this shader, a seaweed blade and an anemone's tentacle fan qualify,
a bubble's ring does not.

The practical rule: before optimising a bound, find out how big its body is in
instructions. If the answer is "a few", the bound is not the problem and
tightening it will make things marginally worse.

## 3. Registers pack two 16-bit values, so `precision` is a first-class lever

AGX packs two 16-bit values per register and issues them at double rate. A
shader that is register-width-bound — as this one is — therefore responds to
precision qualifiers the way other hardware responds to instruction count.

The upper bound is easy to find and useless on its own: compiling this shader
wholly `mediump` reaches 47 GPRs and **1.835x**, on an image that is wrong.
That number is worth knowing precisely because it says how much is on the table
before you start choosing, value by value, which quantities can actually take
16 bits without visible banding.

Check the result with `AGX_MESA_DEBUG=shaderdb`, and expect the instruction
count to *fall*, not rise: a precision change that raises it has inserted
conversions, and you have paid for packing you did not get.

## 4. Clock state is a hidden variable, so absolute timings do not compare

This is the one that costs the most time, because nothing about it looks like an
error.

The GPU's clock governor is bistable at the full display rate: miss a frame
deadline once and the clocks sag, which guarantees the next miss. Sustained
benchmarking starves the GPU into an even-30 lock, after which **everything
measures ~25% slower** until it recovers — which it cannot while the
benchmarking continues. It is not thermal, and cooling down does not fix it.

Byte-identical code measured **10.90 ms and 13.50 ms two hours apart** in one
session. Every conclusion drawn by comparing a timing against one taken earlier
in that session was noise.

So: **only an A/B taken minutes apart means anything.** Bench candidate against
reference alternately, in the same clock state, and score the ratio of the
minima — the minimum, because the tail is all governor.

## 5. What a harness that survives all four looks like

The four traps above eliminate most of what a benchmark could be. What is left,
in this project, is `bench/eval.py`:

    git worktree add ../omarchy-aquarium-ref <baseline-commit>
    make -C ../omarchy-aquarium-ref all preview
    python3 bench/eval.py --record        # golden frames, once
    python3 bench/eval.py --split dev     # score a change

It renders ten golden frames and gates correctness on two numbers at once: the
mean per-channel delta (at most **1.20** of 255) and its **99.9th percentile**
(at most **12**). The percentile is the half that catches a deleted entity —
the mean cannot do that job alone, because dropping three of five jellyfish
moves it by **0.22 of 255** and sails past any mean threshold anyone would set,
while spiking p999 to **29**. Then it benches candidate against reference
alternately and scores the ratio of the minima.
`--split test` is a held-out half, kept for the final check so that a long
optimisation campaign cannot quietly overfit the metric.

## What the numbers bought

With measurement honest, the optimisations that mattered were mostly not about
arithmetic:

- **Stop drawing when you cannot be seen.** Wayland has no occlusion signal for
  layer surfaces, so the renderer watches the compositor's event socket and
  suspends behind a fullscreen window — tearing down the EGL surface with it and
  handing **~50 MB** of swapchain buffers back exactly when the fullscreen
  application wants them.
- **Climb out of the DVFS trap deliberately.** Commits keep flowing on the wall
  clock with up to two frames in flight, a two-second full-resolution burst warms
  the clocks whenever a full-rate attempt starts, and at full rate **a ninth of a
  frame** of throwaway work per cycle keeps the GPU saturated enough that the
  governor holds the clocks. Delivered on an M2: **53–58 fps** with
  glass-blurred windows on screen. If the rate truly collapses (under **75% for
  fifteen seconds**), lock the exact half rate — a perfectly even 30 beats an
  uneven 40 — and retry every **two minutes**.
- **Move everything that can be to the CPU.** Entity positions are pure
  functions of time, computed once per frame and uploaded as uniforms, so per
  pixel the loops are two compares per entity.
- **Make seeds data, not hashes.** Deriving positions from
  `fract(sin(big) * 43758…)` inside the shader meant that whether the value ran
  on the GPU or was constant-folded depended on the exact loop shape — so
  touching control flow silently rearranged the scene, and every A/B against a
  golden frame failed for the wrong reason.

Source, shader and harness: https://github.com/macarchy/omarchy-aquarium
