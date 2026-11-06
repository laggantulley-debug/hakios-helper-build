import struct, sys
for p in sys.argv[1:]:
    d = open(p, 'rb').read()
    m, ct, cs, ft, nc, sc, fl, _ = struct.unpack_from('<IiiIIIII', d, 0)
    kind = {2: 'MH_EXECUTE', 6: 'MH_DYLIB', 8: 'MH_BUNDLE'}.get(ft, '?')
    print('%s: %d bytes, filetype %d (%s), ncmds %d' % (p, len(d), ft, kind, nc))
    off = 32
    for _ in range(nc):
        c, s = struct.unpack_from('<II', d, off)
        print('   cmd %#x size %d' % (c, s))
        off += s
