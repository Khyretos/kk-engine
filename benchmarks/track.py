#!/usr/bin/env python3
"""Keeps the benchmark history (docs/BENCHMARKS.md "Tracked over time").

    track.py --history DIR --commit SHA --date ISO [--ref main] REPORT.json...

Reads kke_bench (`suite_*.json`) and stress-test (`stress_*.json`) reports,
appends one line to DIR/history.jsonl, compares every number with the
median of the previous runs, and rewrites DIR/README.md (a table and one
SVG trend chart per number in DIR/charts/). Prints the comparison as
Markdown for the CI job summary.

Only the Python standard library, so it runs anywhere CI or a contributor
has python3. Pass --dry-run to compare without writing anything.
"""

import argparse
import json
import os
import statistics
import sys

# Slower than the recent median by more than this = flagged in the summary.
# Shared CI runners wobble by 10-20% between runs, so smaller changes are
# noise there; the chart shows whether a flag was a blip or a step.
REGRESSION = 0.25
BASELINE_RUNS = 10   # how many previous runs the median is taken over
CHART_RUNS = 60      # points per chart

# Which numbers are tracked, and whether higher is better. Everything a
# report has that's not listed here is ignored (p95s stay in the reports).
STRESS_KEYS = {
    "fps_avg": True,
    "fps_1pct_low": True,
    "frame_p99_ms": False,
    "peak_rss_mb": False,
    "walk.fps_avg": True,
    "crates.fps_avg": True,
    "impacts.fps_avg": True,
    "crates.physics_avg_ms": False,
}


def load_numbers(paths):
    """{metric: value}, plus a system description from the first report."""
    numbers, system = {}, {}
    for path in paths:
        with open(path, encoding="utf-8") as f:
            report = json.load(f)
        kind = report.get("benchmark", "")
        results = report.get("results", {})
        # The suite has no GPU; the stress test does. Take each field
        # from whichever report knows it.
        for key, value in report.get("system", {}).items():
            if system.get(key, "unknown") == "unknown":
                system[key] = value
        if kind == "suite":
            for key, value in results.items():
                if key.endswith("_ms") and not key.endswith("_p95_ms") and value is not None:
                    numbers["bench." + key] = float(value)
        elif kind == "stress":
            for key in STRESS_KEYS:
                if results.get(key) is not None:
                    numbers["stress." + key] = float(results[key])
    return numbers, system


def higher_is_better(metric):
    kind, key = metric.split(".", 1)
    return kind == "stress" and STRESS_KEYS.get(key, False)


def fmt(v):
    if v >= 100:
        return f"{v:.0f}"
    if v >= 1:
        return f"{v:.2f}"
    return f"{v:.4f}"


def compare(numbers, history):
    """Rows of (metric, value, baseline, change, flag)."""
    rows = []
    for metric in sorted(numbers):
        previous = [h["numbers"][metric] for h in history if metric in h["numbers"]][-BASELINE_RUNS:]
        value = numbers[metric]
        if not previous:
            rows.append((metric, value, None, None, "new"))
            continue
        base = statistics.median(previous)
        change = (value - base) / base if base else 0.0
        worse = -change if higher_is_better(metric) else change
        flag = "slower" if worse > REGRESSION else ("faster" if worse < -REGRESSION else "")
        rows.append((metric, value, base, change, flag))
    return rows


def summary_markdown(rows, commit, runs):
    out = [f"### Benchmarks for `{commit[:10]}`", ""]
    if runs == 0:
        out.append("First tracked run: nothing to compare with yet.")
        out += ["", "| Metric | This run |", "|---|---:|"] + [f"| `{m}` | {fmt(v)} |" for m, v, *_ in rows]
        return "\n".join(out) + "\n"
    out.append(f"Compared with the median of the last {min(runs, BASELINE_RUNS)} tracked run(s) on main. "
               f"Changes beyond ±{int(REGRESSION * 100)}% are marked; shared runners are noisy, so check the trend chart before acting on one.")
    out += ["", "| Metric | This run | Recent median | Change | |", "|---|---:|---:|---:|---|"]
    for metric, value, base, change, flag in rows:
        mark = {"slower": "⚠️ worse", "faster": "✅ better", "new": "new"}.get(flag, "")
        out.append(f"| `{metric}` | {fmt(value)} | {fmt(base) if base is not None else '–'} | "
                   f"{f'{change:+.1%}' if change is not None else '–'} | {mark} |")
    return "\n".join(out) + "\n"


