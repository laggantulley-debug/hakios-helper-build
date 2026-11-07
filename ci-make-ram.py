#!/usr/bin/env python3
"""把系统卷放进常驻内存镜像（CI 用）。"""
import os, sys
vol_path = sys.argv[1] if len(sys.argv) > 1 else 'volume.apfs'
vol = os.path.getsize(vol_path)
OFF = 0xbc000000
TOTAL = 10 * 1024**3
print('卷 %d MB (偏移 %#x)' % (vol / 1048576, OFF))
assert OFF + vol < TOTAL, '卷放不进 10G'
with open('ram.img', 'wb') as d:
    d.truncate(TOTAL)
with open(vol_path, 'rb') as s, open('ram.img', 'r+b') as d:
    d.seek(OFF)
    while True:
        b = s.read(8 * 1024 * 1024)
        if not b:
            break
        d.write(b)
print('ram.img 就绪')
