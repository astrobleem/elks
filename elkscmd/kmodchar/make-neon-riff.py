#!/usr/bin/env python3
"""Generate Neon Relay, an original PC-9801-26/K and Tandy EX test riff."""
import struct
import sys
from pathlib import Path

CLOCK = 3993600
STEP = 8820                         # 200 ms at 44.1 kHz
melody = [76, None, 79, 81, 79, 76, None, 74,
          76, 79, None, 83, 81, None, 79, 74,
          76, None, 79, 81, 84, 83, 81, 79,
          76, None, 74, 72, 71, 74, 76, None]
bass = [40, 40, 47, 40, 43, 43, 50, 43,
        45, 45, 52, 45, 47, 47, 54, 47,
        40, 40, 47, 40, 48, 48, 55, 48,
        43, 43, 50, 43, 47, 47, 50, 40]


def track_ym2203():
    data = bytearray()

    def reg(r, v):
        data.extend((0x55, r, v))

    def note(ch, key):
        reg(0x28, ch)
        if key is None:
            return
        hz = 440 * 2 ** ((key - 69) / 12)
        block = 0
        fnum = round(hz * 144 * 2**20 / CLOCK)
        while fnum > 2047:
            fnum = (fnum + 1) // 2
            block += 1
        reg(0xa4 + ch, block * 8 + (fnum >> 8))
        reg(0xa0 + ch, fnum & 255)
        reg(0x28, 0x10 | ch)

    def wait(n):
        data.append(0x61)
        data.extend(struct.pack('<H', n))

    for ch, level in enumerate((25, 33, 42)):
        reg(0x28, ch)
        for op in (4, 8, 12):
            reg(0x40 + op + ch, 127)
        for r, v in [(0xb0, 7), (0x30, 1), (0x40, level), (0x50, 31),
                     (0x60, 12), (0x70, 11), (0x80, 0x4f), (0x90, 0)]:
            reg(r + ch, v)
    for i, (lead, low) in enumerate(zip(melody, bass)):
        note(0, lead)
        note(1, low)
        note(2, low + 19 if i % 4 in (1, 3) else None)
        wait(7056)
        for ch in range(3):
            reg(0x28, ch)
        wait(STEP - 7056)
    data.append(0x66)
    return data


def track_tandy():
    data = bytearray()

    def psg(value):
        data.extend((0x50, value))

    def wait(n):
        data.append(0x61)
        data.extend(struct.pack('<H', n))

    def mute():
        for ch in range(4):
            psg(0x9f + 0x20 * ch)

    def tone(ch, key, level):
        if key is not None:
            hz = 440 * 2 ** ((key - 69) / 12)
            divisor = round(3579545 / (32 * hz))
            psg(0x80 + 0x20 * ch + (divisor & 15))
            psg(divisor >> 4)
            psg(0x90 + 0x20 * ch + level)

    mute()
    for i, (lead, low) in enumerate(zip(melody, bass)):
        tone(0, lead, 5)
        tone(1, low + 12, 7)
        tone(2, low + 19 if i % 4 in (1, 3) else None, 10)
        if i % 4 in (0, 2):
            psg(0xe5 if i % 4 == 0 else 0xe6)
            psg(0xfc)
        wait(1323)
        psg(0xff)
        wait(5733)
        mute()
        wait(STEP - 7056)
    data.append(0x66)
    return data


def vgm(data, tandy=False):
    header = bytearray(0x80)
    header[:4] = b'Vgm '
    for p, value in [(4, len(header) + len(data) - 4), (8, 0x151),
                     (0x18, len(melody) * STEP), (0x34, len(header) - 0x34),
                     (0x0c if tandy else 0x44, 3579545 if tandy else CLOCK)]:
        struct.pack_into('<I', header, p, value)
    if tandy:
        header[0x28:0x2c] = b"\x06\x00\x0f\x00"
    return header + data


if __name__ == '__main__':
    d = Path(sys.argv[1]) if len(sys.argv) == 2 else Path('.')
    (d / 'NEON26K.VGM').write_bytes(vgm(track_ym2203()))
    (d / 'NEONEX.VGM').write_bytes(vgm(track_tandy(), tandy=True))
