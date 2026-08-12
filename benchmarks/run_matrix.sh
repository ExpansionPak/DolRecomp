#!/usr/bin/env bash
# Runs the title benchmark across scenes and backends and prints a comparison.
#
# Scenes are savestates, not boot sequences: a boot measures loading, and the
# run-to-run spread on this harness is ~3.5%, so anything that varies between
# runs has to be pinned or it swamps the effect being measured.
#
# Repeats default to 3 because a single pair cannot resolve the 15% target the
# brief asks for, let alone its 5% regression bound.
#
# Usage:
#   benchmarks/run_matrix.sh <out-dir> [repeats] [seconds]
set -u

OUT="${1:?usage: run_matrix.sh <out-dir> [repeats] [seconds]}"
REPEATS="${2:-3}"
SECONDS_PER_RUN="${3:-45}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH="$HERE/run_title_benchmark.py"

LM_ROOT="${LM_ROOT:-C:/Users/douglaswhittingham/luigis-mansion-recomp}"
MK_ROOT="${MK_ROOT:-C:/Users/douglaswhittingham/mariokart-doubledash-recomp}"
RUNNER="${RUNNER:-$LM_ROOT/lib/ModernGekko/build/moderngekko-run.exe}"

mkdir -p "$OUT"

# label | game root | module | savestate
SCENES=$(cat <<ENTRIES
lm-foyer|$LM_ROOT/extracted/Luigis-Mansion-USA|$LM_ROOT/llvmcur/gGLME01_recomp.dll|$LM_ROOT/states/foyer.sav
mkdd-1p-race|$MK_ROOT/extracted/GM4E01|$MK_ROOT/build/release/MKDD-best/gGM4E01_recomp.dll|$MK_ROOT/states/race.sav
mkdd-4p-race|$MK_ROOT/extracted/GM4E01|$MK_ROOT/build/release/MKDD-best/gGM4E01_recomp.dll|$MK_ROOT/states/race-4p.sav
ENTRIES
)

echo "repeats=$REPEATS window=${SECONDS_PER_RUN}s -> $OUT"
echo

while IFS='|' read -r label game module state; do
  [ -z "$label" ] && continue
  if [ ! -f "$module" ]; then
    echo "skip $label: module missing ($module)"
    continue
  fi
  if [ ! -f "$state" ]; then
    echo "skip $label: savestate missing ($state)"
    continue
  fi
  for i in $(seq 1 "$REPEATS"); do
    python "$BENCH" \
      --runner "$RUNNER" \
      --game "$game" \
      --module "$module" \
      --load-state "$state" \
      --label "$label-r$i" \
      --warmup 12 \
      --seconds "$SECONDS_PER_RUN" \
      --work-dir "$OUT/work-$label" \
      --out "$OUT/$label-r$i.json" || echo "  run $label-r$i FAILED"
  done
done <<< "$SCENES"

echo
python - "$OUT" <<'SUMMARY'
import json, statistics, sys
from collections import defaultdict
from pathlib import Path

out = Path(sys.argv[1])
groups = defaultdict(list)
invalid = defaultdict(list)
for path in sorted(out.glob("*.json")):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        continue
    label = data.get("label", path.stem)
    # Runs that never advanced a frame are failures, not slow results; folding
    # them into a mean would quietly drag every comparison toward zero.
    if not data.get("valid", True):
        invalid[label.rsplit("-r", 1)[0]].append(data.get("invalid_reason", "?"))
        continue
    groups[label.rsplit("-r", 1)[0]].append(data)

if not groups:
    print("no results")
    sys.exit(0)

print(f"{'scene':<16}{'runs':>5}{'fps':>10}{'sd%':>7}"
      f"{'bursts/frame':>14}{'fallback':>10}")
for scene, runs in sorted(groups.items()):
    fps = [r["fps"] for r in runs if r.get("fps")]
    bpf = [r["bursts_per_frame"] for r in runs if r.get("bursts_per_frame")]
    fb = sum(r.get("shutdown", {}).get("fallback", 0) for r in runs)
    if not fps:
        continue
    mean = statistics.mean(fps)
    sd = (statistics.stdev(fps) / mean * 100.0) if len(fps) > 1 else 0.0
    print(f"{scene:<16}{len(runs):>5}{mean:>10.2f}{sd:>7.1f}"
          f"{(statistics.mean(bpf) if bpf else 0):>14.1f}{int(fb):>10}")

for scene, reasons in sorted(invalid.items()):
    print(f"  ! {scene}: {len(reasons)} invalid run(s) -- {reasons[0]}")
SUMMARY
