#!/usr/bin/env python3
"""Score a candidate aquarium against the recorded baseline.

Three numbers matter and they are not interchangeable:

  fidelity  the scene must still be the scene.  A shader "optimisation" that
            drops the kelp is infinitely fast and worthless, so every frame in
            the split is compared against a golden render and a failed
            comparison zeroes the score outright.
  ms/frame  measured with the two-render-target pipeline (--bench --pipe).
            Anything that draws repeatedly into one target on this GPU is
            hidden-surface-removed and reports fantasy numbers.
  PSS       of the real client, parked on the `background` layer where the
            opaque omarchy-background surface hides it, so measuring costs the
            desktop nothing.

Splits exist so the optimiser cannot tune to the frames it can see: `dev` is
reported every cycle, `test` is held out for the final merge.
"""

import argparse, json, os, pathlib, re, shlex, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench"
BASELINE = BENCH / "baseline.json"


def _golden_dir():
    """Golden frames are 30 MB of PPM, so they stay out of git.

    An executor working in a `git worktree` therefore has an empty bench/golden
    of its own -- fall back to the main checkout's, which is where --record
    wrote them.  $AQUARIUM_GOLDEN overrides both."""
    env = os.environ.get("AQUARIUM_GOLDEN")
    if env:
        return pathlib.Path(env)
    local = BENCH / "golden"
    if (local / "dev").is_dir() and any((local / "dev").iterdir()):
        return local
    common = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute",
         "--git-common-dir"], capture_output=True, text=True)
    if common.returncode == 0 and common.stdout.strip():
        main_root = pathlib.Path(common.stdout.strip()).parent
        shared = main_root / "bench" / "golden"
        if shared.is_dir():
            return shared
    return local


GOLDEN = _golden_dir()

PREVIEW = ROOT / "build" / "aquarium-preview"
CLIENT = ROOT / "build" / "omarchy-aquarium"

# The reference checkout: the baseline commit, built once, kept beside the main
# repo and benched against the candidate IN THE SAME RUN.
#
# Absolute timings on this machine are not comparable across time. The GPU's
# clock state is a hidden variable -- the live aquarium daemon holds clocks up
# with a permanent trickle draw, and heavy benching starves it into its 30 fps
# lock, after which everything measures ~25% slower until it recovers. Trunk
# itself measured 10.90 ms and then 13.50 ms for byte-identical output within
# two hours. Only an A/B taken minutes apart means anything, so every eval
# rebuilds nothing on the reference side and interleaves the two.
def _ref_dir():
    """As with the goldens, an executor in a `git worktree` must reach the
    reference built beside the MAIN checkout, not a sibling of its own temp
    directory."""
    env = os.environ.get("AQUARIUM_REF")
    if env:
        return pathlib.Path(env)
    common = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute",
         "--git-common-dir"], capture_output=True, text=True)
    if common.returncode == 0 and common.stdout.strip():
        main_root = pathlib.Path(common.stdout.strip()).parent
        return main_root.parent / (main_root.name + "-ref")
    return ROOT.parent / (ROOT.name + "-ref")


REF = _ref_dir()
REF_PREVIEW = REF / "build" / "aquarium-preview"
REF_CLIENT = REF / "build" / "omarchy-aquarium"
PAIRS = 5    # A/B/B/A/...; the ratio of MINIMA is what gets scored

# Bench resolution: the panel's own, because occupancy (111 GPRs, 896/1024
# threads) is what the shader is bound by and that only shows at full size.
BENCH_W, BENCH_H = 2560, 1600
BENCH_FRAMES = 60
BENCH_REPEATS = 3

# Fidelity renders are smaller -- gating in the shader is uv-relative, so the
# content is the same and the comparison stays cheap.
FID_W, FID_H = 1280, 800

# (name, extra preview args).  Between them these light every gated block:
# the caustic band (uv.y>0.75), the floor field (uv.y<0.40), the night
# bioluminescence gate, the golden-hour grade, and every entity loop.
FRAMES = {
    "dev": [
        ("day_mid",     ["--time", "8.0",    "--day", "1.0"]),
        ("day_drift",   ["--time", "37.5",   "--day", "1.0"]),
        ("golden_hour", ["--time", "112.25", "--day", "0.40", "--sun", "0.50,0.55"]),
        ("night",       ["--time", "201.70", "--day", "0.0",  "--sun", "-0.34,0.72"]),
        ("turtle_pass", ["--time", "213.00", "--day", "1.0",  "--turtle", "1"]),
    ],
    "test": [
        ("day_late",    ["--time", "64.75",  "--day", "1.0",  "--sun", "0.70,0.90"]),
        ("dusk",        ["--time", "150.10", "--day", "0.25", "--sun", "0.66,0.61"]),
        ("night_late",  ["--time", "402.40", "--day", "0.0",  "--sun", "-0.34,0.72"]),
        ("sparse",      ["--time", "95.00",  "--day", "1.0",  "--fish", "3",
                         "--weed", "4", "--jelly", "1", "--anemone", "0", "--starfish", "0"]),
        ("dense",       ["--time", "128.00", "--day", "1.0",  "--fish", "16",
                         "--weed", "20", "--jelly", "5", "--anemone", "4", "--starfish", "4"]),
    ],
}

