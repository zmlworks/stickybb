#!/bin/sh
# StickyBB - Windows x64 GUI build with MinGW-w64 (Debian/Ubuntu: apt install gcc-mingw-w64-x86-64)
set -eu
cd "$(dirname "$0")"
x86_64-w64-mingw32-windres -O coff -i StickyBB.rc -o StickyBB.res.o
x86_64-w64-mingw32-gcc -O2 -s -mwindows -o StickyBB.exe StickyBB.c StickyBB.res.o \
    -lcomctl32 -lgdi32 -lcomdlg32 -lshell32
rm -f StickyBB.res.o
echo "built StickyBB.exe"
