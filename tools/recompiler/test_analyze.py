"""Self-check for analyze.py on a synthetic ROM (no game data needed).

    python tools/recompiler/test_analyze.py
"""
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402


def rom_with(code_at_200):
    rom = bytearray(0x400)
    struct.pack_into('>II', rom, 0, 0x00FFFE00, 0x200)   # SSP, reset -> $200
    rom[0x200:0x200 + len(code_at_200)] = code_at_200
    f = tempfile.NamedTemporaryFile(delete=False, suffix='.bin')
    f.write(rom)
    f.close()
    return f.name


def main():
    # Compiled switch: move.w $208(pc,d0.l),d0 ; jmp $208(pc,d0.w)
    # table of word offsets from $208 -> cases at $20C, $20E (both rts);
    # a jsr $214(pc) after the cases must be followed by descent.
    code = bytes.fromhex('303B0806' '4EFB0002' '00040006' '4E75' '4E75'
                         '4EBA0002' '4E75' '4E75')
    path = rom_with(code)
    try:
        rom = analyze.GenesisROM(path)
        a = analyze.M68KAnalyzer(rom)
        a.EXPLICIT_SEEDS = {0x210: 'caller'}
        a.analyze()
        assert a.pc_switches.get(0x204) == [0x20C, 0x20E], a.pc_switches
        assert 0x214 in a.instructions, 'PC-relative JSR target not followed'
        assert 0x208 not in a.instructions, 'switch table decoded as code'
    finally:
        os.unlink(path)

    p = a._parse_absolute_addr
    assert p('$df2a4(pc), a4') == 0xDF2A4
    assert p('$ffff0000.l, a3') == 0xFF0000
    assert p('$e(a3), a0') is None           # register displacement: not an address
    print('test_analyze: ok')


if __name__ == '__main__':
    main()
