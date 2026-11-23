#!/bin/bash
# 将 util 库打包成 .pm 并发布到本地仓库
#
# 用法:
#   ./pack.sh            # 编译 debug/release 两套 libutil.a, 打包并发布
#   ./pack.sh -f         # 同名同版本已存在时强制覆盖
#
# 包内结构:
#   util/
#   ├── manifest.json
#   ├── include/util.h
#   └── lib/{debug,release}/libutil.a
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG_NAME="util"
PKG_VERSION="0.1.1"
WORK_DIR="$ROOT/pack/build"
PKG_DIR="$WORK_DIR/$PKG_NAME"

FORCE_FLAG=""
if [ "$1" = "-f" ]; then
    FORCE_FLAG="-f"
fi

SRCS=(
    "$ROOT/src/msg_object.cc"
    "$ROOT/src/timer.cc"
)

echo "==> 清理旧的打包产物"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/include" "$PKG_DIR/lib/debug" "$PKG_DIR/lib/release"

build_lib() {
    local mode="$1"
    local flags="$2"
    local obj_dir="$WORK_DIR/obj_$mode"
    mkdir -p "$obj_dir"

    echo "==> 编译 $mode 静态库 (flags: $flags)"
    for src in "${SRCS[@]}"; do
        local base
        base="$(basename "$src" .cc)"
        g++ -std=c++14 $flags \
            -I"$ROOT/inc" -I"$ROOT/extern_inc" -I"$ROOT/src" \
            -c "$src" -o "$obj_dir/$base.o"
    done

    ar rcs "$PKG_DIR/lib/$mode/lib$PKG_NAME.a" "$obj_dir"/*.o
    echo "    -> $PKG_DIR/lib/$mode/lib$PKG_NAME.a"
}

build_lib debug "-g -O0 -DDEBUG"
build_lib release "-O2 -mtune=generic -DNDEBUG"

echo "==> 拷贝头文件与 manifest"
cp "$ROOT"/inc/*.h "$PKG_DIR/include/"
cp "$ROOT/pack/manifest.json" "$PKG_DIR/"

echo "==> 打包并发布到本地仓库"
pm pkg create "$PKG_DIR" $FORCE_FLAG

echo "==> 完成, 仓库内容:"
pm pkg list | grep -E "$PKG_NAME|name" || true