def chart_svg(metric, points):
    """A small line chart; points = [(label, value)], oldest first."""
    w, h, pad = 640, 180, 36
    values = [v for _, v in points]
    # Zero-based, so a 3% wobble doesn't look like a cliff.
    lo, hi = min(0.0, min(values)), max(values) * 1.15
    if hi - lo < 1e-12:
        hi = lo + 1.0
    n = len(points)

    def xy(i, v):
        x = pad + (w - 2 * pad) * (i / (n - 1) if n > 1 else 0.5)
        y = h - pad - (h - 2 * pad) * (v - lo) / (hi - lo)
        return x, y

    path = " ".join(f"{'M' if i == 0 else 'L'}{x:.1f},{y:.1f}" for i, (x, y) in enumerate(xy(i, v) for i, (_, v) in enumerate(points)))
    dots = "".join(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="2.5"><title>{label}: {fmt(v)}</title></circle>'
                   for (label, v), (x, y) in zip(points, (xy(i, v) for i, (_, v) in enumerate(points))))
    better = "higher is better" if higher_is_better(metric) else "lower is better"
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" font-family="sans-serif" font-size="11">
<rect width="{w}" height="{h}" fill="#ffffff"/>
<text x="{pad}" y="16" font-size="13" font-weight="bold" fill="#222">{metric}</text>
<text x="{w - pad}" y="16" text-anchor="end" fill="#666">{better}</text>
<line x1="{pad}" y1="{h - pad}" x2="{w - pad}" y2="{h - pad}" stroke="#ccc"/>
<text x="{pad - 4}" y="{xy(0, hi)[1] + 4:.1f}" text-anchor="end" fill="#666">{fmt(hi)}</text>
<text x="{pad - 4}" y="{xy(0, lo)[1]:.1f}" text-anchor="end" fill="#666">{fmt(lo)}</text>
<text x="{pad}" y="{h - pad + 16}" fill="#666">{points[0][0]}</text>
<text x="{w - pad}" y="{h - pad + 16}" text-anchor="end" fill="#666">{points[-1][0]}</text>
<path d="{path}" fill="none" stroke="#2563eb" stroke-width="2"/>
<g fill="#2563eb">{dots}</g>
</svg>
"""


def write_readme(directory, history):
    os.makedirs(os.path.join(directory, "charts"), exist_ok=True)
    latest = history[-1]
    metrics = sorted({m for h in history for m in h["numbers"]})
    lines = [
        "# KKE benchmark history",
        "",
        "Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; "
        "don't edit by hand. What the numbers mean: "
        "[docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).",
        "",
        f"Latest: `{latest['commit'][:10]}` ({latest['date']}) on {latest['system'].get('cpu', 'unknown CPU')}, "
        f"{latest['system'].get('gpu', 'no GPU reported')}. {len(history)} run(s) tracked.",
        "",
        "Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; "
        "look for steps that stay.",
        "",
        "| Metric | Latest | Best | Runs |",
        "|---|---:|---:|---:|",
    ]
    for m in metrics:
        vals = [h["numbers"][m] for h in history if m in h["numbers"]]
        best = max(vals) if higher_is_better(m) else min(vals)
        now = latest["numbers"].get(m)
        lines.append(f"| `{m}` | {fmt(now) if now is not None else '–'} | {fmt(best)} | {len(vals)} |")
    lines.append("")
    for m in metrics:
        pts = [(h["commit"][:7], h["numbers"][m]) for h in history if m in h["numbers"]][-CHART_RUNS:]
        name = m.replace(".", "_") + ".svg"
        with open(os.path.join(directory, "charts", name), "w", encoding="utf-8") as f:
            f.write(chart_svg(m, pts))
        lines.append(f"![{m}](charts/{name})")
        lines.append("")
    with open(os.path.join(directory, "README.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--history", required=True, help="directory holding history.jsonl (created if missing)")
    ap.add_argument("--commit", required=True)
    ap.add_argument("--date", required=True)
    ap.add_argument("--ref", default="main")
    ap.add_argument("--dry-run", action="store_true", help="compare only; write nothing")
    ap.add_argument("reports", nargs="+")
    args = ap.parse_args()

    numbers, system = load_numbers(args.reports)
    if not numbers:
        print("track.py: no benchmark numbers in the given reports", file=sys.stderr)
        return 1

    path = os.path.join(args.history, "history.jsonl")
    history = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            history = [json.loads(line) for line in f if line.strip()]

    print(summary_markdown(compare(numbers, history), args.commit, len(history)))
    if args.dry_run:
        return 0

    keep_system = {k: system.get(k, "unknown") for k in ("os", "cpu", "cpu_logical_cores", "ram_mb", "gpu", "gpu_type", "build_type", "compiler")}
    history.append({"commit": args.commit, "date": args.date, "ref": args.ref, "system": keep_system, "numbers": numbers})
    os.makedirs(args.history, exist_ok=True)
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps(history[-1], sort_keys=True) + "\n")
    write_readme(args.history, history)
    return 0


if __name__ == "__main__":
    sys.exit(main())
