#!/usr/bin/env python3
import sys


def parse_coverage(info_path):
    lh = 0
    lf = 0
    da_total = 0
    da_hit = 0
    try:
        with open(info_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line.startswith("LH:"):
                    lh += int(line[3:])
                elif line.startswith("LF:"):
                    lf += int(line[3:])
                elif line.startswith("DA:"):
                    da_total += 1
                    try:
                        count = int(line.split(",", 1)[1])
                    except (IndexError, ValueError):
                        count = 0
                    if count > 0:
                        da_hit += 1
    except FileNotFoundError:
        print("[COVERAGE] missing coverage info:", info_path)
        return None
    if lf == 0 and da_total:
        lf = da_total
        lh = da_hit
    return lh, lf


def main():
    if len(sys.argv) != 3:
        print("usage: coverage_check.py <coverage.info> <min_pct>")
        return 2

    info_path = sys.argv[1]
    try:
        min_pct = float(sys.argv[2])
    except ValueError:
        print("[COVERAGE] invalid min_pct:", sys.argv[2])
        return 2

    result = parse_coverage(info_path)
    if result is None:
        return 1

    lh, lf = result
    if lf == 0:
        print("[COVERAGE] no line coverage data found")
        return 1

    pct = (lh * 100.0) / lf
    print("[COVERAGE] line coverage %.2f%% (%d/%d)" % (pct, lh, lf))
    if pct < min_pct:
        print("[COVERAGE] below threshold %.2f%%" % min_pct)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
