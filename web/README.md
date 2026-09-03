# WebAssembly 前端

`shell.html` 是 Emscripten 的 `--shell-file`。`build_wasm.sh` / CI 会把编译结果写成同目录的 `index.html`、`sxwnl_gui.js`、`sxwnl_gui.wasm`，并把精简资源放到 `resources/v1/`。

本仓库自建 GitHub Pages（默认 `GITHUB_TOKEN`）：

- https://sxwnl.github.io/sxwnl-cpp-3d/
- 自定义域 https://sx3d.qaiu.top/（`CNAME` 文件，与主站 `sx.qaiu.top` 解耦）

不要把 144MB 的桌面 `resources/` 整包拷进站点。CI 只上传精简后的约 18MB。

## 路径

项目站挂在 `/sxwnl-cpp-3d/`，自定义域在根路径。所有 HTTP fetch / `locateFile` 都相对「当前 URL 的目录部分」，两种都能工作。

```text
resources/v1/planet/tex/8k_earth_daymap.jpg   # 对
/resources/v1/planet/tex/8k_earth_daymap.jpg  # 错：打到 sxwnl.github.io 根
```

MEMFS 内部路径（`/resources/fonts/...`）是 wasm 虚拟盘，不是 HTTP。

## EdgeOne

不要做 `/3d/` 回源路径重写。源站 `sxwnl.github.io`，回源 HOST `sx3d.qaiu.top`，HTTPS。

| 路径 | 缓存 |
|------|------|
| `/resources/*` | 强制 30 天（目录带 `v1/`） |
| `/*.wasm` | 强制 30 天 |
| `/index.html` | 60s |

刷贴图缓存：改 `v1` → `v2`（CMake `SXWNL_WEB_ASSET_PREFIX`、`shell.html` 里的字体 URL、`SXWNL_WEB_ASSET_VER`）。
