#!/usr/bin/env python3
"""Prints the file ID of every file in the 'waves' folder of an NDS rom (and of a few other folders for comparison).

  python3 nds_fileid.py test.nds

The streamed music code needs the ID of the FIRST file in 'waves' (src/sound.c: firstWavID = 533;).  This prints it.
Reads only; changes nothing.
"""
import struct, sys

def main(path):
    rom = open(path, "rb").read()
    fnt_off, fnt_size, fat_off, fat_size = struct.unpack_from("<IIII", rom, 0x40)
    def dir_entry(did):
        o = fnt_off + (did & 0xFFF) * 8
        return struct.unpack_from("<IHH", rom, o)          # subtable offset, first file id, parent
    out = []
    def walk(did, prefix):
        sub, fid, parent = dir_entry(did)
        p = fnt_off + sub
        while True:
            b = rom[p]; p += 1
            if b == 0: break
            n = b & 0x7F
            name = rom[p:p + n].decode("latin-1"); p += n
            if b & 0x80:
                child = struct.unpack_from("<H", rom, p)[0]; p += 2
                walk(child, prefix + name + "/")
            else:
                out.append((fid, prefix + name)); fid += 1
    walk(0xF000, "")
    waves = sorted((i, n) for i, n in out if n.startswith("waves/"))
    if not waves:
        print("no 'waves/' folder in this rom: the .nwav files did not get into the build")
        print("(check that base_extra/root/waves/ has your files, then run make clean && make -j8 again)")
        print("folders found at the top level:", sorted({n.split('/')[0] for _, n in out if '/' in n}))
        return
    print("files in waves/:")
    for i, n in waves:
        a = fat_off + i * 8
        s, e = struct.unpack_from("<II", rom, a)
        print("  id %5d  %-40s %d bytes" % (i, n, e - s))
    print("\nrom file length: %d bytes (0x%X), FAT has %d entries" % (len(rom), len(rom), fat_size // 8))
    first = waves[0][0]
    for i in range(first - 2, first + 2):
        if 0 <= i < fat_size // 8:
            s2, e2 = struct.unpack_from("<II", rom, fat_off + i * 8)
            label = dict((k, v) for k, v in out).get(i, "?")
            print("  FAT[%d] %-34s start 0x%08X end 0x%08X size %d" % (i, label, s2, e2, e2 - s2))
    names = {n: i for i, n in out}
    print("\nsanity check (the parser itself): %d files in the rom" % len(out))
    for n in ("a/0/0/8", "a/0/0/2"):
        if n in names:
            s, e = struct.unpack_from("<II", rom, fat_off + names[n] * 8)
            print("  %s  id %d  %d bytes" % (n, names[n], e - s))
    print("\nfirst id in waves/ =", waves[0][0], "-> src/sound.c needs  firstWavID = %d;" % waves[0][0])

main(sys.argv[1] if len(sys.argv) > 1 else "test.nds")
