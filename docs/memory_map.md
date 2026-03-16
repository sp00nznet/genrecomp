# Genesis Memory Map (M68K Side)

## Address Space

The Motorola 68000 in the Genesis has a 24-bit address bus (16MB addressable).

| Address Range       | Size  | Description                          |
|---------------------|-------|--------------------------------------|
| `$000000-$3FFFFF`   | 4MB   | Cartridge ROM                        |
| `$400000-$7FFFFF`   | 4MB   | Reserved / unused (some mappers)     |
| `$800000-$9FFFFF`   | 2MB   | Reserved / SRAM (mapper dependent)   |
| `$A00000-$A0FFFF`   | 64KB  | Z80 address space                    |
| `$A10000-$A1001F`   | 32B   | I/O area                             |
| `$A11000-$A110FF`   |       | Memory mode / bus control             |
| `$A11100-$A11101`   | 2B    | Z80 bus request                      |
| `$A11200-$A11201`   | 2B    | Z80 reset                            |
| `$A14000-$A14003`   | 4B    | TMSS (Trademark Security System)     |
| `$C00000-$C00003`   | 4B    | VDP data port                        |
| `$C00004-$C00007`   | 4B    | VDP control port                     |
| `$C00008-$C0000F`   | 8B    | HV counter                           |
| `$C00011`           | 1B    | PSG output (directly mapped)         |
| `$E00000-$FFFFFF`   | 2MB   | Work RAM (64KB at $FF0000, mirrored) |

## VDP Registers

The VDP has 24 internal registers, written via the control port with format:
`$80xx` where the high nybble is `8+reg` and low byte is the value.

| Register | Description                              |
|----------|------------------------------------------|
| $00      | Mode Register 1 (H-INT enable, etc.)     |
| $01      | Mode Register 2 (display enable, V-INT)  |
| $02      | Plane A name table address               |
| $03      | Window name table address                |
| $04      | Plane B name table address               |
| $05      | Sprite attribute table address           |
| $07      | Background color (palette + index)       |
| $0A      | H-INT counter                            |
| $0B      | Mode Register 3 (scroll mode)            |
| $0C      | Mode Register 4 (H40/H32, interlace)    |
| $0D      | H-scroll data table address              |
| $0F      | Auto-increment value                     |
| $10      | Plane size                               |
| $11      | Window H position                        |
| $12      | Window V position                        |
| $13-$14  | DMA length                               |
| $15-$17  | DMA source address                       |

## Z80 Address Space ($A00000-$A0FFFF from M68K side)

| Z80 Address | M68K Address  | Description            |
|-------------|---------------|------------------------|
| $0000-$1FFF | $A00000-$A01FFF | Z80 RAM (8KB)       |
| $4000-$4003 | $A04000-$A04003 | YM2612 registers     |
| $6000       | $A06000         | Bank register        |
| $7F11       | $A07F11         | PSG output           |
| $8000-$FFFF | $A08000-$A0FFFF | M68K bank window     |

## I/O Registers ($A10000-$A1001F)

| Address    | Description                               |
|------------|-------------------------------------------|
| $A10001    | Version register (region, NTSC/PAL)       |
| $A10003    | Data port 1 (player 1 controller)         |
| $A10005    | Data port 2 (player 2 controller)         |
| $A10007    | Data port 3 (expansion)                   |
| $A10009    | Control port 1                            |
| $A1000B    | Control port 2                            |
| $A1000D    | Control port 3                            |
