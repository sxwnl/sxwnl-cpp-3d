# WebAssembly 前端

`shell.html` 是 Emscripten 的 `--shell-file`。`build_wasm.sh` / CI 会把编译结果写成同目录的 `index.html`、`sxwnl_gui.js`、`sxwnl_gui.wasm`，并把精简资源放到 `resources/v1/`。

工作流把这一整棵树推到 `sxwnl.github.io/3d/`。根目录的 JS 寿星历不动。

## 路径

页面可能挂在 `/3d/` 或 `/3d/index.html`。所有 fetch / `locateFile` 都相对「当前路径的目录部分」，所以两种 URL 以及自定义域 `https://sx.qaiu.top/3d/` 都能工作。不要用绝对 `/resources/...`（那会打到站点根上的 JS 版资源）。

## EdgeOne

不再有单个百兆 `.data`，不必开范围回源。建议：

| 路径 | 缓存 |
|------|------|
| `/3d/resources/*` | 强制 30 天（文件名带 `v1/` 版本目录） |
| `/3d/*.wasm` `/3d/*.js` | 1 天或按 hash |
| `/3d/` `/3d/index.html` | `no-cache` 或短 TTL |

刷贴图缓存：改 `v1` → `v2`（CMake `SXWNL_WEB_ASSET_PREFIX`、`web/shell.html` 里的字体 URL、`SXWNL_WEB_ASSET_VER`）。