# A frame may drift by a hair -- reassociated float math, a folded constant --
# but not by a feature.  Tuned on the baseline: identical renders score 0.
MEAN_ABS_MAX = 1.20      # mean |delta| per channel, 0-255
P999_MAX     = 12.0      # 99.9th percentile |delta|, catches a deleted entity


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def build():
    r = run(["make", "-C", str(ROOT), "all", "preview"])
    if r.returncode:
        return "build failed:\n" + (r.stderr or r.stdout)[-4000:]
    return None


def read_ppm(path):
    """P6 loader.  No numpy dependency -- the venv is arbor's, not the repo's."""
    data = pathlib.Path(path).read_bytes()
    fields, pos = [], 0
    while len(fields) < 4:
        if data[pos:pos + 1].isspace():
            pos += 1
            continue
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    if fields[0] != b"P6":
        raise ValueError(f"{path}: not a P6 PPM")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos:pos + w * h * 3]


def compare(a_path, b_path):
    aw, ah, a = read_ppm(a_path)
    bw, bh, b = read_ppm(b_path)
    if (aw, ah) != (bw, bh):
        return {"ok": False, "why": f"size {aw}x{ah} vs {bw}x{bh}"}
    hist = [0] * 256
    for x, y in zip(a, b):
        hist[x - y if x > y else y - x] += 1
    n = len(a)
    total = sum(d * c for d, c in enumerate(hist))
    mean = total / n
    # 99.9th percentile and max, straight off the histogram.
    cut, seen, p999, mx = n * 0.999, 0, 0, 0
    for d, c in enumerate(hist):
        if not c:
            continue
        mx = d
        seen += c
        if p999 == 0 and seen >= cut:
            p999 = d
    ok = mean <= MEAN_ABS_MAX and p999 <= P999_MAX
    return {"ok": ok, "mean_abs": round(mean, 4), "p999": p999, "max": mx,
            "why": "" if ok else f"mean {mean:.3f} (<= {MEAN_ABS_MAX}) p999 {p999} (<= {P999_MAX})"}


def render(name, args, out):
    cmd = [str(PREVIEW), "--width", str(FID_W), "--height", str(FID_H),
           "--out", str(out)] + args
    r = run(cmd)
    if r.returncode or not pathlib.Path(out).exists():
        return f"render {name} failed: {(r.stderr or r.stdout)[-800:]}"
    return None


def fidelity(split, tmp):
    """Every frame in the split against its golden.  Any miss fails the run."""
    results, worst = {}, None
    for name, args in FRAMES[split]:
        gold = GOLDEN / split / f"{name}.ppm"
        if not gold.exists():
            return None, f"missing golden {gold} -- run `eval.py --record` on the baseline"
        out = tmp / f"{name}.ppm"
        err = render(name, args, out)
        if err:
            return None, err
        results[name] = compare(gold, out)
        out.unlink(missing_ok=True)
        if not results[name]["ok"]:
            worst = f"{name}: {results[name]['why']}"
    return results, worst


def bench_once(binary):
    r = run([str(binary), "--width", str(BENCH_W), "--height", str(BENCH_H),
             "--bench", str(BENCH_FRAMES), "--pipe"])
    m = re.search(r"pipelined\s+([0-9.]+)\s+ms/frame", r.stderr + r.stdout)
    if not m:
        return None, f"bench produced no timing: {(r.stderr or r.stdout)[-800:]}"
    return float(m.group(1)), None


