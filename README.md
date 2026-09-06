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
| **日月食地面视角** | 站在食带上按真实历算复现全过程：偏食、贝利珠与钻石环、日冕、天光变暗与地平圈霞光；月食为月面掠过地本影 |
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

### WebAssembly（本仓库 GitHub Pages）

浏览器预览走 Emscripten + WebGL2。构建产物推到独立分支 **`gh-pages`** 的 `web/` 目录（默认 `GITHUB_TOKEN`，不调用 Pages API）。在仓库 Settings → Pages 里选：

- Source: **Deploy from a branch**
- Branch: **`gh-pages`** / folder **`/`**

站点落在（`sx.qaiu.top` 已是组织站点域名，项目站自动进子路径；**本仓库不要加 CNAME**）：

- https://sxwnl.github.io/sxwnl-cpp-3d/web/
- https://sx.qaiu.top/sxwnl-cpp-3d/web/

仓库里的 `resources/` 约 **144MB**（桌面/Android 用的 8K JPEG + 16MB CJK 字体），几乎全是已编码文件，xz 压不动。Web 构建**不会**把这棵树打进 `.data` 或整包上传 Pages，而是现场生成精简树（约 **18MB**）：

| 资源 | 做法 |
|------|------|
| 行星/月球贴图 | 8K→最长边 4K，连续色调重编码为 JPEG q85；土星环等带 alpha 的仍用 PNG。每张独立相对 URL，浏览器 `createImageBitmap` 解码后直接 `texImage2D` |
| CJK / 星座字体 | 按源码用到的字符子集化（约 1MB），`main()` 之前写入 MEMFS。ImGui 图集建设不能异步等字体 |
| OBJ / `world_b.bin` | 原样放到 `web/resources/v1/`，按需 fetch 进 MEMFS |
| wasm / js | 只含代码（约 1.6MB + 150KB），没有百兆 `.data` |

贴图 URL 必须是相对路径（`resources/v1/...`），不能写成 `/resources/...`，否则项目站 `/sxwnl-cpp-3d/` 会打到 `sxwnl.github.io` 根路径。

```bash
# 需已激活 emsdk，并安装 pillow / fonttools
bash build_wasm.sh              # 产物在 web/
cd web && python3 -m http.server 8080
```

工作流 [`.github/workflows/deploy-pages.yml`](.github/workflows/deploy-pages.yml) 在 `master` / `workflow_dispatch` 上把 `web/` 推到 `gh-pages`。PR 只构建、不推分支。

EdgeOne 跟主站同一套即可：源站 `sxwnl.github.io`，回源 HOST `sx.qaiu.top`，HTTPS。按 path 分缓存：

- `/sxwnl-cpp-3d/web/resources/*`、`/sxwnl-cpp-3d/web/*.wasm`、`*.js` → 强制 30 天
- `/sxwnl-cpp-3d/web/` 与 `index.html` → 60s
- 若需要 COOP/COEP，加在 `/sxwnl-cpp-3d/web/*`

换贴图把目录前缀 `v1/` 改成 `v2/`。

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
| 太阳系视图·轨道视角 | 选中日月食后，本影/半影锥、地面阴影与界线图直接画进太阳系；“光影几何”可关 |
| 太阳系视图·地面视角 | 同一视口内切换到食带上的地面观测：拖动看四周、滚轮缩放视场、双击归位；“中心线观测”把观测点放在食甚中心线上 |

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
├── test/         控制台、日月食、地面视角及时间控制回归测试
├── gui/          图形界面（本次新增）
│   ├── main.cpp         GLFW + GL + ImGui 主循环
│   ├── scene.{h,cpp}    太阳系模型 + 引擎对接
│   ├── renderer.{h,cpp} OpenGL FBO + 球体 + 轨道 + 着色器
│   ├── panels.{h,cpp}   ImGui 面板（控制/历法/星历/日月食/3D 光锥）
│   ├── camera.h         轨道相机
│   ├── gles/            Android 专用 GLES 3 兼容层
│   └── mathx.h          内联 vec3/mat4
├── android/       NativeActivity + Gradle APK 工程
├── web/           Wasm 页面壳 (shell.html)；构建脚本写入 index.html / resources/v1/
├── tools/prepare_web_assets.py   4K JPEG + CJK 字体子集
├── build_gui.sh   Linux/macOS/MSYS2 构建脚本
├── build_gui.ps1  Windows PowerShell 构建脚本
├── build_wasm.sh  Emscripten 构建（无 .data 预加载）
├── build_wasm.ps1 Windows 下的 wasm 构建
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

