# WebAssembly 前端

`shell.html` 是 Emscripten 的 `--shell-file`。`build_wasm.sh` / CI 会把编译结果写成同目录的 `index.html`、`sxwnl_gui.js`、`sxwnl_gui.wasm`，并把精简资源放到 `resources/v1/`。

线上 Pages 用独立分支 **`gh-pages`**，里面只有这份 `web/` 目录（不是整仓）。在仓库 Settings → Pages：

1. Source: **Deploy from a branch**
2. Branch: **`gh-pages`**
3. Folder: **`/` (root)**

打开：

- https://sxwnl.github.io/sxwnl-cpp-3d/web/
- https://sx.qaiu.top/sxwnl-cpp-3d/web/

`sx.qaiu.top` 已是组织站点 `sxwnl/sxwnl.github.io` 的自定义域名，项目站自动进子路径。**不要在本仓库或 `gh-pages` 上放 `CNAME`**，写 `sx.qaiu.top` 会把主站绑定抢过来。

不要把 144MB 的桌面 `resources/` 整包拷进站点。CI 只上传精简后的约 18MB。

## 路径

页面挂在 `/sxwnl-cpp-3d/web/`。所有 HTTP fetch / `locateFile` 都相对「当前 URL 的目录部分」。

```text
resources/v1/planet/tex/8k_earth_daymap.jpg   # 对
/resources/v1/planet/tex/8k_earth_daymap.jpg  # 错：打到 sx.qaiu.top 根（主站）
```

MEMFS 内部路径（`/resources/fonts/...`）是 wasm 虚拟盘，不是 HTTP。

## EdgeOne

跟主站同一套：源站 `sxwnl.github.io`，回源 HOST `sx.qaiu.top`，HTTPS。

| 路径 | 缓存 |
|------|------|
| `/sxwnl-cpp-3d/web/resources/*` | 强制 30 天 |
| `/sxwnl-cpp-3d/web/*.wasm` `*.js` | 强制 30 天 |
| `/sxwnl-cpp-3d/web/` `index.html` | 60s |
