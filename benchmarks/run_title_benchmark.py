#!/usr/bin/env python3
"""Measure a recompiled title's throughput through ModernGekko.

Why not just read `fps` from status.txt: in a headless run nothing presents, so
that field stays 0, and in a windowed run the emulator is throttled to real time
(`speed` pins at 1.00) -- a CPU-side win shows up as the emulator waiting
longer, not as a bigger number. Either way the field cannot move.

What this does instead:

  * Writes an isolated Dolphin user directory with `EmulationSpeed = 0`, which
    is Dolphin's "unlimited" setting. The runtime never sets that key itself, so
    the ini wins and the emulator runs as fast as the host allows.

  * Derives throughput from `frame_count`, which is populated even headless, over
    measured wall time. That is the real frames-per-second the CPU can sustain.

  * Captures ModernGekko's own shutdown counters -- native, fallback, bursts,
    cycles -- because `bursts` is dispatcher re-entries, which is exactly the
    quantity the region work exists to reduce, and it is deterministic across
    runs in a way frame timing is not.

A run is only comparable to another run of the same scene, so pin one with
--load-state rather than measuring whatever the title screen happens to do.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

STATUS_LINE = re.compile(r"^([a-z_]+)=(.*)$")
SHUTDOWN_LINE = re.compile(r"\[staticrecomp\] shutdown:\s*(.*)$")


def read_status(path):
    """status.txt is rewritten in place, so a torn read is expected; treat any
    failure as 'no sample yet' rather than an error."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    values = {}
    for line in text.splitlines():
        match = STATUS_LINE.match(line.strip())
        if match:
            values[match.group(1)] = match.group(2)
    return values or None


