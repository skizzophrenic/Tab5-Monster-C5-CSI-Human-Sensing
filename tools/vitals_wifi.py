#!/usr/bin/env python3
"""Give the sensing radio (the Monster C5) the WiFi network VITALS joins.

VITALS wants the radio joined to your network, pinging the router for a
steady stream of frames (heart rate needs it). The Tab5 has no WiFi of its
own to lend yet, so the radio keeps this one network itself, apart from its
normal settings, and only uses it while the VITALS screen is open.

Run it with sensing OFF on the Tab5 (so the PC can reach the radio):

    python tools/vitals_wifi.py COM12
    python tools/vitals_wifi.py COM12 --forget

The password is typed here, without echo, and goes straight to the radio
over its USB cable. Nothing is written to disk on the PC.
"""
import argparse
import getpass
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('port', nargs='?', default='COM12')
    ap.add_argument('--forget', action='store_true', help='clear the saved VITALS network')
    a = ap.parse_args()
    if a.forget:
        cmd = 'vitalsnet forget'
        want = 'vitals network forgotten'
    else:
        ssid = input('WiFi network name: ').strip()
        if not ssid:
            print('no name, nothing sent'); return 1
        pw = getpass.getpass('Password (not shown): ')
        cmd = 'vitalsnet %s\t%s' % (ssid, pw)
        want = 'vitals network saved'
    s = serial.Serial(a.port, 115200, timeout=0.2)
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write(b'\r\n' + cmd.encode('utf-8') + b'\r\n')
    cmd = pw = None
    t0 = time.time(); buf = b''
    while time.time() - t0 < 4:
        buf += s.read(4096)
        for line in buf.decode('utf-8', 'replace').splitlines():
            if want in line:
                print('radio:', line.lstrip('# ').strip())
                print('Done. Turn SENSING back on and open VITALS.')
                return 0
    print('The radio did not answer. Is sensing OFF on the Tab5, and is it the right port?')
    return 1


if __name__ == '__main__':
    sys.exit(main())
