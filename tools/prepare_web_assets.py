#!/usr/bin/env python3
"""Build the slim static tree served next to the wasm binary.

The desktop/Android trees keep the original 8K JPEGs. Web does not: those
files are already entropy-coded, so xz/gzip cannot shrink them. The gains
come from dropping resolution (8K -> 4K) and re-encoding continuous-tone
maps as JPEG quality 85. Alpha maps stay PNG.

Also subsets the CJK font for download size. Earlier this scanned source
text for the exact characters used and generated gui/web_font_ranges.h so
ImGui only rasterised that narrow list - correct today, but silently wrong
the next time a new place name, festival, or almanac string was added
anywhere (including data tables this scanner never covered) without
re-running the tool first. The subset now keeps ImGui's own "2500 regularly
used" Simplified Chinese set (see ZH_COMMON_OFFSETS below) unconditionally,
on top of whatever the source scan finds, so ordinary future text renders
without touching this file again; a genuinely rare/classical character still
needs a real re-run, same as before. gui/main.cpp requests ImGui's built-in
GetGlyphRangesChineseFull() directly, the same call desktop and Android
already use, instead of a generated range table - harmless against a
narrower shipped subset, since ImGui silently skips codepoints the font
file doesn't have.
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

ASSET_VER = os.environ.get("SXWNL_WEB_ASSET_VER", "v1")
MAX_EDGE = int(os.environ.get("SXWNL_WEB_MAX_EDGE", "4096"))
JPEG_QUALITY = int(os.environ.get("SXWNL_WEB_JPEG_QUALITY", "85"))

SOURCE_ROOTS = ("gui", "lunar", "eph", "mylib")
SOURCE_EXTS = {".cpp", ".h", ".hpp"}

# Extra punctuation / box-drawing / symbols that appear in the UI even when
# they are easy to miss in a source scan (string concatenations, escapes).
EXTRA_UNICODES = (
    list(range(0x0020, 0x007F))
    + list(range(0x00A0, 0x0100))
    + list(range(0x2010, 0x2028))
    + list(range(0x2030, 0x2034))
    + list(range(0x2100, 0x2104))
    + list(range(0x2190, 0x21A0))
    + list(range(0x2500, 0x2570))
    + list(range(0x25A0, 0x25C0))
    + list(range(0x2600, 0x2608))
    + list(range(0x263D, 0x2654))
    + [0x26E2, 0x3000]
    + list(range(0x3001, 0x303F))
    + list(range(0xFF01, 0xFF20))
    + list(range(0xFF21, 0xFF65))
)

# Files copied as-is (meshes, already-small PNGs with alpha, binary data).
COPY_AS_IS = [
    "planet/8k-solar-system.obj",
    "moon/Moon2K.obj",
    "world_b.bin",
    "fonts/NotoSansSymbols-Astro.ttf",
    "planet/tex/8k_saturn_ring_UV-mapped.png",
]

# Continuous-tone maps. PNG sources become JPEG; 8K sources are resized.
# rel_src -> (rel_dst, force_jpeg)
RASTER_MAP = [
    ("planet/tex/8k_sun.jpg", "planet/tex/8k_sun.jpg", True),
    ("planet/tex/8k_mercury.jpg", "planet/tex/8k_mercury.jpg", True),
    ("planet/tex/4k_venus_atmosphere.jpg", "planet/tex/4k_venus_atmosphere.jpg", True),
    ("planet/tex/8k_venus_surface.jpg", "planet/tex/8k_venus_surface.jpg", True),
    ("planet/tex/8k_earth_daymap.jpg", "planet/tex/8k_earth_daymap.jpg", True),
    ("planet/tex/8k_earth_clouds.jpg", "planet/tex/8k_earth_clouds.jpg", True),
    ("planet/tex/8k_mars.jpg", "planet/tex/8k_mars.jpg", True),
    ("planet/tex/8k_jupiter.jpg", "planet/tex/8k_jupiter.jpg", True),
    ("planet/tex/8k_saturn.jpg", "planet/tex/8k_saturn.jpg", True),
    ("planet/tex/2k_uranus.jpg", "planet/tex/2k_uranus.jpg", True),
    ("planet/tex/2k_neptune.jpg", "planet/tex/2k_neptune.jpg", True),
    ("moon/Textures/Diffuse_2K.png", "moon/Textures/Diffuse_2K.jpg", True),
]


# ImGui's ImFontAtlas::GetGlyphRangesChineseSimplifiedCommon() table (imgui
# docking branch, imgui_draw.cpp), copied verbatim so the web font subset
# never falls behind the same guarantee desktop/Android get from the full
# range. "Accumulative offsets from 0x4E00" is ImGui's own compact encoding:
# each entry is added to a running codepoint, starting at 0x4E00 (see
# UnpackAccumulativeOffsetsIntoRanges / _unpack_zh_common below). Covers
# 97.97% of characters used in general Chinese text (source: imgui's own
# citation, https://zh.wiktionary.org/wiki/附录:现代汉语常用字表). Re-copy this
# block if imgui's table is ever revised.
ZH_COMMON_OFFSETS = (
    0, 1, 2, 4, 1, 1, 1, 1, 2, 1, 3, 2, 1, 2, 2, 1, 1, 1, 1, 1, 5, 2, 1, 2, 3, 3, 3, 2, 2, 4, 1,
    1, 1, 2, 1, 5, 2, 3, 1, 2, 1, 2, 1, 1, 2, 1, 1, 2, 2, 1, 4, 1, 1, 1, 1, 5, 10, 1, 2, 19, 2,
    1, 2, 1, 2, 1, 2, 1, 2, 1, 5, 1, 6, 3, 2, 1, 2, 2, 1, 1, 1, 4, 8, 5, 1, 1, 4, 1, 1, 3, 1, 2,
    1, 5, 1, 2, 1, 1, 1, 10, 1, 1, 5, 2, 4, 6, 1, 4, 2, 2, 2, 12, 2, 1, 1, 6, 1, 1, 1, 4, 1, 1,
    4, 6, 5, 1, 4, 2, 2, 4, 10, 7, 1, 1, 4, 2, 4, 2, 1, 4, 3, 6, 10, 12, 5, 7, 2, 14, 2, 9, 1,
    1, 6, 7, 10, 4, 7, 13, 1, 5, 4, 8, 4, 1, 1, 2, 28, 5, 6, 1, 1, 5, 2, 5, 20, 2, 2, 9, 8, 11,
    2, 9, 17, 1, 8, 6, 8, 27, 4, 6, 9, 20, 11, 27, 6, 68, 2, 2, 1, 1, 1, 2, 1, 2, 2, 7, 6, 11,
    3, 3, 1, 1, 3, 1, 2, 1, 1, 1, 1, 1, 3, 1, 1, 8, 3, 4, 1, 5, 7, 2, 1, 4, 4, 8, 4, 2, 1, 2, 1,
    1, 4, 5, 6, 3, 6, 2, 12, 3, 1, 3, 9, 2, 4, 3, 4, 1, 5, 3, 3, 1, 3, 7, 1, 5, 1, 1, 1, 1, 2,
    3, 4, 5, 2, 3, 2, 6, 1, 1, 2, 1, 7, 1, 7, 3, 4, 5, 15, 2, 2, 1, 5, 3, 22, 19, 2, 1, 1, 1, 1,
    2, 5, 1, 1, 1, 6, 1, 1, 12, 8, 2, 9, 18, 22, 4, 1, 1, 5, 1, 16, 1, 2, 7, 10, 15, 1, 1, 6, 2,
    4, 1, 2, 4, 1, 6, 1, 1, 3, 2, 4, 1, 6, 4, 5, 1, 2, 1, 1, 2, 1, 10, 3, 1, 3, 2, 1, 9, 3, 2,
    5, 7, 2, 19, 4, 3, 6, 1, 1, 1, 1, 1, 4, 3, 2, 1, 1, 1, 2, 5, 3, 1, 1, 1, 2, 2, 1, 1, 2, 1,
    1, 2, 1, 3, 1, 1, 1, 3, 7, 1, 4, 1, 1, 2, 1, 1, 2, 1, 2, 4, 4, 3, 8, 1, 1, 1, 2, 1, 3, 5, 1,
    3, 1, 3, 4, 6, 2, 2, 14, 4, 6, 6, 11, 9, 1, 15, 3, 1, 28, 5, 2, 5, 5, 3, 1, 3, 4, 5, 4, 6,
    14, 3, 2, 3, 5, 21, 2, 7, 20, 10, 1, 2, 19, 2, 4, 28, 28, 2, 3, 2, 1, 14, 4, 1, 26, 28, 42,
    12, 40, 3, 52, 79, 5, 14, 17, 3, 2, 2, 11, 3, 4, 6, 3, 1, 8, 2, 23, 4, 5, 8, 10, 4, 2, 7, 3,
    5, 1, 1, 6, 3, 1, 2, 2, 2, 5, 28, 1, 1, 7, 7, 20, 5, 3, 29, 3, 17, 26, 1, 8, 4, 27, 3, 6,
    11, 23, 5, 3, 4, 6, 13, 24, 16, 6, 5, 10, 25, 35, 7, 3, 2, 3, 3, 14, 3, 6, 2, 6, 1, 4, 2, 3,
    8, 2, 1, 1, 3, 3, 3, 4, 1, 1, 13, 2, 2, 4, 5, 2, 1, 14, 14, 1, 2, 2, 1, 4, 5, 2, 3, 1, 14,
    3, 12, 3, 17, 2, 16, 5, 1, 2, 1, 8, 9, 3, 19, 4, 2, 2, 4, 17, 25, 21, 20, 28, 75, 1, 10, 29,
    103, 4, 1, 2, 1, 1, 4, 2, 4, 1, 2, 3, 24, 2, 2, 2, 1, 1, 2, 1, 3, 8, 1, 1, 1, 2, 1, 1, 3, 1,
    1, 1, 6, 1, 5, 3, 1, 1, 1, 3, 4, 1, 1, 5, 2, 1, 5, 6, 13, 9, 16, 1, 1, 1, 1, 3, 2, 3, 2, 4,
    5, 2, 5, 2, 2, 3, 7, 13, 7, 2, 2, 1, 1, 1, 1, 2, 3, 3, 2, 1, 6, 4, 9, 2, 1, 14, 2, 14, 2, 1,
    18, 3, 4, 14, 4, 11, 41, 15, 23, 15, 23, 176, 1, 3, 4, 1, 1, 1, 1, 5, 3, 1, 2, 3, 7, 3, 1,
    1, 2, 1, 2, 4, 4, 6, 2, 4, 1, 9, 7, 1, 10, 5, 8, 16, 29, 1, 1, 2, 2, 3, 1, 3, 5, 2, 4, 5, 4,
    1, 1, 2, 2, 3, 3, 7, 1, 6, 10, 1, 17, 1, 44, 4, 6, 2, 1, 1, 6, 5, 4, 2, 10, 1, 6, 9, 2, 8,
    1, 24, 1, 2, 13, 7, 8, 8, 2, 1, 4, 1, 3, 1, 3, 3, 5, 2, 5, 10, 9, 4, 9, 12, 2, 1, 6, 1, 10,
    1, 1, 7, 7, 4, 10, 8, 3, 1, 13, 4, 3, 1, 6, 1, 3, 5, 2, 1, 2, 17, 16, 5, 2, 16, 6, 1, 4, 2,
    1, 3, 3, 6, 8, 5, 11, 11, 1, 3, 3, 2, 4, 6, 10, 9, 5, 7, 4, 7, 4, 7, 1, 1, 4, 2, 1, 3, 6, 8,
    7, 1, 6, 11, 5, 5, 3, 24, 9, 4, 2, 7, 13, 5, 1, 8, 82, 16, 61, 1, 1, 1, 4, 2, 2, 16, 10, 3,
    8, 1, 1, 6, 4, 2, 1, 3, 1, 1, 1, 4, 3, 8, 4, 2, 2, 1, 1, 1, 1, 1, 6, 3, 5, 1, 1, 4, 6, 9, 2,
    1, 1, 1, 2, 1, 7, 2, 1, 6, 1, 5, 4, 4, 3, 1, 8, 1, 3, 3, 1, 3, 2, 2, 2, 2, 3, 1, 6, 1, 2, 1,
    2, 1, 3, 7, 1, 8, 2, 1, 2, 1, 5, 2, 5, 3, 5, 10, 1, 2, 1, 1, 3, 2, 5, 11, 3, 9, 3, 5, 1, 1,
    5, 9, 1, 2, 1, 5, 7, 9, 9, 8, 1, 3, 3, 3, 6, 8, 2, 3, 2, 1, 1, 32, 6, 1, 2, 15, 9, 3, 7, 13,
    1, 3, 10, 13, 2, 14, 1, 13, 10, 2, 1, 3, 10, 4, 15, 2, 15, 15, 10, 1, 3, 9, 6, 9, 32, 25,
    26, 47, 7, 3, 2, 3, 1, 6, 3, 4, 3, 2, 8, 5, 4, 1, 9, 4, 2, 2, 19, 10, 6, 2, 3, 8, 1, 2, 2,
    4, 2, 1, 9, 4, 4, 4, 6, 4, 8, 9, 2, 3, 1, 1, 1, 1, 3, 5, 5, 1, 3, 8, 4, 6, 2, 1, 4, 12, 1,
    5, 3, 7, 13, 2, 5, 8, 1, 6, 1, 2, 5, 14, 6, 1, 5, 2, 4, 8, 15, 5, 1, 23, 6, 62, 2, 10, 1, 1,
    8, 1, 2, 2, 10, 4, 2, 2, 9, 2, 1, 1, 3, 2, 3, 1, 5, 3, 3, 2, 1, 3, 8, 1, 1, 1, 11, 3, 1, 1,
    4, 3, 7, 1, 14, 1, 2, 3, 12, 5, 2, 5, 1, 6, 7, 5, 7, 14, 11, 1, 3, 1, 8, 9, 12, 2, 1, 11, 8,
    4, 4, 2, 6, 10, 9, 13, 1, 1, 3, 1, 5, 1, 3, 2, 4, 4, 1, 18, 2, 3, 14, 11, 4, 29, 4, 2, 7, 1,
    3, 13, 9, 2, 2, 5, 3, 5, 20, 7, 16, 8, 5, 72, 34, 6, 4, 22, 12, 12, 28, 45, 36, 9, 7, 39, 9,
    191, 1, 1, 1, 4, 11, 8, 4, 9, 2, 3, 22, 1, 1, 1, 1, 4, 17, 1, 7, 7, 1, 11, 31, 10, 2, 4, 8,
    2, 3, 2, 1, 4, 2, 16, 4, 32, 2, 3, 19, 13, 4, 9, 1, 5, 2, 14, 8, 1, 1, 3, 6, 19, 6, 5, 1,
    16, 6, 2, 10, 8, 5, 1, 2, 3, 1, 5, 5, 1, 11, 6, 6, 1, 3, 3, 2, 6, 3, 8, 1, 1, 4, 10, 7, 5,
    7, 7, 5, 8, 9, 2, 1, 3, 4, 1, 1, 3, 1, 3, 3, 2, 6, 16, 1, 4, 6, 3, 1, 10, 6, 1, 3, 15, 2, 9,
    2, 10, 25, 13, 9, 16, 6, 2, 2, 10, 11, 4, 3, 9, 1, 2, 6, 6, 5, 4, 30, 40, 1, 10, 7, 12, 14,
    33, 6, 3, 6, 7, 3, 1, 3, 1, 11, 14, 4, 9, 5, 12, 11, 49, 18, 51, 31, 140, 31, 2, 2, 1, 5, 1,
    8, 1, 10, 1, 4, 4, 3, 24, 1, 10, 1, 3, 6, 6, 16, 3, 4, 5, 2, 1, 4, 2, 57, 10, 6, 22, 2, 22,
    3, 7, 22, 6, 10, 11, 36, 18, 16, 33, 36, 2, 5, 5, 1, 1, 1, 4, 10, 1, 4, 13, 2, 7, 5, 2, 9,
    3, 4, 1, 7, 43, 3, 7, 3, 9, 14, 7, 9, 1, 11, 1, 1, 3, 7, 4, 18, 13, 1, 14, 1, 3, 6, 10, 73,
    2, 2, 30, 6, 1, 11, 18, 19, 13, 22, 3, 46, 42, 37, 89, 7, 3, 16, 34, 2, 2, 3, 9, 1, 7, 1, 1,
    1, 2, 2, 4, 10, 7, 3, 10, 3, 9, 5, 28, 9, 2, 6, 13, 7, 3, 1, 3, 10, 2, 7, 2, 11, 3, 6, 21,
    54, 85, 2, 1, 4, 2, 2, 1, 39, 3, 21, 2, 2, 5, 1, 1, 1, 4, 1, 1, 3, 4, 15, 1, 3, 2, 4, 4, 2,
    3, 8, 2, 20, 1, 8, 7, 13, 4, 1, 26, 6, 2, 9, 34, 4, 21, 52, 10, 4, 4, 1, 5, 12, 2, 11, 1, 7,
    2, 30, 12, 44, 2, 30, 1, 1, 3, 6, 16, 9, 17, 39, 82, 2, 2, 24, 7, 1, 7, 3, 16, 9, 14, 44, 2,
    1, 2, 1, 2, 3, 5, 2, 4, 1, 6, 7, 5, 3, 2, 6, 1, 11, 5, 11, 2, 1, 18, 19, 8, 1, 3, 24, 29, 2,
    1, 3, 5, 2, 2, 1, 13, 6, 5, 1, 46, 11, 3, 5, 1, 1, 5, 8, 2, 10, 6, 12, 6, 3, 7, 11, 2, 4,
    16, 13, 2, 5, 1, 1, 2, 2, 5, 2, 28, 5, 2, 23, 10, 8, 4, 4, 22, 39, 95, 38, 8, 14, 9, 5, 1,
    13, 5, 4, 3, 13, 12, 11, 1, 9, 1, 27, 37, 2, 5, 4, 4, 63, 211, 95, 2, 2, 2, 1, 3, 5, 2, 1,
    1, 2, 2, 1, 1, 1, 3, 2, 4, 1, 2, 1, 1, 5, 2, 2, 1, 1, 2, 3, 1, 3, 1, 1, 1, 3, 1, 4, 2, 1, 3,
    6, 1, 1, 3, 7, 15, 5, 3, 2, 5, 3, 9, 11, 4, 2, 22, 1, 6, 3, 8, 7, 1, 4, 28, 4, 16, 3, 3, 25,
    4, 4, 27, 27, 1, 4, 1, 2, 2, 7, 1, 3, 5, 2, 28, 8, 2, 14, 1, 8, 6, 16, 25, 3, 3, 3, 14, 3,
    3, 1, 1, 2, 1, 4, 6, 3, 8, 4, 1, 1, 1, 2, 3, 6, 10, 6, 2, 3, 18, 3, 2, 5, 5, 4, 3, 1, 5, 2,
    5, 4, 23, 7, 6, 12, 6, 4, 17, 11, 9, 5, 1, 1, 10, 5, 12, 1, 1, 11, 26, 33, 7, 3, 6, 1, 17,
    7, 1, 5, 12, 1, 11, 2, 4, 1, 8, 14, 17, 23, 1, 2, 1, 7, 8, 16, 11, 9, 6, 5, 2, 6, 4, 16, 2,
    8, 14, 1, 11, 8, 9, 1, 1, 1, 9, 25, 4, 11, 19, 7, 2, 15, 2, 12, 8, 52, 7, 5, 19, 2, 16, 4,
    36, 8, 1, 16, 8, 24, 26, 4, 6, 2, 9, 5, 4, 36, 3, 28, 12, 25, 15, 37, 27, 17, 12, 59, 38, 5,
    32, 127, 1, 2, 9, 17, 14, 4, 1, 2, 1, 1, 8, 11, 50, 4, 14, 2, 19, 16, 4, 17, 5, 4, 5, 26,
    12, 45, 2, 23, 45, 104, 30, 12, 8, 3, 10, 2, 2, 3, 3, 1, 4, 20, 7, 2, 9, 6, 15, 2, 20, 1, 3,
    16, 4, 11, 15, 6, 134, 2, 5, 59, 1, 2, 2, 2, 1, 9, 17, 3, 26, 137, 10, 211, 59, 1, 2, 4, 1,
    4, 1, 1, 1, 2, 6, 2, 3, 1, 1, 2, 3, 2, 3, 1, 3, 4, 4, 2, 3, 3, 1, 4, 3, 1, 7, 2, 2, 3, 1, 2,
    1, 3, 3, 3, 2, 2, 3, 2, 1, 3, 14, 6, 1, 3, 2, 9, 6, 15, 27, 9, 34, 145, 1, 1, 2, 1, 1, 1, 1,
    2, 1, 1, 1, 1, 2, 2, 2, 3, 1, 2, 1, 1, 1, 2, 3, 5, 8, 3, 5, 2, 4, 1, 3, 2, 2, 2, 12, 4, 1,
    1, 1, 10, 4, 5, 1, 20, 4, 16, 1, 15, 9, 5, 12, 2, 9, 2, 5, 4, 2, 26, 19, 7, 1, 26, 4, 30,
    12, 15, 42, 1, 6, 8, 172, 1, 1, 4, 2, 1, 1, 11, 2, 2, 4, 2, 1, 2, 1, 10, 8, 1, 2, 1, 4, 5,
    1, 2, 5, 1, 8, 4, 1, 3, 4, 2, 1, 6, 2, 1, 3, 4, 1, 2, 1, 1, 1, 1, 12, 5, 7, 2, 4, 3, 1, 1,
    1, 3, 3, 6, 1, 2, 2, 3, 3, 3, 2, 1, 2, 12, 14, 11, 6, 6, 4, 12, 2, 8, 1, 7, 10, 1, 35, 7, 4,
    13, 15, 4, 3, 23, 21, 28, 52, 5, 26, 5, 6, 1, 7, 10, 2, 7, 53, 3, 2, 1, 1, 1, 2, 163, 532,
    1, 10, 11, 1, 3, 3, 4, 8, 2, 8, 6, 2, 2, 23, 22, 4, 2, 2, 4, 2, 1, 3, 1, 3, 3, 5, 9, 8, 2,
    1, 2, 8, 1, 10, 2, 12, 21, 20, 15, 105, 2, 3, 1, 1, 3, 2, 3, 1, 1, 2, 5, 1, 4, 15, 11, 19,
    1, 1, 1, 1, 5, 4, 5, 1, 1, 2, 5, 3, 5, 12, 1, 2, 5, 1, 11, 1, 1, 15, 9, 1, 4, 5, 3, 26, 8,
    2, 1, 3, 1, 1, 15, 19, 2, 12, 1, 2, 5, 2, 7, 2, 19, 2, 20, 6, 26, 7, 5, 2, 2, 7, 34, 21, 13,
    70, 2, 128, 1, 1, 2, 1, 1, 2, 1, 1, 3, 2, 2, 2, 15, 1, 4, 1, 3, 4, 42, 10, 6, 1, 49, 85, 8,
    1, 2, 1, 1, 4, 4, 2, 3, 6, 1, 5, 7, 4, 3, 211, 4, 1, 2, 1, 2, 5, 1, 2, 4, 2, 2, 6, 5, 6, 10,
    3, 4, 48, 100, 6, 2, 16, 296, 5, 27, 387, 2, 2, 3, 7, 16, 8, 5, 38, 15, 39, 21, 9, 10, 3, 7,
    59, 13, 27, 21, 47, 5, 21, 6,
)


def _unpack_zh_common() -> list[int]:
    codepoints = []
    cp = 0x4E00
    for offset in ZH_COMMON_OFFSETS:
        cp += offset
        codepoints.append(cp)
    return codepoints


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def collect_codepoints(root: Path) -> list[int]:
    found: set[int] = set(EXTRA_UNICODES)
    # Any of the ~2500 everyday Simplified Chinese characters, not just the
    # ones this scan happens to find today: ordinary future text - including
    # strings built from data tables this scan cannot see - still has to
    # render without another release.
    found.update(_unpack_zh_common())
    for folder in SOURCE_ROOTS:
        base = root / folder
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix.lower() not in SOURCE_EXTS:
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except (UnicodeDecodeError, OSError):
                continue
            for ch in text:
                found.add(ord(ch))
    # Never emit the NUL sentinel ImGui uses to terminate a range list,
    # and skip C0 controls that do not belong in a font.
    return sorted(cp for cp in found if cp >= 0x20)


def pack_ranges(codepoints: list[int]) -> list[tuple[int, int]]:
    if not codepoints:
        return []
    ranges: list[tuple[int, int]] = []
    start = prev = codepoints[0]
    for cp in codepoints[1:]:
        if cp == prev + 1:
            prev = cp
            continue
        ranges.append((start, prev))
        start = prev = cp
    ranges.append((start, prev))
    return ranges


def write_unicodes_file(path: Path, codepoints: list[int]) -> None:
    path.write_text("\n".join(f"U+{cp:04X}" for cp in codepoints) + "\n", encoding="utf-8")


def subset_font(src: Path, dst: Path, unicodes_file: Path) -> None:
    try:
        from fontTools.subset import main as subset_main
    except ImportError as exc:
        raise SystemExit(
            "fontTools is required to subset the CJK font.\n"
            "  python3 -m pip install fonttools brotli"
        ) from exc
    dst.parent.mkdir(parents=True, exist_ok=True)
    argv = [
        str(src),
        f"--unicodes-file={unicodes_file}",
        "--layout-features=*",
        "--glyph-names",
        "--symbol-cmap",
        "--legacy-cmap",
        "--notdef-glyph",
        "--notdef-outline",
        "--recommended-glyphs",
        "--name-IDs=*",
        "--name-legacy",
        "--name-languages=*",
        f"--output-file={dst}",
    ]
    subset_main(argv)


def encode_raster(src: Path, dst: Path, force_jpeg: bool) -> None:
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required to resize / re-encode web textures.\n"
            "  python3 -m pip install pillow"
        ) from exc

    dst.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(src) as im:
        im.load()
        w, h = im.size
        scale = 1.0
        edge = max(w, h)
        if edge > MAX_EDGE:
            scale = MAX_EDGE / float(edge)
        nw = max(1, int(round(w * scale)))
        nh = max(1, int(round(h * scale)))
        # Keep even sizes; some GPU decoders pad odd JPEG dimensions.
        nw -= nw % 2
        nh -= nh % 2
        nw = max(2, nw)
        nh = max(2, nh)
        if (nw, nh) != (w, h):
            im = im.resize((nw, nh), Image.Resampling.LANCZOS)

        if force_jpeg:
            if im.mode not in ("RGB", "L"):
                im = im.convert("RGB")
            im.save(
                dst,
                format="JPEG",
                quality=JPEG_QUALITY,
                optimize=True,
                progressive=True,
                subsampling=2,
            )
        else:
            im.save(dst)


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def fmt_mb(n: int) -> str:
    return f"{n / (1024 * 1024):.2f}MiB"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        default=None,
        help="Output directory (default: web/resources/<ver>)",
    )
    parser.add_argument(
        "--skip-images",
        action="store_true",
        help="Only regenerate the font subset and glyph list",
    )
    args = parser.parse_args()

    root = repo_root()
    out = Path(args.out) if args.out else root / "web" / "resources" / ASSET_VER
    out.mkdir(parents=True, exist_ok=True)

    codepoints = collect_codepoints(root)
    ranges = pack_ranges(codepoints)
    unicodes_file = out / "fonts" / "web_glyphs.txt"
    unicodes_file.parent.mkdir(parents=True, exist_ok=True)
    write_unicodes_file(unicodes_file, codepoints)
    print(f"[font] {len(codepoints)} codepoints in {len(ranges)} ranges -> {unicodes_file}")

    font_src = root / "resources" / "fonts" / "NotoSansCJKsc-Regular.otf"
    font_dst = out / "fonts" / "NotoSansCJKsc-Regular.otf"
    if not font_src.is_file():
        raise SystemExit(f"missing bundled CJK font: {font_src}")
    subset_font(font_src, font_dst, unicodes_file)
    print(
        f"[font] subset {fmt_mb(font_src.stat().st_size)} -> "
        f"{fmt_mb(font_dst.stat().st_size)}  {font_dst}"
    )

    if args.skip_images:
        return 0

    copied = 0
    for rel in COPY_AS_IS:
        src = root / "resources" / rel
        if not src.is_file():
            print(f"[skip] missing {rel}", file=sys.stderr)
            continue
        dst = out / rel
        copy_file(src, dst)
        print(f"[copy] {rel}  {fmt_mb(src.stat().st_size)}")
        copied += 1

    for src_rel, dst_rel, force_jpeg in RASTER_MAP:
        src = root / "resources" / src_rel
        if not src.is_file():
            print(f"[skip] missing {src_rel}", file=sys.stderr)
            continue
        dst = out / dst_rel
        encode_raster(src, dst, force_jpeg)
        print(
            f"[tex]  {src_rel} {fmt_mb(src.stat().st_size)} -> "
            f"{dst_rel} {fmt_mb(dst.stat().st_size)}"
        )

    notice = root / "resources" / "fonts" / "NOTICE.md"
    if notice.is_file():
        copy_file(notice, out / "fonts" / "NOTICE.md")

    total = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"[done] {out}  {fmt_mb(total)}")
    # Tiny manifest so the hosted tree can be inspected without listing S3/EO.
    manifest = out / "manifest.txt"
    rows = []
    for p in sorted(out.rglob("*")):
        if p.is_file() and p.name != "manifest.txt":
            rel = p.relative_to(out).as_posix()
            rows.append(f"{p.stat().st_size:12d}  {rel}")
    manifest.write_text("\n".join(rows) + f"\n# total {total}\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
