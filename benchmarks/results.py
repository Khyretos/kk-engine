#!/usr/bin/env python3
"""Reads kke_benchmark results files (kke-benchmark-*.json) and compares them.

    benchmarks/results.py FILE.json                 one machine: every demo, problems, hitches
    benchmarks/results.py OLD.json NEW.json ...     side by side, change vs the first file
    benchmarks/results.py --hitches FILE.json       every hitch with its cause and nearby log lines

Several files from one machine (two engine versions) show performance
gains or losses; files from different machines show which machines
struggle where. Only needs Python 3. docs/BENCHMARKS.md explains the keys.
"""

import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if data.get("format", "").split("/")[0] != "kke-benchmark":
        sys.exit(f"{path}: not a kke_benchmark results file")
    return data


def label(data):
    s = data.get("system", {})
    return f"{s.get('engine_version', '?')}@{s.get('engine_commit', '?')[:7]} {s.get('gpu', '?')}"


def summary(demo):
    return (demo.get("report") or {}).get("summary") or {}


def fmt(v, digits=1):
    return "-" if v is None else f"{v:.{digits}f}"


def machine(data):
    s = data.get("system", {})
    print(f"{data.get('started', '?')}  engine {s.get('engine_version', '?')} ({s.get('engine_commit', '?')}), {s.get('build_type', '?')}")
    print(f"  {s.get('os_version', s.get('os', '?'))}")
    print(f"  {s.get('cpu', '?')}, {s.get('cpu_logical_cores', '?')} threads, {s.get('ram_mb', '?')} MB RAM")
    print(f"  {s.get('gpu', '?')} ({s.get('gpu_type', '?')}), {s.get('gpu_driver', s.get('gpu_driver_version', '?'))}, display {s.get('display', '?')}")
    if "problem" in data.get("vulkan", {}):
        print(f"  VULKAN PROBLEM: {data['vulkan']['problem']}")


def one(data):
    machine(data)
    print()
    print(f"{'demo':<18}{'status':<16}{'fps':>7}{'1% low':>8}{'p99 ms':>8}{'gpu ms':>8}{'hitches':>8}{'worst ms':>9}  slowest stage / module")
    for d in data["demos"]:
        s = summary(d)
        status = d["status"] if d["status"] != "ok" else s.get("verdict", "ok")
        report = d.get("report") or {}
        stages = report.get("stages_ms") or {}
        busiest = max(stages.items(), key=lambda kv: (kv[1] or {}).get("avg", 0), default=(None, None))
        modules = report.get("modules") or []
        where = ""
        if busiest[0]:
            where = f"{busiest[0]} {busiest[1]['avg']:.1f} ms"
        if modules:
            where += f" / {modules[0]['name']} {modules[0]['avg_ms_per_frame']:.2f} ms"
        frame = s.get("frame_ms") or {}
        gpu = s.get("gpu_ms") or {}
        hitches = (s.get("hitches") or {}).get("count")
        print(f"{d['id']:<18}{status:<16}{fmt(s.get('fps_avg')):>7}{fmt(s.get('fps_1pct_low')):>8}{fmt(frame.get('p99')):>8}"
              f"{fmt(gpu.get('avg')):>8}{'-' if hitches is None else hitches:>8}{fmt(frame.get('max')):>9}  {where}")
    problems = [d for d in data["demos"] if d["status"] not in ("ok", "missing") or d["log"]["errors"]]
    if problems:
        print("\nProblems")
        for d in problems:
            print(f"  {d['id']}: {d['status']} ({d.get('exit', '')}), {d['log']['errors']} error(s), {d['log']['warnings']} warning(s)")
            for b in (d.get("report") or {}).get("broken_modules", []):
                print(f"    broken: {b}")
            for m in d["log"]["messages"]:
                if m["level"] != "warning":
                    print(f"    {m['level']} x{m['count']}: {m['text'][:160]}")
            for line in d["log"].get("tail", [])[-15:]:
                print(f"    | {line[:160]}")


def compare(files):
    base = files[0][1]
    print("columns: fps avg / 1% low fps / hitches; change vs the first file in brackets\n")
    for i, (path, data) in enumerate(files):
        print(f"[{i}] {path}: {label(data)}")
    ids = []
    for _, data in files:
        for d in data["demos"]:
            if d["id"] not in ids:
                ids.append(d["id"])
    print()
    print(f"{'demo':<18}" + "".join(f"{'[' + str(i) + ']':<30}" for i in range(len(files))))
    for demo_id in ids:
        row = f"{demo_id:<18}"
        ref = next((summary(d) for d in base["demos"] if d["id"] == demo_id), {})
        for _, data in files:
            d = next((d for d in data["demos"] if d["id"] == demo_id), None)
            if d is None:
                row += f"{'(not run)':<30}"
                continue
            s = summary(d)
            if d["status"] != "ok" or not s:
                row += f"{d['status']:<30}"
                continue
            cell = f"{s['fps_avg']:.0f}/{s['fps_1pct_low']:.0f}/{s['hitches']['count']}"
            if ref and ref is not s and ref.get("fps_avg"):
                cell += f" ({(s['fps_avg'] / ref['fps_avg'] - 1) * 100:+.0f}%/{(s['fps_1pct_low'] / max(ref['fps_1pct_low'], 1e-9) - 1) * 100:+.0f}%)"
            row += f"{cell:<30}"
        print(row)


def hitches(data):
    machine(data)
    for d in data["demos"]:
        hs = (d.get("report") or {}).get("hitches") or []
        if not hs:
            continue
        print(f"\n{d['id']}: {len(hs)} hitch(es)")
        for h in hs:
            print(f"  {h['t_s']:7.2f} s  {h['frame_ms']:7.1f} ms (median {h['median_ms']:.1f}) {h['severity']:<6} {h['cause']}")
            top = ", ".join(f"{c['module']} {c['call']} {c['ms']:.1f}" for c in h.get("top_calls", []))
            if top:
                print(f"             top calls: {top}")
            if h.get("rss_delta_mb"):
                print(f"             memory {h['rss_delta_mb']:+.1f} MB")
            for line in h.get("log_near", []):
                print(f"             log: {line[:150]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--hitches", action="store_true", help="list every hitch of each file")
    args = ap.parse_args()
    files = [(p, load(p)) for p in args.files]
    if args.hitches:
        for _, data in files:
            hitches(data)
    elif len(files) == 1:
        one(files[0][1])
    else:
        compare(files)


if __name__ == "__main__":
    main()
