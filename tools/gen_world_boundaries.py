#!/usr/bin/env python3
"""
gen_world_boundaries.py
Generate resources/world_b.bin from Natural Earth 110m admin-0 GeoJSON.

Binary format (little-endian):
  magic[4]      = b"WBD1"
  num_segs      uint32
  for each seg:
    num_pts     uint32
    lon_0 lat_0 ... lon_N lat_N   (float32 pairs)

Usage:
  pip install requests
  python tools/gen_world_boundaries.py

The script downloads the GeoJSON automatically and writes
resources/world_b.bin  (relative to the project root).
Run once; the binary is loaded at application start-up.
"""

import json, math, os, struct, sys, urllib.request

NE_URL = (
    "https://raw.githubusercontent.com/datasets/geo-countries/"
    "master/data/countries.geojson"
)
OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "resources", "world_b.bin")


def flatten_rings(geometry):
    """Yield all coordinate rings (list of [lon, lat]) from a GeoJSON geometry."""
    t = geometry["type"]
    if t == "Polygon":
        yield from geometry["coordinates"]
    elif t == "MultiPolygon":
        for poly in geometry["coordinates"]:
            yield from poly


def write_bin(segments, out_path):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(b"WBD1")
        f.write(struct.pack("<I", len(segments)))
        for seg in segments:
            f.write(struct.pack("<I", len(seg)))
            for lon, lat in seg:
                f.write(struct.pack("<ff", lon, lat))
    total_pts = sum(len(s) for s in segments)
    print(f"Wrote {len(segments)} segments, {total_pts} points → {out_path}")


def main():
    print(f"Downloading {NE_URL} …")
    try:
        with urllib.request.urlopen(NE_URL, timeout=30) as r:
            data = json.loads(r.read())
    except Exception as e:
        sys.exit(f"Download failed: {e}\nPlace countries.geojson manually beside this script.")

    segments = []
    for feature in data.get("features", []):
        geom = feature.get("geometry")
        if not geom:
            continue
        for ring in flatten_rings(geom):
            if len(ring) < 2:
                continue
            segments.append([[pt[0], pt[1]] for pt in ring])

    write_bin(segments, OUT_PATH)


if __name__ == "__main__":
    main()
