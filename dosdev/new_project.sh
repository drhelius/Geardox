#!/bin/bash
# new_project.sh <djgpp|nasm> <directory>: copies an example and the gdxdbg files into a new project
set -e

KIND=$1
TARGET=$2
HERE=$(cd "$(dirname "$0")" && pwd)

if [ -z "$KIND" ] || [ -z "$TARGET" ] || [ ! -d "$HERE/examples/hello-$KIND" ]; then
    echo "Usage: $0 <djgpp|nasm> <directory>"
    exit 1
fi

if [ -e "$TARGET" ]; then
    echo "$TARGET already exists"
    exit 1
fi

mkdir -p "$TARGET"
cp -R "$HERE/examples/hello-$KIND/." "$TARGET/"
rm -rf "$TARGET/build"
mv "$TARGET/claude-mcp.json" "$TARGET/.mcp.json"
mkdir -p "$TARGET/include"
cp "$HERE/include/gdxdbg.h" "$HERE/include/gdxdbg.c" "$HERE/include/gdxdbg.inc" "$TARGET/include/"
echo "Created $TARGET from the $KIND example. Build it with: make -C \"$TARGET\""
