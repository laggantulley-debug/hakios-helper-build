#!/usr/bin/env python3
"""fbproc.py —— 从正在运行的 QEMU 进程内存里直接读客人机帧缓冲（不需要 share=on）。

用法: fbproc.py <fb_base_hex> <width> <height> <out.png> [ram.img 名]

做法: 在 /proc/<qemu-pid>/maps 里找到 ram.img 的映射，算出帧缓冲在 guest 物理地址里的
      文件偏移，再用 /proc/<pid>/mem 按虚拟地址读出最新（脏）页。root 可读。
"""
import os
import struct
import sys
import zlib

base = int(sys.argv[1], 16)
w = int(sys.argv[2])
h = int(sys.argv[3])
out = sys.argv[4]
needle = sys.argv[5] if len(sys.argv) > 5 else "ram-ios.img"
GUEST_RAM_BASE = 0x40000000
target = base - GUEST_RAM_BASE

pid = None
for d in os.listdir("/proc"):
    if not d.isdigit():
        continue
    try:
        exe = os.readlink("/proc/%s/exe" % d)
    except OSError:
        continue
    if "qemu-system" in exe:
        pid = d
        break
if pid is None:
    print("没找到 QEMU 进程")
    sys.exit(1)

segs = []
with open("/proc/%s/maps" % pid) as fh:
    for line in fh:
        if needle not in line:
            continue
        parts = line.split()
        lo, hi = [int(x, 16) for x in parts[0].split("-")]
        foff = int(parts[2], 16)
        segs.append((lo, hi, foff))
if not segs:
    print("maps 里没有 %s 的映射" % needle)
    sys.exit(2)

size = w * 4 * h
got = bytearray()
mem = open("/proc/%s/mem" % pid, "rb", buffering=0)
pos, remain = target, size
while remain > 0:
    seg = None
    for lo, hi, foff in segs:
        if foff <= pos < foff + (hi - lo):
            seg = (lo, hi, foff)
            break
    if seg is None:
        print("偏移 %#x 不在任何映射里" % pos)
        sys.exit(3)
    lo, hi, foff = seg
    chunk = min(remain, foff + (hi - lo) - pos)
    vaddr = lo + (pos - foff)
    mem.seek(vaddr)
    data = mem.read(chunk)
    if len(data) != chunk:
        print("只读到 %d/%d" % (len(data), chunk))
        sys.exit(4)
    got += data
    pos += chunk
    remain -= chunk
mem.close()
print("读到 %d 字节（pid %s）" % (len(got), pid))

raw = bytearray()
nb = 0
stride = w * 4
for y in range(h):
    row = got[y * stride:(y + 1) * stride]
    for x in range(w):
        b, g, r = row[x * 4], row[x * 4 + 1], row[x * 4 + 2]
        if r or g or b:
            nb += 1
        raw += bytes((r, g, b))


def chunk(tag, payload):
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)


png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
buf = bytearray()
for y in range(h):
    buf.append(0)
    buf += raw[y * w * 3:(y + 1) * w * 3]
png += chunk(b"IDAT", zlib.compress(bytes(buf), 6)) + chunk(b"IEND", b"")
with open(out, "wb") as fh:
    fh.write(png)
print("已写 %s；非黑像素 %d (%.4f%%)" % (out, nb, 100.0 * nb / (w * h)))
