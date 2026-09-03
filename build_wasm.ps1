# build_wasm.ps1 — 将 sxwnl_gui 编译为 WebAssembly 前端预览项目 (Emscripten)
# 用法: .\build_wasm.ps1 [-Clean] [-Debug]
#
# 前置条件: 已安装并激活 emsdk (https://emscripten.org/docs/getting_started/downloads.html)
#   git clone https://github.com/emscripten-core/emsdk.git
#   cd emsdk; .\emsdk install latest; .\emsdk activate latest
#   .\emsdk_env.ps1        # 每个新终端都要执行一次，把 emcc/emcmake 加入 PATH
#
# 构建产物 (index.html / .js / .wasm) 和精简后的 resources/v1/ 会放到 web/。
# 不再生成百兆级 .data：纹理按文件由浏览器解码，字体在 main() 前写入 MEMFS。
#   cd web; python -m http.server 8080
param(
    [switch]$Clean,
    [switch]$Debug
)

$ErrorActionPreference = "Stop"
$BuildDir  = "build_wasm"
$WebDir    = "web"
$BuildType = if ($Debug) { "Debug" } else { "Release" }
$ScriptDir = $PSScriptRoot

Write-Host ""
Write-Host "========================================"
Write-Host "  sxwnl-cpp WebAssembly 构建脚本"
Write-Host "  BuildType=$BuildType"
Write-Host "========================================"

Set-Location $ScriptDir

# ─── 检测 emcmake / emcc ─────────────────────────────────────────────────────
$emcmake = Get-Command "emcmake" -ErrorAction SilentlyContinue
$emcc    = Get-Command "emcc" -ErrorAction SilentlyContinue
if (-not $emcmake -or -not $emcc) {
    Write-Host "[error] 未找到 emcmake/emcc，请先安装并激活 emsdk:"
    Write-Host "  git clone https://github.com/emscripten-core/emsdk.git"
    Write-Host "  cd emsdk; .\emsdk install latest; .\emsdk activate latest"
    Write-Host "  .\emsdk_env.ps1   # 每个新终端都需要重新执行一次"
    exit 1
}
Write-Host "[ok] $(& emcc --version | Select-Object -First 1)"

# ─── 清理 ──────────────────────────────────────────────────────────────────────
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[info] 清理 $BuildDir ..."
    Remove-Item -Recurse -Force $BuildDir
}
if (-not (Test-Path $WebDir)) { New-Item -ItemType Directory -Path $WebDir | Out-Null }

# ─── 精简 web 资源 (8K→4K JPEG, CJK 字体子集) ────────────────────────────────
Write-Host ""
Write-Host "[assets] 生成 web/resources/v1/ (Pillow + fonttools)..."
$pyCheck = python -c "import PIL, fontTools" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[info] 安装 pillow / fonttools ..."
    python -m pip install -q pillow fonttools brotli
}
python tools/prepare_web_assets.py
if ($LASTEXITCODE -ne 0) { Write-Host "[error] web 资源生成失败"; exit 1 }

# ─── CMake 配置 ───────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "[cmake] 配置中 (首次运行将下载 imgui / stb, 需要网络)..."
& emcmake cmake -B $BuildDir `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DSXWNL_BUILD_GUI=ON" `
    "-DSXWNL_BUILD_TESTS=OFF"
if ($LASTEXITCODE -ne 0) { Write-Host "[error] cmake 配置失败"; exit 1 }

# ─── 编译 ──────────────────────────────────────────────────────────────────────
$Jobs = [System.Environment]::ProcessorCount
Write-Host ""
Write-Host "[cmake] 编译 sxwnl_gui ($Jobs 线程, 不再打包 resources/.data)..."
& cmake --build $BuildDir --config $BuildType --target sxwnl_gui --parallel $Jobs
if ($LASTEXITCODE -ne 0) { Write-Host "[error] 编译失败"; exit 1 }

# ─── 拷贝产物到 web/ ───────────────────────────────────────────────────────────
$OutBase = "$BuildDir\sxwnl_gui"
if (-not (Test-Path "$OutBase.html")) { $OutBase = "$BuildDir\$BuildType\sxwnl_gui" }
if (-not (Test-Path "$OutBase.html")) {
    Write-Host "[error] 未找到编译产物 sxwnl_gui.html"; exit 1
}

Write-Host ""
Write-Host "[copy] 拷贝构建产物到 $WebDir\ ..."
Copy-Item "$OutBase.html" "$WebDir\index.html" -Force
foreach ($ext in @("js", "wasm", "wasm.map")) {
    $f = "$OutBase.$ext"
    if (Test-Path $f) { Copy-Item $f "$WebDir\sxwnl_gui.$ext" -Force }
}
Remove-Item "$WebDir\sxwnl_gui.data" -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "========================================"
Write-Host "  构建完成"
Write-Host "========================================"
Write-Host "  产物目录: $WebDir\"
Write-Host "  本地预览 (浏览器不允许直接用 file:// 打开 wasm, 需起一个静态服务器):"
Write-Host "    cd $WebDir; python -m http.server 8080"
Write-Host "    然后浏览器打开 http://localhost:8080/"
Remove-Item "$WebDir\CNAME" -ErrorAction SilentlyContinue
New-Item -ItemType File -Path "$WebDir\.nojekyll" -Force | Out-Null
Write-Host "  发布路径: https://sxwnl.github.io/sxwnl-cpp-3d/  ->  https://sx.qaiu.top/sxwnl-cpp-3d/"
Write-Host ""
