#!/usr/bin/env python3
"""Benchmark Color-Screen regular-screen geometry detection on external scans.

The script deliberately keeps large/private corpus images outside the source
repository.  It runs `colorscreen autodetect` with geometry-only postprocessing,
keeps the detector report for each run, and writes one CSV row from the stable
`detect_stats:` and `detect_stats_ms:` records.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import re
import subprocess
import time


DETECT_FIELDS = [
    "result", "type", "regions", "seed_pixels", "initial_grids",
    "initial_solver_failures", "flood_attempts", "flood_failures", "patches",
    "last_flood_failure", "color_opt_failures", "precompute_failures",
    "classmap_builds", "rgb_precomputes",
    "legacy_preclassification_sharpening",
]
TIME_FIELDS = [
    "optimize_colors_ms", "precompute_ms", "classmap_ms", "initial_solver_ms",
    "flood_ms", "final_solver_ms", "mesh_solver_ms", "total_ms",
]
COVERAGE_FIELDS = [
    "scan_coverage_pct", "screen_coverage_pct", "left_border_pct",
    "top_border_pct", "right_border_pct", "bottom_border_pct",
]
CSV_FIELDS = [
    "input", "mode", "algorithm", "repeat", "sharpen_radius", "sharpen_amount",
    "exit_code", "timed_out", "wall_ms", "peak_rss_kb",
] + COVERAGE_FIELDS + DETECT_FIELDS + TIME_FIELDS + ["report", "output_par"]

COVERAGE_RE = re.compile(
    r"Analyzed\s+([0-9.]+)% of scan and\s+([0-9.]+)%\s+of the screen area"
    r"(?:; left border:\s*([0-9.]+)%; top border:\s*([0-9.]+)%;"
    r" right border:\s*([0-9.]+)%; bottom border:\s*([0-9.]+)%)?"
)


def parse_key_values(line: str, prefix: str) -> dict[str, str]:
    """Parse whitespace-separated KEY=VALUE fields after PREFIX."""
    if not line.startswith(prefix):
        return {}
    result: dict[str, str] = {}
    for item in line[len(prefix):].strip().split():
        if "=" in item:
            key, value = item.split("=", 1)
            result[key] = value
    return result


def parse_report(path: Path) -> dict[str, str]:
    """Extract stable detector statistics and coverage from report PATH."""
    result: dict[str, str] = {}
    if not path.exists():
        return result
    for line in path.read_text(errors="replace").splitlines():
        match = COVERAGE_RE.search(line)
        if match:
            values = match.groups()
            for key, value in zip(COVERAGE_FIELDS, values):
                if value is not None:
                    result[key] = value
        if line.startswith("detect_stats:"):
            result.update(parse_key_values(line, "detect_stats:"))
        elif line.startswith("detect_stats_ms:"):
            timings = parse_key_values(line, "detect_stats_ms:")
            result.update({f"{key}_ms": value for key, value in timings.items()})
    return result


def read_rss_kb(pid: int) -> int | None:
    """Return resident memory for PID on procfs systems, otherwise None."""
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (FileNotFoundError, PermissionError, ValueError):
        pass
    return None


def write_detection_parameters(path: Path, radius: float, amount: float) -> None:
    """Write the minimal parameter file selecting detector sharpening."""
    path.write_text(
        "screen_alignment_version: 1\n"
        f"scr_detect_sharpen_radius: {radius:g}\n"
        f"scr_detect_sharpen_amount: {amount:g}\n"
        "screen_alignment_end\n"
    )


def algorithm_arguments(name: str) -> list[str]:
    """Return explicit fast/slow flood-fill switches for NAME."""
    if name == "both":
        return ["--fast-floodfill", "--slow-floodfill"]
    if name == "fast":
        return ["--fast-floodfill", "--no-slow-floodfill"]
    if name == "slow":
        return ["--no-fast-floodfill", "--slow-floodfill"]
    raise ValueError(name)


def run_one(args: argparse.Namespace, image: Path, mode: str, algorithm: str,
            repeat: int, writer: csv.DictWriter) -> None:
    """Run one benchmark case and append its summary through WRITER."""
    radius, amount = ((2.0, 3.0) if mode == "legacy23" else (0.0, 0.0))
    stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", image.stem)
    tag = f"{stem}.{mode}.{algorithm}.{repeat}"
    report = args.output_dir / f"{tag}.report"
    output_par = args.output_dir / f"{tag}.par"
    detect_par = args.output_dir / f"{tag}.detect.par"
    stdout_path = args.output_dir / f"{tag}.stdout"
    stderr_path = args.output_dir / f"{tag}.stderr"
    write_detection_parameters(detect_par, radius, amount)

    command = [
        str(args.colorscreen), f"--threads={args.threads}", "autodetect",
        str(image), str(output_par), f"--par={detect_par}",
        f"--report={report}", f"--screen-type={args.screen_type}",
        f"--scanner-type={args.scanner_type}", f"--gamma={args.gamma:g}",
        "--no-mesh", f"--min-screen-percentage={args.min_screen_percentage:g}",
        f"--max-unknown-screen-range={args.max_unknown_screen_range}",
        "--no-auto-color-model", "--no-auto-levels",
    ] + algorithm_arguments(algorithm) + args.extra_arg

    start = time.monotonic()
    peak_rss_kb: int | None = None
    timed_out = False
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        proc = subprocess.Popen(command, stdout=stdout, stderr=stderr)
        while proc.poll() is None:
            rss = read_rss_kb(proc.pid)
            if rss is not None:
                peak_rss_kb = max(peak_rss_kb or 0, rss)
            if args.timeout and time.monotonic() - start > args.timeout:
                timed_out = True
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
                break
            time.sleep(0.1)
    wall_ms = (time.monotonic() - start) * 1000.0

    row = {key: "" for key in CSV_FIELDS}
    row.update({
        "input": str(image),
        "mode": mode,
        "algorithm": algorithm,
        "repeat": repeat,
        "sharpen_radius": radius,
        "sharpen_amount": amount,
        "exit_code": proc.returncode,
        "timed_out": int(timed_out),
        "wall_ms": f"{wall_ms:.3f}",
        "peak_rss_kb": peak_rss_kb if peak_rss_kb is not None else "",
        "report": str(report),
        "output_par": str(output_par),
    })
    row.update(parse_report(report))
    writer.writerow(row)
    args.csv_file.flush()
    print(
        f"{tag}: exit={proc.returncode} result={row['result'] or '-'} "
        f"coverage={row['screen_coverage_pct'] or '-'}% "
        f"seeds={row['seed_pixels'] or '-'} detector_ms={row['total_ms'] or '-'}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark regular-screen geometry detection on external scans."
    )
    parser.add_argument("images", nargs="+", type=Path)
    parser.add_argument(
        "--colorscreen", type=Path,
        default=Path("build-qt/src/colorscreen/colorscreen"),
        help="path to the colorscreen command",
    )
    parser.add_argument("--output-dir", type=Path, default=Path("scr-detect-benchmark"))
    parser.add_argument("--csv", type=Path, default=None, help="summary CSV path")
    parser.add_argument(
        "--mode", action="append", choices=("legacy23", "zero"),
        help="detector sharpening mode; default: run legacy23 and zero",
    )
    parser.add_argument(
        "--algorithm", action="append", choices=("both", "fast", "slow"),
        help="flood-fill mode; default: both",
    )
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--screen-type", default="Dufay")
    parser.add_argument("--scanner-type", default="fixed-lens")
    parser.add_argument("--gamma", type=float, default=1.0)
    parser.add_argument("--min-screen-percentage", type=float, default=90.0)
    parser.add_argument("--max-unknown-screen-range", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=0.0,
                        help="per-run timeout in seconds; 0 disables timeout")
    parser.add_argument("--extra-arg", action="append", default=[],
                        help="additional colorscreen autodetect argument")
    args = parser.parse_args()

    if args.repeat < 1:
        parser.error("--repeat must be positive")
    if not args.colorscreen.is_file():
        parser.error(f"colorscreen executable not found: {args.colorscreen}")
    missing = [str(image) for image in args.images if not image.is_file()]
    if missing:
        parser.error("input file not found: " + ", ".join(missing))

    modes = args.mode or ["legacy23", "zero"]
    algorithms = args.algorithm or ["both"]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = args.csv or args.output_dir / "results.csv"
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    new_csv = not csv_path.exists() or csv_path.stat().st_size == 0
    with csv_path.open("a", newline="") as csv_file:
        args.csv_file = csv_file
        writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
        if new_csv:
            writer.writeheader()
        for image in args.images:
            for mode in modes:
                for algorithm in algorithms:
                    for repeat in range(1, args.repeat + 1):
                        run_one(args, image, mode, algorithm, repeat, writer)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
