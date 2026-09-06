# 字体来源与许可

| 文件 | 字体 | 版权 | 许可 |
| --- | --- | --- | --- |
| `NotoSansCJKsc-Regular.otf` | Noto Sans CJK SC | Copyright The Noto Project Authors | SIL Open Font License 1.1 |
| `NotoSansSymbols-Astro.ttf` | Noto Sans Symbols (子集) | Copyright 2022 The Noto Project Authors (https://github.com/notofonts/symbols) | SIL Open Font License 1.1 |

`NotoSansSymbols-Astro.ttf` 是 Noto Sans Symbols Regular (v2.003) 的裁剪子集，只保留
天文与星座符号 `U+263D-U+2653` 与 `U+26E2`（日月、五星、黄道十二宫）。Noto Sans CJK
不含这些码位，星座名称里的 ♈-♓ 因此会显示为 `?`；该子集以合并字体源的方式并入图集，
补齐这一段字形。

裁剪方式（fontTools）：

```
python3 -m fontTools.subset NotoSansSymbols-Regular.ttf \
  --unicodes="U+263D-2653,U+26E2" --output-file=NotoSansSymbols-Astro.ttf \
  --no-hinting --desubroutinize --name-IDs="*" --drop-tables+=DSIG
```

OFL 许可全文见 https://scripts.sil.org/OFL 。
