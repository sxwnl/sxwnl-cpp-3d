# WebAssembly 前端

`shell.html` 是 Emscripten 的 `--shell-file`。`build_wasm.sh` / CI 会把编译结果写成同目录的 `index.html`、`sxwnl_gui.js`、`sxwnl_gui.wasm`，并把精简资源放到 `resources/v1/`。

本仓库自建 GitHub Pages（默认 `GITHUB_TOKEN`）。`sx.qaiu.top` 已是组织站点 `sxwnl/sxwnl.github.io` 的自定义域名，项目站自动出现在子路径：

- https://sxwnl.github.io/sxwnl-cpp-3d/
- https://sx.qaiu.top/sxwnl-cpp-3d/

**不要在本仓库放 `CNAME` 文件。** 一个自定义域名同时只能绑一个仓库；写成 `sx.qaiu.top` 会把主站绑定抢过来，主站直接 404。

不要把 144MB 的桌面 `resources/` 整包拷进站点。CI 只上传精简后的约 18MB。

## 路径

项目站挂在 `/sxwnl-cpp-3d/`。所有 HTTP fetch / `locateFile` 都相对「当前 URL 的目录部分」。

```text
resources/v1/planet/tex/8k_earth_daymap.jpg   # 对
/resources/v1/planet/tex/8k_earth_daymap.jpg  # 错：打到 sx.qaiu.top 根（主站）
```

MEMFS 内部路径（`/resources/fonts/...`）是 wasm 虚拟盘，不是 HTTP。

## EdgeOne

跟主站同一套：源站 `sxwnl.github.io`，回源 HOST `sx.qaiu.top`，HTTPS。

| 路径 | 缓存 |
|------|------|
| `/sxwnl-cpp-3d/resources/*` | 强制 30 天（目录带 `v1/`） |
| `/sxwnl-cpp-3d/*.wasm` `*.js` | 强制 30 天 |
| `/sxwnl-cpp-3d/` `/sxwnl-cpp-3d/index.html` | 60s |
| `/sxwnl-cpp-3d/*` | 需要时加 COOP/COEP |

刷贴图缓存：改 `v1` → `v2`（CMake `SXWNL_WEB_ASSET_PREFIX`、`shell.html` 里的字体 URL、`SXWNL_WEB_ASSET_VER`）。
