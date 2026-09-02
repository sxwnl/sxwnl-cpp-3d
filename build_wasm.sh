#!/usr/bin/env bash
# build_wasm.sh — 将 sxwnl_gui 编译为 WebAssembly 前端预览项目 (Emscripten)
# 用法: bash build_wasm.sh [--clean] [--debug]
#
# 前置条件: 已安装并激活 emsdk (https://emscripten.org/docs/getting_started/downloads.html)
#   git clone https://github.com/emscripten-core/emsdk.git
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source ./emsdk_env.sh     # 每个新终端都要执行一次，把 emcc/emcmake 加入 PATH
#
# 构建产物 (index.html / .js / .wasm / .data) 会被复制到 web/ 目录，
# 可直接用任意静态文件服务器打开预览，例如:
#   cd web && python3 -m http.server 8080
set -e

BUILD_TYPE=Release
CLEAN=0
for arg in "$@"; do
  case $arg in
    --clean)   CLEAN=1 ;;
    --debug)   BUILD_TYPE=Debug ;;
    --release) BUILD_TYPE=Release ;;
  esac
done

BUILD_DIR="build_wasm"
WEB_DIR="web"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo ""
echo "========================================"
echo "  sxwnl-cpp WebAssembly 构建脚本"
echo "  BUILD_TYPE=$BUILD_TYPE"
echo "========================================"

# ─── 检测 emcmake / emcc ─────────────────────────────────────────────────────
if ! command -v emcmake &>/dev/null || ! command -v emcc &>/dev/null; then
  echo "[error] 未找到 emcmake/emcc，请先安装并激活 emsdk:"
  echo "  git clone https://github.com/emscripten-core/emsdk.git"
  echo "  cd emsdk && ./emsdk install latest && ./emsdk activate latest"
  echo "  source ./emsdk_env.sh   # 每个新终端都需要重新执行一次"
  exit 1
fi
echo "[ok] $(emcc --version | head -1)"

# ─── 清理 ─────────────────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 ]] && [[ -d "$BUILD_DIR" ]]; then
  echo "[info] 清理 $BUILD_DIR ..."
  rm -rf "$BUILD_DIR"
fi
mkdir -p "$WEB_DIR"

# ─── cmake 配置 ───────────────────────────────────────────────────────────────
echo ""
echo "[cmake] 配置中 (首次运行将下载 imgui / stb, 需要网络)..."
emcmake cmake -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSXWNL_BUILD_GUI=ON \
  -DSXWNL_BUILD_TESTS=OFF

# ─── 编译 ─────────────────────────────────────────────────────────────────────
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
echo ""
echo "[cmake] 编译 sxwnl_gui ($JOBS 线程, resources/ 会被打包进 .data, 可能需要一些时间)..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --target sxwnl_gui -j "$JOBS"

# ─── 拷贝产物到 web/ ───────────────────────────────────────────────────────────
OUT_BASE="$BUILD_DIR/sxwnl_gui"
if [[ ! -f "$OUT_BASE.html" ]]; then OUT_BASE="$BUILD_DIR/$BUILD_TYPE/sxwnl_gui"; fi
if [[ ! -f "$OUT_BASE.html" ]]; then
  echo "[error] 未找到编译产物 sxwnl_gui.html"; exit 1
fi

echo ""
echo "[copy] 拷贝构建产物到 $WEB_DIR/ ..."
cp -f "$OUT_BASE.html" "$WEB_DIR/index.html"
for ext in js wasm data wasm.map; do
  [[ -f "$OUT_BASE.$ext" ]] && cp -f "$OUT_BASE.$ext" "$WEB_DIR/sxwnl_gui.$ext"
done

echo ""
echo "========================================"
echo "  构建完成"
echo "========================================"
echo "  产物目录: $WEB_DIR/"
echo "  本地预览 (浏览器不允许直接用 file:// 打开 wasm, 需起一个静态服务器):"
echo "    cd $WEB_DIR && python3 -m http.server 8080"
echo "    然后浏览器打开 http://localhost:8080/index.html"
echo ""
