#!/usr/bin/env python3
"""Log and summarise the sensing radio's output.

    python tools/sense_log.py COM20                      # print lines, log to sense_<time>.csv
    python tools/sense_log.py COM20 --secs 60 --tag rest # stop after 60 s, name the run
    python tools/sense_log.py COM20 --cmd "park 36"      # send a command first

Reads the ASCII lines the sensing radio prints (include/sense_proto.h): the
$S status lines go to a CSV, '#' notes and $I info lines go to the console.
At the end (or on Ctrl-C) it prints what a bench test wants to know: CSI
samples a second (min / median / max), the motion score's distribution,
how long each state was shown, and how often MOTION fired, so a run with
the room still and a run with a person walking can be set side by side.

Bench use: `--secs` and `--tag` name a run; the CSV lands in --out (default:
the current folder) so nothing is written where it should not be.
"""
import argparse, csv, os, statistics, sys, time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")

STATES = {0: "CAL", 1: "LOWDATA", 2: "STILL", 3: "POSSIBLE", 4: "MOTION"}
FIELDS = ["t", "up_ms", "score", "state", "rate", "chan", "band", "rssi", "rel", "cal",
          "targets", "mode", "amp", "phase", "corr", "live", "drop", "pres"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--secs", type=float, default=0, help="stop after this many seconds (0 = until Ctrl-C)")
    ap.add_argument("--tag", default="run")
    ap.add_argument("--out", default=".")
    ap.add_argument("--cmd", action="append", default=[], help="a command to send first (repeatable)")
    ap.add_argument("--quiet", action="store_true", help="no per-line echo of $S")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    name = os.path.join(a.out, "sense_%s_%s.csv" % (a.tag, time.strftime("%Y%m%d_%H%M%S")))
    rows = []
    notes = []
    with serial.Serial(a.port, a.baud, timeout=0.5) as s, open(name, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(FIELDS)
        time.sleep(0.3)
        for c in a.cmd:
            s.write((c + "\n").encode())
            print(">", c)
            time.sleep(0.2)
        t0 = time.time()
        try:
            while True:
                if a.secs and time.time() - t0 >= a.secs:
                    break
                line = s.readline().decode("utf-8", "replace").strip()
                if not line:
                    continue
                now = time.time() - t0
                if line.startswith("$S,"):
                    parts = line[3:].split(",")
                    if len(parts) not in (16, 17):
                        continue
                    try:
                        v = [int(x) for x in parts] + ([0] if len(parts) == 16 else [])
                    except ValueError:
                        continue
                    rows.append([round(now, 2)] + v)
                    w.writerow([round(now, 2)] + v)
                    if not a.quiet:
                        print("%7.1fs score %3d %-8s rate %3d/s ch %3d (%dG) rssi %4d rel %3d cal %3d%% tgts %d live %4d drop %d"
                              % (now, v[1], STATES.get(v[2], "?"), v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[14], v[15]))
                else:
                    notes.append((round(now, 1), line))
                    print("%7.1fs %s" % (now, line))
        except KeyboardInterrupt:
            pass
    print("\n== %s: %d status lines over %.0f s, saved to %s" % (a.tag, len(rows), rows[-1][0] if rows else 0, name))
    if not rows:
        return
    rates = [r[4] for r in rows]
    scores = [r[2] for r in rows if r[3] >= 2]          # once past calibration
    live = [r[15] for r in rows if r[3] >= 2]
    print("CSI rate/s     min %d  median %d  max %d" % (min(rates), statistics.median(rates), max(rates)))
    if scores:
        print("score (calibrated) min %d  median %d  p90 %d  max %d" %
              (min(scores), statistics.median(scores), sorted(scores)[int(0.9 * (len(scores) - 1))], max(scores)))
        print("live (calibrated)  median %d  p90 %d  max %d" %
              (statistics.median(live), sorted(live)[int(0.9 * (len(live) - 1))], max(live)))
    total = len(rows)
    for k, nm in STATES.items():
        n = sum(1 for r in rows if r[3] == k)
        if n:
            print("state %-8s %5.1f%% of the run" % (nm, 100.0 * n / total))
    # MOTION episodes: runs of consecutive MOTION lines.
    episodes, inside = 0, False
    for r in rows:
        if r[3] == 4 and not inside:
            episodes += 1
        inside = r[3] == 4
    print("MOTION episodes  %d" % episodes)
    # PRESENCE (field 17): share of the calibrated run, and how many times it came on.
    cal_rows = [r for r in rows if r[3] >= 2]
    if cal_rows:
        on = sum(1 for r in cal_rows if r[17])
        starts, prev = 0, 0
        for r in cal_rows:
            if r[17] and not prev:
                starts += 1
            prev = r[17]
        print("PRESENCE         %.1f%% of the calibrated run, came on %d time(s)" % (100.0 * on / len(cal_rows), starts))
    bands = sorted(set(r[6] for r in rows))
    print("band(s) seen     %s  channel(s) %s" % (bands, sorted(set(r[5] for r in rows))))


if __name__ == "__main__":
    main()
