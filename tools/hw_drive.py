#!/usr/bin/env python3
"""Drive a TamaPoke board over USB serial and grab real screenshots.

  python3 tools/hw_drive.py PORT OUTDIR CMD...

Each CMD is a console line sent to the board (TAP 233 60, PERF, HEALTH, ...),
or shot:<name> to save the screen as OUTDIR/<name>.png, or wait:<seconds>.
Needs pyserial and Pillow.
"""
import sys, time
import serial
from PIL import Image

def main():
    port, out = sys.argv[1], sys.argv[2]
    p = serial.Serial(port, 115200, timeout=0.3)
    t0 = time.time()
    time.sleep(0.4); p.read(1 << 20)

    def cmd(c):
        p.write((c + "\n").encode()); buf = b''; t = time.time()
        while time.time() - t < 5:
            buf += p.read(4096)
            if b'DONE' in buf: break
        return buf.decode(errors='replace').replace('\r', '').strip()

    def grab():
        p.read(1 << 16); p.write(b"SHOT\n"); buf = b''; idle = time.time()
        while time.time() - idle < 1.0:
            d = p.read(65536)
            if d: buf += d; idle = time.time()
        i = buf.index(b'SHOT '); j = buf.index(b'\n', i)
        w, h, n = map(int, buf[i + 5:j].split())
        data = buf[j + 1:]; k = data.rfind(b'DONE')
        if k >= 0: data = data[:k].rstrip(b'\r\n')
        return w, h, n, data

    def shot(name):
        for _ in range(4):           # USB CDC can still drop a few bytes; retry
            w, h, n, data = grab()
            if len(data) >= n: break
        short = max(0, n - len(data))
        img = Image.frombytes('RGB', (w, h), data[:n] + b'\0' * short, 'raw', 'BGR;16')
        img.save(f"{out}/{name}.png")
        print("shot", name, f"({short} bytes short)" if short else "")

    for c in sys.argv[3:]:
        if c.startswith('shot:'): shot(c[5:])
        elif c.startswith('wait:'): time.sleep(float(c[5:]))
        else: print(round(time.time() - t0, 1), c, '->', cmd(c))

if __name__ == '__main__':
    main()
