#!/usr/bin/env python3
"""Generate the exact LVGL UI font subset used by the Star Pet firmware.

Run from firmware/:  python3 tools/gen_font.py
Requires lv_font_conv (node) and a TTF/OTF via PET_FONT.
The checked-in font is an OFL-licensed Noto Sans SC subset.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONT = Path(os.environ.get("PET_FONT", "/tmp/HeitiSCMedium.ttf"))
CONV = Path(os.environ.get("LV_FONT_CONV", str(Path.home() / ".npm/_npx/5e891675aaf2bdf7/node_modules/lv_font_conv/lv_font_conv.js")))
SIZE = os.environ.get("PET_FONT_SIZE", "18")
FONT_NAME = f"lv_font_starpet_{SIZE}"
OUT = ROOT / f"components/pet_ui/src/{FONT_NAME}.c"
SOURCES = [
    ROOT / "components/pet_ui/src/pet_ui.c",
    ROOT / "main/main.c",
    ROOT / "components/pet_core/src/pet_core.c",
]

def collect_symbols() -> str:
    chars = set()
    lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
    for path in SOURCES:
        text = path.read_text(encoding="utf-8")
        for m in lit.finditer(text):
            for ch in m.group(1):
                if ord(ch) > 0x7E:
                    chars.add(ch)
    return "".join(sorted(chars))

def main() -> int:
    if not FONT.exists():
        print(f"font not found: {FONT}", file=sys.stderr)
        return 1
    if not CONV.exists():
        print(f"lv_font_conv not found: {CONV}", file=sys.stderr)
        return 1
    symbols = collect_symbols()
    print(f"non-ASCII symbols: {len(symbols)}")
    print(symbols)
    cmd = [
        "node", str(CONV),
        "--size", SIZE, "--bpp", "4",
        "--format", "lvgl", "--no-compress", "--no-kerning",
        "--font", str(FONT),
        "-r", "0x20-0x7E",
        "--symbols", symbols,
        "-o", str(OUT),
        "--lv-font-name", FONT_NAME,
    ]
    subprocess.run(cmd, check=True)
    # Managed LVGL exposes lvgl.h, not lvgl/lvgl.h.
    text = OUT.read_text()
    text = text.replace(
        '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n'
        '#include "lvgl.h"\n'
        '#else\n'
        '#include "lvgl/lvgl.h"\n'
        '#endif\n',
        '#include "lvgl.h"\n')
    OUT.write_text(text)
    print(f"generated: {OUT} ({OUT.stat().st_size} bytes)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