def to_number(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def write_user_directory(root, unthrottle):
    config_dir = root / "Config"
    config_dir.mkdir(parents=True, exist_ok=True)
    # 0.0 is Dolphin's unlimited-speed value. Audio is silenced because a real
    # backend paces the emulator to the sound card and would reintroduce the
    # very throttle this is removing.
    speed = "0.0000" if unthrottle else "1.0000"
    (config_dir / "Dolphin.ini").write_text(
        "[Core]\n"
        f"EmulationSpeed = {speed}\n"
        "\n"
        "[DSP]\n"
        "Backend = No Audio Output\n"
        "Volume = 0\n",
        encoding="utf-8",
    )


def send_command(automation_dir, name, body):
    commands = automation_dir / "commands"
    commands.mkdir(parents=True, exist_ok=True)
    (commands / name).write_text(body, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runner", required=True, help="moderngekko-run executable")
    parser.add_argument("--game", required=True, help="extracted game root")
    parser.add_argument("--module", required=True, help="recompiled module (.dll/.so)")
    parser.add_argument("--label", required=True, help="name for this arm, e.g. llvm-fixed")
    parser.add_argument("--seconds", type=float, default=60.0, help="measurement window")
    parser.add_argument("--warmup", type=float, default=15.0,
                        help="seconds to discard before measuring, so boot and "
                             "shader compilation do not land in the sample")
    parser.add_argument("--load-state", help="savestate to pin the scene")
    parser.add_argument("--progress-timeout", type=float, default=180.0,
                        help="how long to wait after boot for the first frame to "
                             "advance; restoring a large savestate can take a while")
    parser.add_argument("--throttled", action="store_true",
                        help="keep Dolphin's real-time throttle (measures nothing "
                             "useful for CPU work; here for comparison only)")
    parser.add_argument("--work-dir", help="scratch root (default: alongside --out)")
    parser.add_argument("--out", required=True, help="JSON results path")
    args = parser.parse_args()

    out_path = Path(args.out)
    work = Path(args.work_dir) if args.work_dir else out_path.parent / f"bench-{args.label}"
    user_dir = work / "user"
    automation_dir = work / "automation"
    # The user directory is deliberately NOT wiped between runs. Dolphin is
    # configured to wait for shaders before starting, so a cold cache turns boot
    # into minutes of compilation that has nothing to do with the CPU work being
    # measured. Keeping it makes repeat runs start in seconds; the warmup window
    # covers what is left.
    if automation_dir.exists():
        shutil.rmtree(automation_dir, ignore_errors=True)
    automation_dir.mkdir(parents=True, exist_ok=True)
    write_user_directory(user_dir, not args.throttled)

    command = [
        args.runner,
        "--game", args.game,
        "--module", args.module,
        "--user-dir", str(user_dir),
        "--automation-dir", str(automation_dir),
        "--headless",
        "--audio", "No Audio Output",
        "--no-mods",
    ]
    if args.load_state:
        command += ["--load-state", args.load_state]

    log_path = work / "runner.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)

        status_path = automation_dir / "status.txt"
        deadline = time.monotonic() + args.warmup + args.seconds + 120.0
        booted = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                break
            status = read_status(status_path)
            if status and status.get("booted") == "1" and status.get("state") == "running":
                booted = status
                break
            time.sleep(0.25)

        if booted is None:
            process.kill()
            process.wait(timeout=30)
            print(f"error: {args.label} never reached a running state; see {log_path}",
                  file=sys.stderr)
            return 1

        # `booted=1, state=running` is not the same as "executing guest code".
        # With --load-state the runtime reports running while a 30-45 MB state
        # is still being restored, and a fixed warmup can expire before a single
        # frame has advanced -- which produced 0-frame runs that looked like
        # 0.00 fps results rather than the failures they were.
        #
        # So wait for frame_count to actually move before starting the clock.
        progress_deadline = time.monotonic() + args.progress_timeout
        baseline = to_number((read_status(status_path) or {}).get("frame_count"))
        advanced = False
        while time.monotonic() < progress_deadline:
            if process.poll() is not None:
                break
            time.sleep(0.5)
            now = to_number((read_status(status_path) or {}).get("frame_count"))
            if now > baseline:
                advanced = True
                break

        if not advanced:
            process.kill()
            process.wait(timeout=30)
            print(f"error: {args.label} booted but never advanced a frame in "
                  f"{args.progress_timeout:.0f}s; see {log_path}", file=sys.stderr)
            return 1

        time.sleep(args.warmup)

        start_status = read_status(status_path) or {}
        start_frames = to_number(start_status.get("frame_count"))
        start_time = time.monotonic()

        samples = []
        while time.monotonic() - start_time < args.seconds:
            if process.poll() is not None:
                break
            time.sleep(1.0)
            sample = read_status(status_path)
            if sample:
                samples.append({
                    "t": round(time.monotonic() - start_time, 3),
                    "frame_count": to_number(sample.get("frame_count")),
                    "speed": to_number(sample.get("speed")),
                    "fps": to_number(sample.get("fps")),
                })

        end_status = read_status(status_path) or {}
        elapsed = time.monotonic() - start_time
        end_frames = to_number(end_status.get("frame_count"))

        send_command(automation_dir, "zzz-stop.txt", "command=stop\n")
        try:
            process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=30)

    shutdown = {}
    log_text = log_path.read_text(encoding="utf-8", errors="replace")
    for line in log_text.splitlines():
        match = SHUTDOWN_LINE.search(line)
        if not match:
            continue
        for field in match.group(1).split():
            if "=" in field:
                key, value = field.split("=", 1)
                shutdown[key] = to_number(value)

    frames = end_frames - start_frames

    # A run where frame_count never advances is a failed run, not a slow one.
    # Reporting it as 0.00 fps puts a number in the table that looks like a
    # measurement and is not -- it happened with a stale savestate that left the
    # emulator stalled, and a mean over that row would be silently wrong.
    unique_frames = {s["frame_count"] for s in samples}
    stalled = frames <= 0 or len(unique_frames) <= 1
    # A speed value that never changes across a 45 s window is the status file
    # going stale rather than a perfectly steady emulator.
    frozen_speed = len({s["speed"] for s in samples}) <= 1 and len(samples) > 3

    result = {
        "valid": not (stalled or frozen_speed),
        "invalid_reason": ("no frame progress" if stalled
                           else "frozen speed reading" if frozen_speed
                           else None),
        "label": args.label,
        "module": str(Path(args.module).resolve()),
        "module_bytes": Path(args.module).stat().st_size if Path(args.module).exists() else 0,
        "throttled": bool(args.throttled),
        "warmup_seconds": args.warmup,
        "measured_seconds": round(elapsed, 3),
        "frames": frames,
        # The load-bearing number. status.txt's own `fps` is 0 headless.
        "fps": round(frames / elapsed, 3) if elapsed > 0 else 0.0,
        "speed_mean": round(
            sum(s["speed"] for s in samples) / len(samples), 4) if samples else 0.0,
        "reported_fps_mean": round(
            sum(s["fps"] for s in samples) / len(samples), 3) if samples else 0.0,
        "shutdown": shutdown,
        "samples": samples,
    }
    # Dispatcher re-entries per frame is the comparison that survives a host
    # that ran hot or cold on the day.
    if frames > 0 and "bursts" in shutdown:
        result["bursts_per_frame"] = round(shutdown["bursts"] / frames, 2)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2), encoding="utf-8")

    if not result["valid"]:
        print(f"{args.label}: INVALID ({result['invalid_reason']}) -- "
              f"{int(frames)} frames over {elapsed:.1f}s; see {log_path}",
              file=sys.stderr)
    else:
        print(f"{args.label}: {result['fps']:.2f} fps over {elapsed:.1f}s "
              f"({int(frames)} frames), speed={result['speed_mean']:.2f}")
    if shutdown:
        print("  " + "  ".join(
            f"{k}={int(v)}" for k, v in sorted(shutdown.items())))
    print(f"  -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