### v1.7.0

**晨昏线与光照**

- 光照改在线性空间里算：贴图先解码再乘余弦，最后编码回 sRGB。此前把余弦直接乘进
  gamma 编码的贴图里，太阳高度 15° 的地面本该显示为 0.54，却压到了 0.20——晨昏线往里
  三十度的白昼被画成了黑夜。同时去掉了额外的 `pow(diff, 1.18)`。
- 太阳改为平行光，方向取自天体中心。逐片元点光源在压缩过的场景尺度下只照亮 87° 球冠，
  现在是完整半球。土星环同改。
- 新增大气项 `uAtmo`：折射把太阳抬高 0.57°，地面在几何晨昏线之后仍受光；晨昏蒙影从地平
  线下 18° 起，贴地暖橙、渐深转蓝。地球 1.0、金星 0.7、火星 0.35、气态巨行星 0.25，
  水星与月球为 0，保持无大气天体应有的刀切终结线。
- 大气壳按太阳方向调制，不再整圈发光——蓝晕越过晨昏线一点点才熄。

**日月食研究视图**

- 「光影几何」现在把轨道视角变成一次日月食的专用研究视图：隐去其余天体、轨道、
  引力网格、小行星带与太阳本体。
- 月球放回**真实光轴**上。此前它待在观赏用的偏移位置（3.2 个地球半径，真实为 60），
  画出的影锥偏离真实光轴 7°–17°；现在只压缩沿轴距离，轴线穿过地心的垂足原样保留，
  实测偏差降到 **0.1° 以内**。月球尺寸也改用真实比例，影锥张角随之正确。
- 新增轨道模型：月球轨道（实线，采样一个恒星月的真实地心方向）、黄道（虚线，同半径）、
  两个交点（黄色十字）。5.1° 的倾角和"日月食为何只发生在交点附近"由此可见。
- 相机自动就位：正侧对光轴、向太阳侧摆 36°、抬高 20°，取景按视口宽高比自适应。
- 月食同样支持：地本影按引擎的角半径定尺寸，本影/半影在月球处的真实大小是画出来的那个。

**天体信息卡**

- 拖动不再被卡片打断。卡片不再占用任何 ImGui 控件，只有原地按下抬起才算点击，
  手指扫过卡片时相机照常旋转——移动端此前拖动会在卡片上断掉。
- 字号整体下调一档，时间戳再下调一档。
- 天象时刻补上年份（`01-03` → `2027-01-03`），时区在卡片标题里标一次。
- 日月食进行时，卡片下半改为显示该次食的过程：类型、食分、中心持续、带宽、中心点
  （月食为本影/半影与月径之比），以及各食阶时刻；当前所处食阶高亮，点任一行跳到该时刻。

**修复**

- `glad.h` 里 `GL_STATIC_DRAW`/`_READ`/`_COPY` 三个常量抄错（`0x88B4-B6`，应为
  `0x88E4-E6`）。Windows/Linux 驱动容忍，Apple 的 GL-over-Metal 严格返回
  `GL_INVALID_ENUM` 且不分配显存，留下 0 字节缓冲区，一画就段错误——macOS 上星空、
  回退球体与大气辉光的三处 `__APPLE__` 绕行都源于此。

### v1.6.1

**日月食演示的两个观测点，与退出**

