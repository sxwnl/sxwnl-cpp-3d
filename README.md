# sxwnl-cpp — 寿星天文历 C++ 版

<img width="1920" height="1058" alt="image" src="https://github.com/user-attachments/assets/05b3124b-cd10-45d9-b10f-fcb07971c1e1" />
<img width="453" height="778" alt="image" src="https://github.com/user-attachments/assets/90e04463-cf5d-4f7a-992e-9d6b230a37f5" />
<img width="445" height="800" alt="image" src="https://github.com/user-attachments/assets/91d3737f-ff24-4260-b0c4-b5114ba57139" />



寿星天文历 v5.x JS 版的 C++ 移植。当前发行版本为 **v1.5.2**，包含：
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
| 日月食 | 日食/月食搜索、地方食况、食阶段、中心线及本影/半影界线 |
| **3D 日月食** | 可旋转地球食带与阴影、完整界线图、月球穿越地影、太阳—地球—月球三体光锥动画 |
| **3D 太阳系** | 日心轨道（VSOP87/冥王星）、光照球体、行星自转与转轴、实时运行参数 |

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

### Android APK

Android 版使用 NativeActivity、Dear ImGui Android 后端和独立的 GLES 3
渲染模块，不会引入或改变桌面端的 GLFW/GLAD 渲染路径。

```bash
cd android
gradle :app:assembleRelease
```

需要 Android SDK 35、NDK 27.3 和 CMake 3.31.5。APK 输出到
`android/app/build/outputs/apk/release/`。本地未提供签名参数时会生成
`app-release-unsigned.apk`；发布工作流要求配置以下 GitHub Actions secrets，
并只发布已签名的 `app-release.apk`：

- `ANDROID_KEYSTORE_BASE64`：发布 keystore 的 Base64 编码
- `ANDROID_KEYSTORE_PASSWORD`
- `ANDROID_KEY_ALIAS`
- `ANDROID_KEY_PASSWORD`

Android 版通过一个极简 `NativeActivity` Java 入口加载
`libs/*/libsxwnl_android.so`；业务与渲染代码仍全部使用 C++。
桌面端仍按平台和架构分别打包。

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
| 控制面板 | 播放/暂停、固定 UTC 时区（默认北京/上海 UTC+8）、跳转本地日期 |
| 速度预设 | 每真实秒推进 x 秒、x 小时或 x 日，负值可倒放 |
| 控制面板 | 切换线性/对数距离压缩、行星大小 |
| 参数面板 | 点击行星行查看详细星历 |
| 日月食面板 | 搜索事件、查看食分/阶段/地方食况，跳到食甚或从食始播放 |
| 3D 食影 | 拖动旋转地球仪；切换“三体光锥”观察本影和半影空间关系 |

**中文字体**：优先使用 `resources/fonts/NotoSansCJKsc-Regular.otf`，其次自动检测 `msyh.ttc`（微软雅黑）
等系统字体；若无则回退英文默认字体（汉字显示为方块）。星座与行星符号（♈-♓、☿♀♂ 等）来自
`resources/fonts/NotoSansSymbols-Astro.ttf`，以合并字体源的方式并入同一图集。

---

## 项目结构

