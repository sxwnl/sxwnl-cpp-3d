#!/usr/bin/env bash
set -e

EXE_POSIX="$1"
OUT_DIR="$2"

# cygcheck 是 Windows 工具，需要 Windows 路径
EXE_WIN=$(cygpath -w "$EXE_POSIX")

echo "[copy_cygdlls] scanning: $EXE_WIN"
echo "[copy_cygdlls] output:   $OUT_DIR"

cygcheck "$EXE_WIN" \
  | grep -oiE '[A-Za-z]:[^ ]*[\\/]cyg[A-Za-z0-9_.+\-]*\.dll' \
  | sort -u \
  | while read -r winpath; do
      posix=$(cygpath -u "$winpath")
      echo "[copy_cygdlls] cp $posix -> $OUT_DIR/"
      cp "$posix" "$OUT_DIR/"
    done

echo "[copy_cygdlls] done"
