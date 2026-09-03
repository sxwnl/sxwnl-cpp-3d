#!/usr/bin/env bash
# build_wasm.sh — 将 sxwnl_gui 编译为 WebAssembly 前端预览项目 (Emscripten)
# 用法: bash build_wasm.sh [--clean] [--debug]
#
# 前置条件: 已安装并激活 emsdk (https://emscripten.org/docs/getting_started/downloads.html)
#   git clone https://github.com/emscripten-core/emsdk.git
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source ./emsdk_env.sh     # 每个新终端都要执行一次，把 emcc/emcmake 加入 PATH
#
# 构建产物 (index.html / .js / .wasm) 和精简后的 resources/v1/ 会放到 web/。
# 不再生成百兆级 .data：纹理按文件由浏览器解码，字体在 main() 前写入 MEMFS。
# 本地预览:
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

# ─── 精简 web 资源 (8K→4K JPEG, CJK 字体子集) ────────────────────────────────
echo ""
echo "[assets] 生成 web/resources/v1/ (Pillow + fonttools)..."
if ! python3 -c "import PIL, fontTools" 2>/dev/null; then
  echo "[info] 安装 pillow / fonttools ..."
  python3 -m pip install --user -q pillow fonttools brotli
fi
python3 tools/prepare_web_assets.py

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
echo "[cmake] 编译 sxwnl_gui ($JOBS 线程, 不再打包 resources/.data)..."
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
for ext in js wasm wasm.map; do
  [[ -f "$OUT_BASE.$ext" ]] && cp -f "$OUT_BASE.$ext" "$WEB_DIR/sxwnl_gui.$ext"
done
# A leftover .data from an older preload build must not be shipped.
rm -f "$WEB_DIR/sxwnl_gui.data"

echo ""
echo "========================================"
echo "  构建完成"
echo "========================================"
echo "  产物目录: $WEB_DIR/"
echo "  wasm/js:  $(du -h "$WEB_DIR/sxwnl_gui.wasm" "$WEB_DIR/sxwnl_gui.js" 2>/dev/null | tr '\n' ' ')"
echo "  资源:     $(du -sh "$WEB_DIR/resources/v1" 2>/dev/null | awk '{print $1}')"
echo "  本地预览 (浏览器不允许直接用 file:// 打开 wasm, 需起一个静态服务器):"
echo "    cd $WEB_DIR && python3 -m http.server 8080"
echo "    然后浏览器打开 http://localhost:8080/"
echo "  发布路径: https://sxwnl.github.io/3d/  (自定义域 https://sx.qaiu.top/3d/)"
echo ""
