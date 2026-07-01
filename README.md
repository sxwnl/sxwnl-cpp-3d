# sxwnl-cpp — 寿星天文历 C++ 版

寿星天文历 v5.x JS 版的 C++ 移植，包含：
- **完整天文历算引擎**：农历万年历、节气、日月食、行星星历、八字、升降等
- **3D 太阳系可视化 GUI**（新）：基于 Dear ImGui + OpenGL，轨道与行星实时运行参数

在线参考版：<https://sx.qaiu.top>

---

## 功能

| 模块 | 说明 |
|------|------|
| 农历万年历 | 公农回三合历，节气、月相、节日 |
| 行星星历 | 视黄经/赤经、距离、方位角、高度角（水星→冥王星+月亮） |
| 节气与朔望 | 24 节气精确时刻 + 全年朔望表 |
| 八字与升降 | 四柱八字、真太阳时、日月出没 |
| 日月食 | 路径图及界线计算 |
| **3D 太阳系** | 日心轨道（VSOP87/冥王星）、光照球体、实时运行参数 |

---

## 构建

### 方式一：一键脚本（推荐）

**Windows（PowerShell）**

```powershell
# 首次需要设置执行策略（管理员 PowerShell 运行一次）:
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

# 构建（自动检测 Visual Studio 或 MinGW）
.\build_gui.ps1

# 可选参数:
.\build_gui.ps1 -Clean        # 先清理再构建
.\build_gui.ps1 -Debug        # Debug 模式
.\build_gui.ps1 -MinGW        # 强制 MinGW 而非 VS
.\build_gui.ps1 -NoGui        # 只构建控制台 test1
```

**Linux / macOS / MSYS2**

```bash
bash build_gui.sh              # 自动安装依赖 + 构建
bash build_gui.sh --clean      # 先清理再构建
bash build_gui.sh --no-gui     # 只构建控制台 test1
bash build_gui.sh --debug      # Debug 模式
```

脚本会：
1. 检测 cmake 与编译器，未找到时提示安装
2. Linux/Debian 自动 `apt install` GL/X11 开发包
3. 通过 **CMake FetchContent** 自动下载并编译 GLFW、GLEW、Dear ImGui（首次需联网，之后走缓存）
4. 输出 `build/sxwnl_gui`（GUI）和 `build/test1`（控制台）

---

### 方式二：手动 CMake

**前置条件**

| 平台 | 工具 |
|------|------|
| Windows | [Visual Studio 2022](https://visualstudio.microsoft.com/)（勾选 *使用 C++ 的桌面开发*）或 [MSYS2](https://www.msys2.org/) + MinGW-w64 |
| Ubuntu/Debian | `sudo apt install cmake build-essential libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev` |
| Arch | `sudo pacman -S cmake gcc mesa libx11 libxrandr libxinerama libxcursor libxi` |
| macOS | `brew install cmake` + Xcode CLI Tools |

**构建命令**

```bash
# 配置 (首次下载依赖约需 1~3 分钟)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build --config Release -j$(nproc)

# Windows MSVC (Developer PowerShell / cmd)
cmake -B build
cmake --build build --config Release
```

**只构建控制台版（无 GL 依赖）**

```bash
cmake -B build -DSXWNL_BUILD_GUI=OFF
cmake --build build
```

---

### 方式三：make（控制台版，与原版兼容）

```bash
make          # 编译 test1 (Linux/Mac/MSYS2)
make clean    # 清理
make cleanw   # 清理（Windows cmd）
```

---

## 运行

```bash
# GUI（3D 太阳系 + 面板）
./build/sxwnl_gui        # Linux/macOS
build\Release\sxwnl_gui  # Windows MSVC
build\sxwnl_gui          # Windows MinGW

# 控制台演示
./build/test1
```

### GUI 操作

| 操作 | 说明 |
|------|------|
| 左键拖拽 | 旋转相机（方位 + 俯仰） |
| 右键拖拽 | 平移相机 |
| 滚轮 | 缩放 |
| 控制面板 | 播放/暂停、速度倍率、跳转日期 |
| 控制面板 | 切换线性/对数距离压缩、行星大小 |
| 参数面板 | 点击行星行查看详细星历 |

**中文字体**：自动检测 `msyh.ttc`（微软雅黑）等系统字体；若无则回退英文默认字体（汉字显示为方块）。

---

## 项目结构

```
sxwnl-cpp/
├── eph/          天文历算（VSOP87、冥王星、章动、日月食…）
├── lunar/        农历、节气、气朔、八字
├── mylib/        工具库（mystl = std::string 别名 + 容器 + 数学）
├── test/         控制台测试入口 (test1.cpp)
├── gui/          图形界面（本次新增）
│   ├── main.cpp         GLFW + GL + ImGui 主循环
│   ├── scene.{h,cpp}    太阳系模型 + 引擎对接
│   ├── renderer.{h,cpp} OpenGL FBO + 球体 + 轨道 + 着色器
│   ├── panels.{h,cpp}   ImGui 面板（控制/参数/日历/星历/节气/八字）
│   ├── camera.h         轨道相机
│   └── mathx.h          内联 vec3/mat4
├── build_gui.sh   Linux/macOS/MSYS2 构建脚本
├── build_gui.ps1  Windows PowerShell 构建脚本
├── autobuild.sh   原有 cmake 构建（控制台版）
└── CMakeLists.txt
```

---

## 依赖（GUI）

| 库 | 版本 | 获取方式 |
|----|------|---------|
| [GLFW](https://www.glfw.org/) | 3.4 | CMake FetchContent 自动下载 |
| [Dear ImGui](https://github.com/ocornut/imgui) | docking 分支 | CMake FetchContent 自动下载 |
| GL 函数加载器 | — | ImGui 内置 `imgui_impl_opengl3_loader.h`，无需额外依赖 |

天文引擎本身**零外部依赖**（`mystl::string` 等均为自实现）。

---

## 说明

本工程核心移植工作大多在手机端用 [C4droid](https://blog.qaiu.top/archives/c4droid) 完成，代码格式较随意，但经多次迭代后精度与 JS 版完全一致，计算效率远高于 JS 版。
