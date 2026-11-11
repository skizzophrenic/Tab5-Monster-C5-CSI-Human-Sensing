#!/usr/bin/env python3
"""Record the sensing radio's chest signal for a VITALS bench test, then say
what it found.

The Monster C5's own USB port (its CH340) hears everything the radio sends on
the M5-Bus, so this listens there while the Tab5 runs VITALS. It only reads:
nothing is sent, so the Tab5 stays in charge of the radio.

    python tools/vitals_log.py COM12 --seconds 120 --label still
    python tools/vitals_log.py --analyse logs/vitals_still_....csv

Writes logs/vitals_<label>_<time>.csv (t, kind, the line's fields) and prints:
frames a second, gaps, the radio's mode, and from the chest signal the
breathing rate two ways (spectrum peak, and breaths counted) plus the heart
band's strongest peak. Compare the breaths counted with your own count.
"""
import argparse
import csv
import os
import sys
import time

try:
    import numpy as np
except ImportError:
    np = None

FS = 20.0


def record(port, seconds, label, outdir):
    import serial
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, 'vitals_%s_%s.csv' % (label, time.strftime('%Y%m%d_%H%M%S')))
    s = serial.Serial(port, 115200, timeout=0.2)
    t0 = time.time(); buf = b''; nv = ns = 0
    with open(path, 'w', newline='') as f:
        w = csv.writer(f); w.writerow(['t', 'kind', 'f1', 'f2', 'f3', 'f4', 'f5', 'f6', 'f7', 'f8', 'f9', 'f10', 'f11', 'f12', 'f13', 'f14', 'f15', 'f16', 'f17'])
        last = 0
        while time.time() - t0 < seconds:
            buf += s.read(4096)
            *lines, buf = buf.split(b'\n')
            for raw in lines:
                line = raw.decode('ascii', 'replace').strip()
                t = round(time.time() - t0, 3)
                if line.startswith('$V,'):
                    w.writerow([t, 'V'] + line[3:].split(',')); nv += 1
                elif line.startswith('$S,'):
                    w.writerow([t, 'S'] + line[3:].split(',')); ns += 1
                elif line.startswith('#'):
                    w.writerow([t, 'note', line[1:].strip()])
                    print('  radio:', line[1:].strip())
            if time.time() - t0 - last >= 10:
                last = time.time() - t0
                print('  %3.0f s: %d chest frames (%.1f/s), %d status lines' % (last, nv, nv / last, ns))
    print('wrote', path)
    return path


def analyse(path):
    v, st = [], []
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            try:   # a line can arrive torn (two run together); skip those
                if r['kind'] == 'V':
                    v.append((int(r['f1']) / 1000.0, int(r['f2']) / 1e4, int(r['f3']) / 1e4, int(r['f4']), int(r['f5'])))
                elif r['kind'] == 'S':
                    st.append((float(r['t']), int(r['f3']), int(r['f11'])))   # t, state, mode
            except (ValueError, TypeError, KeyError):
                pass
    if not v:
        print('no chest frames: was VITALS on, and the Monster on the bus?'); return
    dur = v[-1][0] - v[0][0]
    gaps = sum(1 for x in v if x[3] == 0)
    modes = {}
    for x in st: modes[x[2]] = modes.get(x[2], 0) + 1
    moving = sum(1 for x in st if x[1] == 4)
    print('%d chest frames over %.0f s (%.1f/s), %.0f%% empty frames, q median %d' % (len(v), dur, len(v) / max(dur, 1), 100.0 * gaps / len(v), sorted(x[4] for x in v)[len(v) // 2]))
    print('radio mode (1 park, 2 joined, 3 joining):', modes, ' MOTION lines: %d of %d' % (moving, len(st)))
    if np is None:
        print('(numpy not found: no rates)'); return
    c1 = np.array([x[1] for x in v])
    # detrend over ~6 s, then the breathing band
    k = int(FS * 6)
    slow = np.convolve(c1, np.ones(k) / k, mode='same')
    d = c1 - slow
    n = 1 << int(np.floor(np.log2(len(d))))
    seg = d[-n:] * np.hanning(n)
    mag = np.abs(np.fft.rfft(seg)); f = np.fft.rfftfreq(n, 1 / FS)
    floor = np.median(mag[(f > 2.5) & (f < 7.8)]) + 1e-12
    br = (f >= 0.1) & (f <= 0.62)
    bi = np.argmax(mag * br)
    print('breathing peak: %.3f Hz = %.1f a minute, %.0f dB over the noise floor' % (f[bi], f[bi] * 60, 20 * np.log10(mag[bi] / floor)))
    # breaths counted: peaks of a 0.7 Hz low-passed wave with hysteresis
    a = np.exp(-2 * np.pi * 0.7 / FS)
    lp = np.zeros_like(d); acc = 0.0
    for i, x in enumerate(d):
        acc = a * acc + (1 - a) * x; lp[i] = acc
    lp2 = np.zeros_like(lp); acc = 0.0
    for i, x in enumerate(lp):
        acc = a * acc + (1 - a) * x; lp2[i] = acc
    hy = 0.5 * np.std(lp2)
    peaks, direction, ext = [], 1, lp2[0]
    for i, x in enumerate(lp2):
        if direction == 1:
            if x > ext: ext = x
            elif x < ext - hy: peaks.append(i); direction, ext = -1, x
        else:
            if x < ext: ext = x
            elif x > ext + hy: direction, ext = 1, x
    print('breaths counted: %d in %.0f s = %.1f a minute' % (len(peaks), dur, len(peaks) / max(dur, 1) * 60))
    hb = (f >= 0.82) & (f <= 2.19)
    excl = np.zeros_like(hb)
    for h in range(2, 10): excl |= np.abs(f - h * f[bi]) <= 1.3 * (f[1] - f[0])
    band = hb & ~excl
    if band.any():
        hi = np.argmax(mag * band)
        prom = mag[hi] / (np.median(mag[band]) + 1e-12)
        print('heart band peak: %.3f Hz = %.0f a minute, %.1fx the band median (a lock wants 3x and up)' % (f[hi], f[hi] * 60, prom))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('port', nargs='?', default='COM12')
    ap.add_argument('--seconds', type=float, default=120)
    ap.add_argument('--label', default='test')
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'logs'))
    ap.add_argument('--analyse', metavar='CSV')
    a = ap.parse_args()
    if a.analyse:
        analyse(a.analyse); return
    analyse(record(a.port, a.seconds, a.label, os.path.abspath(a.out)))


if __name__ == '__main__':
    sys.exit(main())