```
sxwnl-cpp/
├── eph/          天文历算（VSOP87、冥王星、章动、结构化日月食搜索与路径…）
├── lunar/        农历、节气、气朔、八字
├── mylib/        工具库（mystl = std::string 别名 + 容器 + 数学）
├── test/         控制台、日月食及时间控制回归测试
├── gui/          图形界面（本次新增）
│   ├── main.cpp         GLFW + GL + ImGui 主循环
│   ├── scene.{h,cpp}    太阳系模型 + 引擎对接
│   ├── renderer.{h,cpp} OpenGL FBO + 球体 + 轨道 + 着色器
│   ├── panels.{h,cpp}   ImGui 面板（控制/历法/星历/日月食/3D 光锥）
│   ├── camera.h         轨道相机
│   ├── gles/            Android 专用 GLES 3 兼容层
│   └── mathx.h          内联 vec3/mat4
├── android/       NativeActivity + Gradle APK 工程
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
| Android 图形接口 | GLES 3.0 | NDK 系统库（仅 Android 构建） |

天文引擎本身**零外部依赖**（`mystl::string` 等均为自实现）。

---

## 更新日志

### v1.5.2

**节日的起始年份**

- 节日表原先大多没有年限，1900 年的日历上会出现"辛亥革命纪念日""世界精神卫生日"这类
  当时还不存在的条目。现为公历表 117 条、周序表 12 条全部补上起始年份，并给按周计算的
  节日加上与公历表相同的年限解析；农历节日中受封、朝廷定制、现代立法这三类能考据到
  具体年份的（妈祖诞 987、天贶节 1008、关帝诞 1594、小年 1644、重阳敬老 1989 等）也一并
  设了年限，其余岁时习俗起源不可考，不设年限。
- 逐条的年份与依据记在 [`docs/festival-origins.md`](docs/festival-origins.md)，含存疑条目的说明
  （如 3 月 14 日"国际警察日"公安部曾辟谣、10 月 6 日"老人节"无出处已删除）。

**星座符号**

- `星座: 天秤座??` 是因为 Noto Sans CJK 没有 ♈-♓ 这段码位，星座符号与其后的变体选择符
  都落到了缺字符号上。现打包 5 KB 的 Noto Sans Symbols 子集（U+263D-U+2653、U+26E2，
  含日月、五星与黄道十二宫）合并进字体图集，并去掉数据里的变体选择符。

**双指缩放文字**

- 太阳系以外的页面，双指开合缩放界面文字（太阳系页仍是缩放 3D 视角），缩放时屏幕中央
  显示当前比例。文字大小范围放宽到 0.6x-2.2x，农历页由"取消全局放大"改为"相对全局
  小一档"，因此也能跟着缩放。

**复制查询结果**

- 星历、节气朔望、八字升降、日月食（单次详情与整个搜索结果）、黄历当天详情各加一个
  复制按钮，把面板上的内容按原样送到系统剪贴板；Android 经 JNI 接 `ClipboardManager`，
  ImGui 的 Android 后端本身没有剪贴板。

**日历排版**

- 星期表头与日格内的日期数字原先差 6 像素，看起来整列错开，现改为同一个缩进。
- 周六、周日两列换用略暖、略亮的底色，弱到不与"今天"和选中日争视线。

### v1.2.0

**日食界线图补全**

- 启用 JS 原版 `rsGS.jieX()` 的完整界线计算（此前在 `eph_rsgs.cpp` 中被整段注释，原注为"暂时还没有作图工具"）。现可输出 13 条曲线：中心线、本影/半影南北界、0.5 半影界、日出日没食甚线，以及**初亏／复圆界线闭合环**（界线图上的两个"椭圆"）。
- 移植中修正两处 JS 直译隐患：`Ua = re.q3` 在 JS 中是改指向而 C++ 引用赋值会变成复制内容（改用指针）；`elmCpy` 中 `a.size()-2` 在空表上会让无符号数回绕并越界写（补边界约束）。另修正未初始化的 `_FLAG F6`。
- 配色沿用原版 `vml.js`：界线与闭合环为红，0.5 半影界为绿。

**3D 渲染方向校准**

- **网格轴系标定**：各天体网格自带任意本地坐标系，此前仅地球有一个 `rotateZ(-125.93°)` 的经验修正，且该形式只能修平面内扭转、修不了极轴。现按各网格自身顶点/UV 数据实测出完整旋转阵，把网格送入统一地理系（+Y 北极、+Z 本初子午线、+X 东经 90°）。偏差量：地球 54°、月球 79°、土星 26.5°、木星 4.5°（后者足以让云带随自转上下漂移）；其余网格极轴本就在 +Y 上，仅差经度相位。
- **世界坐标系手性**：`toWorld` 原将黄道系映射为 `(x,z,y)`，是行列式 −1 的**反射**——整个太阳系以镜像绘制，导致贴图与自转无法同时正确。改为 `(x,z,−y)` 的正交映射。
- 地球自转原为 `-(gmst)+180`，gmst 系数为负，即**反向自转**。以"太阳直射点须正对太阳"为判据重解得 `spin = gmst + 90°`、倾角 `−ε`，2025–2030 实测最大误差 0.020°。
- 日食 3D 地球仪：界线与路径本已在地理系中，此前误与网格同乘标定阵，导致相对错位不变。现仅网格乘标定阵。
- 太阳标记由固定屏幕坐标改为按真实**太阳直射点**定位，随视角转动。
- 行政区界此前仅在纹理模式下绘制，2D 回退视图无此图层；现两种模式共用同一份经纬度数据。

**行星自转**

- sxwnl 星历为纯位置星历，不含自转要素。新增 IAU WGCCRE 自转要素（α₀、δ₀、W₀、Ẇ）覆盖太阳、八大行星、冥王星与月球，并推广模型矩阵为 `rotateY(node)·rotateX(tilt)·rotateY(spin)`，可表达任意极轴指向（天王星倒转、金星/天王星逆行等自动落位）。
- 转轴指示线改为对所有具自转要素的天体绘制。
- 月球加入同步自转（IAU W 速率 13.176°/日，对应 27.32 日恒星月）。
- 自转周期实测与公认值全部吻合；地球因 IAU 线性拟合不含岁差章动（残差 0.73°），仍保留精度更高的恒星时解。

**时间控制**

- 顶部播放状态旁显示当前倍速，按数值大小自动选用秒/分/小时/日/年。
- 日食演示速度改为按该次食的实际时长自适应（首尾接触约 45 秒走完），不再固定 1 分/秒；同时同步侧栏速度预设，避免显示与实际不符、以及演示结束后预设错乱。

### 更早

见 git 提交历史。

---

## 说明

本工程核心移植工作大多在手机端用 [C4droid](https://blog.qaiu.top/archives/c4droid) 完成，代码格式较随意，但经多次迭代后精度与 JS 版完全一致，计算效率远高于 JS 版。