- 演示控件加「退出演示」：时钟拿回自己的速率并暂停，视口回到普通太阳系。此前从演示
  里出来，速率要么停在食的爬行速度上，要么跳回每秒五天——后者会让地球在昼夜之间闪。
- 观测点由一个开关改成两个可切换的点：**本地观测**（日月食页里设的经纬度）与
  **最佳观测点**。日食的最佳点是食甚中心线上那一点；月食此前没有对应概念，进地面视角
  常常提示"月亮在地平线下"，现在取**月下点**（食甚时月亮当顶的地方），月食因此总能看见。
- 顺带修正取景：视线高度角原先钳在 85°，月亮当顶时它会挂在画面外；现放宽到 89.8°。
- 天体在地平线下时的提示改为指路——"此地看不到，试试最佳观测点"。

**手机端**

- 日月食搜索结果不再是页面里的滚动框中框：嵌套滚动要靠手指落点猜该滚哪一层，现在列表
  直接排在页面里，跟着页面滚，行高也够按。
- 触摸端滚动条按界面缩放加粗（此前是 13 物理像素，在手机上约一毫米半）。
- 视口右上的日月食控件与左上的时间徽章不再叠在一起：控件右对齐并自动换行，一行放不下
  就落到徽章下面，地面视角的读数也随之下移。

**动画**

- 底部导航的高亮在两个入口之间滑动，而不是在新入口下重新出现。
- 换页淡入、月历换月按来向滑入，都在 0.2 秒左右——看得见，等不着。

**桌面端**

- 侧栏分隔条中间加了圆点手柄，并加宽命中区：三像素的线要先知道它是个可拖的东西，
  才谈得上对准它。

### v1.6.0

**日月食的地面视角**

- 太阳系视图里选中一次日月食，右上角多出"轨道视角／地面视角"。地面视角把观测者放到
  地面上，按同一套历算画出那一刻的天空：日食是日面被月面一点点吃掉、贝利珠与钻石环、
  全食的日冕与色球，天光随之暗下去、地平圈一周泛起霞光；月食是月面缓缓滑进地本影，
  影内不是黑的而是铜红色（地球大气把红光折进自己的影子里）。两种视角在同一个视口里
  切换，不是两个页面。
- 位置与几何都取真值：日月的地平坐标、视半径与视差修正来自 `MSC`，两轮相对位置用不含
  蒙气差的地平方向做切平面投影，因此屏幕上的重叠与读出的食分是同一件事。天空用日心
  投影（gnomonic），大圆在屏幕上仍是直线，地平线因此是一条水平线，方位刻度可直接标在
  上面。
- 新增引擎层 `eph/sky_view.{h,cpp}`（`computeSkyView`），与平台无关；`test/sky_view_test.cpp`
  拿它与贝塞尔要素解出的结果对表：食甚食分与地方食况一致、初亏/复圆时刻两轮正好相切
  （<0.003°）、月食本影食分与搜索结果一致（<0.002）。
- "中心线观测"把观测点固定在食甚中心线上，而不是让它随影子移动——跟着影子走的人始终
  在全食里，永远看不到食既前后的过程。

**日月食光影画进太阳系**

- 选中日月食后，本影/半影锥、落在地球上的影子与完整界线图直接叠加在太阳系场景里，
  月食时月面按本影深度转成铜红。影子的位置和大小取自引擎的中心线与南北界，只有连回
  月球的那截锥体是示意的。

**点击天体看参数与将要发生的事**

- 3D 视图里点行星（现在也能点月球）会在天体旁弹出一块半透明的读数，除轨道参数外还列出
  接下来几件天象：大距、合、冲、顺留逆留、合月、日月食、凌日等；点其中一条即把时钟带到
  那一刻。留的方向由赤经曲率判定，凌日在未来 130 年内单独搜索并留出固定席位。

**Web 版**

- 发布 WebAssembly 预览版（`build_wasm.sh` / GitHub Pages），修正 HiDPI 文字比例、双指缩放、
  手机端点击落点与滚轮缩放，字体子集扩到 GB 2312。

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
