#!/usr/bin/env python3
"""Compare benchmark arms and emit Markdown plus a machine-readable summary.

Reads the per-run JSON written by run_title_benchmark.py. Labels are expected to
look like `<scene>-<arm>-r<N>`; the arm is taken from the second-to-last field so
`lm-fixed-r2` groups under scene `lm`, arm `fixed`.

Per-frame counters are the headline, not fps. fps depends on how busy the host
was; bursts/frame and cycles/frame do not, and bursts is dispatcher re-entries --
the quantity the region work exists to reduce.

A delta is only reported as meaningful when it clears the measured run-to-run
spread of the baseline arm. Anything inside the noise is printed as "~" rather
than dressed up with a sign.
"""

import argparse
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def load(directory):
    runs = defaultdict(list)
    for path in sorted(Path(directory).glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        if not data.get("valid", True):
            continue
        label = data.get("label", path.stem)
        parts = label.split("-")
        if len(parts) < 3 or not parts[-1].startswith("r"):
            continue
        arm = parts[-2]
        scene = "-".join(parts[:-2])
        runs[(scene, arm)].append(data)
    return runs


def mean_of(runs, key):
    values = [r[key] for r in runs if r.get(key)]
    return statistics.mean(values) if values else None


def spread(runs, key):
    values = [r[key] for r in runs if r.get(key)]
    if len(values) < 2:
        return 0.0
    return statistics.stdev(values) / statistics.mean(values) * 100.0


def delta(new, old, noise):
    """Percent change, or None when it does not clear the noise floor."""
    if not old or not new:
        return None
    change = (new - old) / old * 100.0
    if abs(change) <= max(noise, 1.0):
        return None
    return change


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory")
    parser.add_argument("--baseline", default="fixed", help="arm to compare against")
    parser.add_argument("--json-out", help="write the summary as JSON too")
    args = parser.parse_args()

    runs = load(args.directory)
    if not runs:
        print("no valid runs found", file=sys.stderr)
        return 1

    scenes = sorted({scene for scene, _ in runs})
    arms = sorted({arm for _, arm in runs})
    summary = []

    print(f"| scene | arm | runs | fps | fps sd% | bursts/frame | cycles/frame | fallback |")
    print(f"|---|---|---:|---:|---:|---:|---:|---:|")
    for scene in scenes:
        for arm in arms:
            group = runs.get((scene, arm))
            if not group:
                continue
            fb = sum(r.get("shutdown", {}).get("fallback", 0) for r in group)
            row = {
                "scene": scene,
                "arm": arm,
                "runs": len(group),
                "fps": mean_of(group, "fps"),
                "fps_sd_pct": spread(group, "fps"),
                "bursts_per_frame": mean_of(group, "bursts_per_frame"),
                "cycles_per_frame": mean_of(group, "cycles_per_frame"),
                "fallback": fb,
            }
            summary.append(row)
            print(f"| {scene} | {arm} | {row['runs']} | {row['fps'] or 0:.2f} | "
                  f"{row['fps_sd_pct']:.1f} | {row['bursts_per_frame'] or 0:.1f} | "
                  f"{(row['cycles_per_frame'] or 0) / 1e6:.2f}M | {fb} |")

    print()
    print(f"Deltas vs `{args.baseline}` (blank = inside the noise floor):")
    print()
    print("| scene | arm | fps | bursts/frame | cycles/frame |")
    print("|---|---|---:|---:|---:|")
    for scene in scenes:
        base = runs.get((scene, args.baseline))
        if not base:
            continue
        noise = spread(base, "fps")
        for arm in arms:
            if arm == args.baseline:
                continue
            group = runs.get((scene, arm))
            if not group:
                continue

            def fmt(key, floor):
                d = delta(mean_of(group, key), mean_of(base, key), floor)
                return "~" if d is None else f"{d:+.1f}%"

            print(f"| {scene} | {arm} | {fmt('fps', noise)} | "
                  f"{fmt('bursts_per_frame', 1.0)} | {fmt('cycles_per_frame', 1.0)} |")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"\n-> {args.json_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
