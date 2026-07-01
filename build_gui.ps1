# build_gui.ps1 — Windows 自动构建脚本 (MSVC 或 MinGW)
# 用法: .\build_gui.ps1 [-Clean] [-Debug] [-MinGW] [-NoGui]
#
# 执行策略: 若遇到 "无法加载文件..." 错误，先以管理员身份运行:
#   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
param(
    [switch]$Clean,
    [switch]$Debug,
    [switch]$MinGW,
    [switch]$NoGui
)

$ErrorActionPreference = "Stop"
$BuildDir   = "build"
$BuildType  = if ($Debug) { "Debug" } else { "Release" }
$BuildGui   = if ($NoGui) { "OFF" } else { "ON" }
$ScriptDir  = $PSScriptRoot

Write-Host ""
Write-Host "========================================"
Write-Host "  sxwnl-cpp GUI 构建脚本 (Windows)"
Write-Host "  BuildType=$BuildType  GUI=$BuildGui"
Write-Host "========================================"

Set-Location $ScriptDir

# ─── 工具函数 ─────────────────────────────────────────────────────────────────
function Find-VS {
    # 使用 vswhere.exe 查找 Visual Studio 安装路径
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    }
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
              -property installationPath 2>$null
        return $vs
    }
    return $null
}

function Check-cmake {
    try { $v = & cmake --version 2>$null | Select-Object -First 1; return $v } catch { return $null }
}

function Install-Guide-MSVC {
    Write-Host ""
    Write-Host "[提示] 未找到 Visual Studio C++ 编译器。请从以下方式安装:"
    Write-Host "  1. Visual Studio 2022 Community (推荐): https://visualstudio.microsoft.com/"
    Write-Host "     安装时勾选 [使用 C++ 的桌面开发]"
    Write-Host "  2. 或安装 MSYS2 + MinGW: https://www.msys2.org/"
    Write-Host "     然后在 MSYS2 UCRT64 终端运行: pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc"
    Write-Host "     再重新执行: bash build_gui.sh"
    Write-Host ""
}

# ─── 检测 cmake ───────────────────────────────────────────────────────────────
$cmakeVersion = Check-cmake
if (-not $cmakeVersion) {
    # 尝试 VS 自带的 cmake
    $vsPath = Find-VS
    if ($vsPath) {
        $vsCmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        if (Test-Path (Join-Path $vsCmake "cmake.exe")) {
            $env:PATH = "$vsCmake;$env:PATH"
            $cmakeVersion = Check-cmake
        }
    }
}
if (-not $cmakeVersion) {
    # 尝试常见独立安装路径
    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\cmake\bin\cmake.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            $env:PATH = "$(Split-Path $c);$env:PATH"
            $cmakeVersion = Check-cmake
            break
        }
    }
}
if (-not $cmakeVersion) {
    Write-Host "[error] 未找到 cmake。"
    Install-Guide-MSVC
    exit 1
}
Write-Host "[ok] $cmakeVersion"

# ─── 选择生成器 ───────────────────────────────────────────────────────────────
$GeneratorArgs = @()

if ($MinGW) {
    # 用户明确指定使用 MinGW
    $GeneratorArgs = @("-G", "MinGW Makefiles")
    Write-Host "[gen] 使用 MinGW Makefiles"
} else {
    # 优先 Visual Studio
    $vsPath = Find-VS
    if ($vsPath) {
        Write-Host "[gen] 检测到 Visual Studio: $vsPath"
        # cmake 会自动选择最新 VS 生成器, 无需 -G
    } else {
        # 检测 MinGW g++
        $gpp = Get-Command "g++.exe" -ErrorAction SilentlyContinue
        if ($gpp) {
            $GeneratorArgs = @("-G", "MinGW Makefiles")
            Write-Host "[gen] 未找到 VS, 使用 MinGW: $($gpp.Source)"
        } else {
            Write-Host "[error] 未找到 Visual Studio 或 MinGW 编译器。"
            Install-Guide-MSVC
            exit 1
        }
    }
}

# ─── 清理 ──────────────────────────────────────────────────────────────────────
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[info] 清理 $BuildDir ..."
    Remove-Item -Recurse -Force $BuildDir
}
if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }

# ─── CMake 配置 ───────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "[cmake] 配置中 (首次运行将下载 glfw / imgui / glew, 需要网络)..."

$ConfigArgs = @("-B", $BuildDir) + $GeneratorArgs + @(
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DSXWNL_BUILD_GUI=$BuildGui"
)
& cmake @ConfigArgs
if ($LASTEXITCODE -ne 0) { Write-Host "[error] cmake 配置失败"; exit 1 }

# ─── 编译 ──────────────────────────────────────────────────────────────────────
$Jobs = [System.Environment]::ProcessorCount
Write-Host ""
Write-Host "[cmake] 编译 ($Jobs 线程)..."

& cmake --build $BuildDir --config $BuildType --parallel $Jobs
if ($LASTEXITCODE -ne 0) { Write-Host "[error] 编译失败"; exit 1 }

# ─── 结果 ──────────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "========================================"
Write-Host "  构建完成"
Write-Host "========================================"

$guiBin = "$BuildDir\$BuildType\sxwnl_gui.exe"
if (-not (Test-Path $guiBin)) { $guiBin = "$BuildDir\sxwnl_gui.exe" }

$consoleBin = "$BuildDir\$BuildType\test1.exe"
if (-not (Test-Path $consoleBin)) { $consoleBin = "$BuildDir\test1.exe" }

if ($BuildGui -eq "ON" -and (Test-Path $guiBin)) {
    Write-Host "  3D GUI:    $guiBin"
    Write-Host "  运行: & '$guiBin'"
}
if (Test-Path $consoleBin) {
    Write-Host "  控制台:    $consoleBin"
    Write-Host "  运行: & '$consoleBin'"
}
Write-Host ""
