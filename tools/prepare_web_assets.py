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
re-running the tool first. The subset now keeps all 6763 hanzi of GB 2312
unconditionally, on top of whatever the source scan finds, so ordinary
future text renders without touching this file again. gui/main.cpp requests
ImGui's built-in GetGlyphRangesChineseFull() directly, the same call desktop
and Android already use, instead of a generated range table.
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


def gb2312_hanzi() -> list[int]:
    """The 6763 hanzi of GB 2312-80, enumerated from the codec itself.

    This is the standard "common modern Chinese" set: it covers everyday text,
    names and places well past what this repo currently contains, so text added
    later renders without regenerating anything. Kept as a codec query rather
    than a pasted table so there is nothing to fall out of date.
    """
    found = []
    for cp in range(0x4E00, 0xA000):
        try:
            chr(cp).encode("gb2312")
        except UnicodeEncodeError:
            continue
        found.append(cp)
    return found


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def collect_codepoints(root: Path) -> list[int]:
    found: set[int] = set(EXTRA_UNICODES)
    # All of GB 2312, not just the characters this scan happens to find
    # today: ordinary future text - including strings built from data tables
    # this scan cannot see - still has to render without another release.
    found.update(gb2312_hanzi())
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
