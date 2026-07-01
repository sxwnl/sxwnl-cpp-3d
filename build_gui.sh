#!/usr/bin/env bash
# build_gui.sh — 自动安装依赖并构建 sxwnl_gui (图形化3D太阳系)
# 支持: Ubuntu/Debian · Arch/Manjaro · MSYS2/MinGW · Cygwin/MobaXterm · macOS
# 用法: bash build_gui.sh [--no-gui] [--clean] [--release|--debug]
set -e

# ─── 参数 ───────────────────────────────────────────────────────────────────
BUILD_GUI=ON
BUILD_TYPE=Release
CLEAN=0

for arg in "$@"; do
  case $arg in
    --no-gui)  BUILD_GUI=OFF ;;
    --clean)   CLEAN=1 ;;
    --debug)   BUILD_TYPE=Debug ;;
    --release) BUILD_TYPE=Release ;;
  esac
done

BUILD_DIR="build"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo ""
echo "========================================"
echo "  sxwnl-cpp GUI 构建脚本"
echo "  BUILD_GUI=$BUILD_GUI  TYPE=$BUILD_TYPE"
echo "========================================"

# ─── 平台检测 ─────────────────────────────────────────────────────────────────
OS="$(uname -s)"

is_msys2=0; is_cygwin=0; is_linux=0; is_mac=0

if   [[ "$OS" == MINGW* ]] || [[ "$OS" == MSYS* ]] || [[ -n "$MSYSTEM" ]]; then
  is_msys2=1
elif [[ "$OS" == CYGWIN* ]]; then
  is_cygwin=1
elif [[ "$OS" == "Linux" ]]; then
  is_linux=1
elif [[ "$OS" == "Darwin" ]]; then
  is_mac=1
fi

echo "[env] OS=$OS  msys2=$is_msys2  cygwin=$is_cygwin  linux=$is_linux  mac=$is_mac"

# ─── 依赖安装 ─────────────────────────────────────────────────────────────────
install_deps() {
  if [[ $is_msys2 -eq 1 ]]; then
    local prefix="mingw-w64-ucrt-x86_64"
    [[ "$MSYSTEM" == "MINGW32" ]] && prefix="mingw-w64-i686"
    [[ "$MSYSTEM" == "MINGW64" ]] && prefix="mingw-w64-x86_64"
    echo "[deps] MSYS2 ($MSYSTEM): pacman install toolchain + cmake + ninja..."
    # toolchain 包组包含 gcc/g++/gdb/make/binutils 等完整工具链
    pacman -S --needed --noconfirm \
      ${prefix}-toolchain ${prefix}-cmake ${prefix}-ninja || true

  elif [[ $is_cygwin -eq 1 ]]; then
    echo "[deps] Cygwin/MobaXterm: 安装 X11/OpenGL 全套开发包..."
    # GLFW X11 后端需要: X11 基础库 + Xcursor + Xrandr + Xinerama + Xi + Xext + Xxf86vm
    # OpenGL 需要: libGL-devel + xorg-x11-xwin-gl (Cygwin XWin 的 GL 运行时)
    CYGS="libX11-devel libXcursor-devel libXrandr-devel libXinerama-devel libXi-devel libXext-devel libXxf86vm-devel libGL-devel xorg-x11-xwin-gl"
    if command -v apt-cyg &>/dev/null; then
      # shellcheck disable=SC2086
      apt-cyg install $CYGS || true
    else
      echo "[warn] apt-cyg 未找到，请在 Cygwin setup.exe / MobaXterm Packages 中安装:"
      echo "         $CYGS"
      echo ""
      echo "[推荐] 改用 MSYS2 UCRT64 (原生 Windows exe，无需 X11，更简单):"
      echo "         https://www.msys2.org/"
      echo "       pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc"
    fi

  elif [[ $is_linux -eq 1 ]]; then
    if [[ -f /etc/debian_version ]]; then
      echo "[deps] Debian/Ubuntu: apt install..."
      sudo apt-get update -qq
      sudo apt-get install -y --no-install-recommends \
        cmake ninja-build build-essential \
        libgl1-mesa-dev libglu1-mesa-dev \
        libx11-dev libxrandr-dev libxinerama-dev \
        libxcursor-dev libxi-dev libxext-dev \
        pkg-config
    elif [[ -f /etc/arch-release ]]; then
      echo "[deps] Arch/Manjaro: pacman install..."
      sudo pacman -S --needed --noconfirm \
        cmake ninja gcc \
        mesa libgl libx11 libxrandr libxinerama libxcursor libxi
    else
      echo "[warn] 未识别发行版，请手动安装: cmake gcc libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev"
    fi

  elif [[ $is_mac -eq 1 ]]; then
    echo "[deps] macOS: brew install..."
    if command -v brew &>/dev/null; then
      brew install cmake ninja || true
    else
      echo "[warn] 未找到 Homebrew: https://brew.sh"
    fi
  fi
}

# GUI 模式下安装依赖 (在 cmake 检测之前)
if [[ "$BUILD_GUI" == "ON" ]]; then
  install_deps
fi

# ─── cmake 检测 ───────────────────────────────────────────────────────────────
if ! command -v cmake &>/dev/null; then
  echo "[error] cmake 未找到"
  if [[ $is_cygwin -eq 1 ]]; then
    echo "        Cygwin: apt-cyg install cmake"
    echo "        或改用 MSYS2: https://www.msys2.org/"
  fi
  exit 1
fi
echo "[ok] $(cmake --version | head -1)"

# Cygwin 额外提示
if [[ $is_cygwin -eq 1 ]] && [[ "$BUILD_GUI" == "ON" ]]; then
  echo ""
  echo "[提示] Cygwin 构建使用 X11 后端，运行时需要 DISPLAY 变量已设置"
  echo "       MobaXterm 本地终端通常已自动设置 DISPLAY=:0"
  echo "       如遇 OpenGL 问题，建议改用 MSYS2 UCRT64 (原生 Windows OpenGL):"
  echo "         https://www.msys2.org/"
  echo ""
fi

# ─── 清理 ─────────────────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 ]] && [[ -d "$BUILD_DIR" ]]; then
  echo "[info] 清理 $BUILD_DIR ..."
  rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

# ─── cmake 配置 ───────────────────────────────────────────────────────────────
GENERATOR_ARGS=()
# Ninja 在各平台均可用且更快; MSVC 环境下跳过 Ninja
if command -v ninja &>/dev/null && [[ $is_cygwin -eq 0 ]]; then
  GENERATOR_ARGS=(-G Ninja)
fi

echo ""
echo "[cmake] 配置 (首次构建会下载 glfw/imgui/glew, 需要网络)..."
cmake -B "$BUILD_DIR" \
  "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSXWNL_BUILD_GUI="$BUILD_GUI"

# ─── 编译 ─────────────────────────────────────────────────────────────────────
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
echo ""
echo "[cmake] 编译 ($JOBS 线程)..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" -j "$JOBS"

# ─── 完成 ─────────────────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo "  构建完成"
echo "========================================"
[[ "$BUILD_GUI" == "ON" ]] && echo "  3D GUI:  ./$BUILD_DIR/sxwnl_gui"
echo "  控制台:  ./$BUILD_DIR/test1"
echo ""
