#!/bin/bash
# autobuild.sh — 构建控制台版 test1 (不含 GUI)
# 如需构建 3D GUI 版本，请使用: bash build_gui.sh
build_dir=build

if [ ! -d "$build_dir" ]; then
    mkdir -p $build_dir
fi
cd build && cmake .. -DSXWNL_BUILD_GUI=OFF && make -j4