def med(xs):
    xs = sorted(xs)
    return xs[len(xs) // 2]


def bench_paired():
    """Interleave candidate and reference so both see the same clock state.

    Scored on the MINIMUM of each side, not the median. The DVFS governor only
    ever costs time -- a run can collapse to 29 ms and back inside one eval --
    so the fastest observation is the least contaminated one, and a median just
    averages in however much sag happened to land on that side. The order is
    flipped every pair so a monotonic drift cannot favour whichever side goes
    first."""
    cand, ref = [], []
    for i in range(PAIRS):
        order = [(PREVIEW, cand), (REF_PREVIEW, ref)]
        if i % 2:
            order.reverse()
        for binary, sink in order:
            v, e = bench_once(binary)
            if e:
                return None, ("candidate " if sink is cand else "reference ") + e
            sink.append(v)
    return {"ms_per_frame": min(cand), "ref_ms_per_frame": min(ref),
            "speedup": round(min(ref) / min(cand), 4),
            "cand_runs": cand, "ref_runs": ref}, None


MEM_SETTLE = 10.0    # Mesa frees its shader-compile scratch around 8 s in;
                     # anything read before that is still on the way down.
MEM_LAUNCHES = 3     # cross-launch spread is ~3% with a one-sided high tail
MEM_DEADBAND = 0.010 # min-of-3 is reproducible to ~0.5%; ignore smaller moves


def _probe_once(client=None):
    """One launch, parked on the `background` layer where the opaque
    omarchy-background surface hides it, so measuring costs the desktop
    nothing."""
    proc = subprocess.Popen(
        [str(client or CLIENT), "--layer", "background", "--no-react", "--no-suspend",
         "--theme", "--fps", "60"],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        time.sleep(MEM_SETTLE)
        if proc.poll() is not None:
            return None, f"client exited early: {proc.stderr.read()[-600:]}"
        roll = pathlib.Path(f"/proc/{proc.pid}/smaps_rollup").read_text()
        return tuple(int(re.search(rf"^{k}:\s+(\d+) kB", roll, re.M).group(1))
                     for k in ("Pss", "Rss", "Private_Dirty")), None
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def memory(client=None):
    """Steady-state footprint of the real client.

    RSS is mostly Mesa's shared LLVM and lies about what a second copy would
    cost; PSS is the number that moves when the renderer stops allocating, and
    Private_Dirty is the part this process alone owns (PSS minus a steady
    ~4 MB of shared Mesa).

    Taken as the *minimum* of three launches, not the median: a single launch
    settles to a stable value within itself but varies ~3% between launches
    with a one-sided high tail, so the low reading is the real floor and the
    high ones are contamination.  Min-of-3 reproduces to ~0.5%."""
    if not os.environ.get("WAYLAND_DISPLAY"):
        return None, "no WAYLAND_DISPLAY -- memory probe needs the live compositor"
    runs = []
    for _ in range(MEM_LAUNCHES):
        got, err = _probe_once(client)
        if err:
            return None, err
        runs.append(got)
    best = min(runs)  # ordered by Pss, which the other two track
    return {"pss_mb": round(best[0] / 1024, 3), "rss_mb": round(best[1] / 1024, 3),
            "private_dirty_mb": round(best[2] / 1024, 3),
            "pss_launches_kb": [r[0] for r in runs]}, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--split", choices=["dev", "test"], default="dev")
    ap.add_argument("--record", action="store_true",
                    help="write golden frames and baseline.json from the current tree")
    ap.add_argument("--no-memory", action="store_true",
                    help="skip the PSS probe (needs a Wayland session)")
    a = ap.parse_args()

    out = {"split": a.split}
    err = build()
    if err:
        print(json.dumps({**out, "score": 0.0, "error": err}, indent=2))
        return 1

    tmp = BENCH / "_tmp"
    tmp.mkdir(parents=True, exist_ok=True)

    if a.record:
        for split, frames in FRAMES.items():
            (GOLDEN / split).mkdir(parents=True, exist_ok=True)
            for name, args in frames:
                e = render(name, args, GOLDEN / split / f"{name}.ppm")
                if e:
                    print(e, file=sys.stderr)
                    return 1
        ms, e = bench_once(PREVIEW)
        if e:
            print(e, file=sys.stderr)
            return 1
        mem, memerr = (None, "skipped") if a.no_memory else memory()
        base = {"ms_per_frame": ms, "bench_res": [BENCH_W, BENCH_H],
                "pss_mb": (mem or {}).get("pss_mb"), "memory_note": memerr,
                "recorded": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                "commit": run(["git", "-C", str(ROOT), "rev-parse", "HEAD"]).stdout.strip(),
                "note": "Context only. Scoring is a paired A/B against the "
                        "reference build; absolute timings on this machine are "
                        "not comparable across time."}
        BASELINE.write_text(json.dumps(base, indent=2) + "\n")
        print(json.dumps(base, indent=2))
        return 0

    fid, worst = fidelity(a.split, tmp)
    out["fidelity"] = fid
    if worst:
        out["score"] = 0.0
        out["error"] = "fidelity regression -- " + worst
        print(json.dumps(out, indent=2))
        return 1

    if not REF_PREVIEW.exists():
        print(json.dumps({**out, "score": 0.0,
                          "error": "no reference build at %s -- create it with "
                                   "`git worktree add %s <baseline-commit> && "
                                   "make -C %s all preview`" % (REF, REF, REF)},
                         indent=2))
        return 1

    b, e = bench_paired()
    if e:
        print(json.dumps({**out, "score": 0.0, "error": e}, indent=2))
        return 1
    out.update(b)

    mem_ratio = 1.0
    if a.no_memory:
        out["memory"] = "skipped"
    else:
        mc, e1 = memory(CLIENT)
        mr, e2 = memory(REF_CLIENT)
        if e1 or e2:
            out["memory"] = e1 or e2
        else:
            out["pss_mb"] = mc["pss_mb"]
            out["ref_pss_mb"] = mr["pss_mb"]
            out["private_dirty_mb"] = mc["private_dirty_mb"]
            raw = mr["pss_mb"] / mc["pss_mb"]
            # min-of-3 reproduces to ~0.5%; a "win" inside 1% is measurement.
            mem_ratio = 1.0 if abs(raw - 1.0) < MEM_DEADBAND else raw
            out["memory_ratio"] = round(mem_ratio, 4)
            out["memory_ratio_raw"] = round(raw, 4)

    # Frame time is what the user sees every 16.7 ms; PSS is a background cost
    # on a 16 GB laptop. Weighted 3:1, 1.0 == reference, higher is better.
    out["score"] = round(0.75 * out["speedup"] + 0.25 * mem_ratio, 4)
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
