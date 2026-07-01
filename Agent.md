# Agent Guidelines

This document records project conventions for future coding agents and human
contributors working in this repository.

## Project Shape

- Core astronomical calculation code lives in `eph/`, `lunar/`, and `mylib/`.
- The desktop 3D GUI lives in `gui/` and is built with CMake, GLFW, OpenGL 3.3,
  GLAD, Dear ImGui, and stb_image.
- Planet, moon, and texture assets live in `resources/`.
- Generated build output lives in `build/` and should not be edited manually.

## Encoding Standard

- All source and text files must be saved as UTF-8 without BOM.
- Do not save files as GBK/ANSI or UTF-8 with BOM.
- Keep Chinese UI text as real UTF-8 Chinese text, not escaped byte soup.
- CMake already passes UTF-8 flags for MinGW:
  `-finput-charset=UTF-8 -fexec-charset=UTF-8`.

### Garbled Text Root Cause

The previous garbled strings, for example text like `鏄剧ず...`, were caused by
mojibake: UTF-8 Chinese bytes were opened or rewritten as a local ANSI/GBK code
page, then saved back as UTF-8. BOM was not the direct cause of the garbling,
but mixed BOM and non-BOM files made editor behavior less predictable. VS Code
showing `UTF-8 with BOM` is a warning sign for this project because some tools
and shell commands may treat the leading bytes differently.

When fixing encoding issues:

- First inspect with UTF-8 explicitly.
- Convert BOM files to UTF-8 without BOM.
- Replace mojibake text with the intended Chinese string, not by byte-level
  guessing unless the original encoding is known.
- After edits, scan for both BOM and common mojibake fragments.

PowerShell checks used in this project:

```powershell
# List UTF-8 BOM files among source/text files.
$exts = @('.cpp','.h','.hpp','.c','.txt','.md','.ps1','.sh','.yml','.yaml','.cmake')
Get-ChildItem -Recurse -File |
  Where-Object { $exts -contains $_.Extension -or $_.Name -eq 'CMakeLists.txt' -or $_.Name -eq 'Makefile' } |
  ForEach-Object {
    $b = [System.IO.File]::ReadAllBytes($_.FullName)
    if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) {
      $_.FullName
    }
  }
```

```powershell
# Convert a UTF-8 BOM file to UTF-8 without BOM.
$path = 'path\to\file.cpp'
$b = [System.IO.File]::ReadAllBytes($path)
$text = [System.Text.Encoding]::UTF8.GetString($b, 3, $b.Length - 3)
$enc = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($path, $text, $enc)
```

## Build And Verification

- Prefer the existing CMake build in `build/`.
- On this Windows/MSYS2 setup, compile with:

```powershell
$env:PATH = 'D:\msys64\ucrt64\bin;' + $env:PATH
& 'D:/msys64/ucrt64/bin/ninja.exe' -C build
```

- If linking `sxwnl_gui.exe` fails with `Permission denied`, an old GUI process
  is probably still running. Check before rebuilding:

```powershell
Get-Process | Where-Object { $_.ProcessName -like '*sxwnl*' }
```

- Do not kill user processes silently unless the user explicitly asks to rebuild
  and replacing the executable is necessary. If you stop a process, mention it.
- Successful GUI output is `build/sxwnl_gui.exe`.

## GUI And Rendering Conventions

- Keep the 3D viewport as the primary experience.
- Left controls and right tools are side-collapsible rails; do not use ImGui's
  title-bar collapse for this layout because the viewport must dynamically gain
  width.
- Keep layout dimensions centralized in `gui/panels.cpp`.
- Avoid putting large explanatory text inside the app. Controls should be clear
  from labels, icons, and expected interaction patterns.
- For OpenGL effects, prefer renderer-owned shader/VAO/VBO paths instead of
  drawing complex scene effects as ImGui overlays.
- Preserve current render ordering unless changing it intentionally:
  star field, spacetime grid, orbit lines, asteroid belt, bodies, effects,
  transparent passes, labels.

## Solar System Modeling Notes

- Planet positions come from the sxwnl astronomical engine.
- Synthetic visual-only data, such as the asteroid belt, must be documented in
  comments and should cite public astronomical ranges when possible.
- Display radii are intentionally non-realistic and compressed for readability.
- Earth orientation is sensitive to mesh and texture coordinate conventions.
  Verify north/south pole alignment visually after changing Earth transforms.

## Editing Rules

- Keep changes scoped to the requested area.
- Do not revert unrelated user changes.
- Use `rg` for searches.
- Use `apply_patch` for manual edits.
- Do not edit generated files under `build/` directly.
- After non-trivial GUI/rendering changes, rebuild and report the result.
