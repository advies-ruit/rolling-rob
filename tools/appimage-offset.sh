#!/bin/sh
# Where the squashfs inside an AppImage starts: the end of its runtime, an
# ELF file, read from the ELF header (section headers come last). For
# unsquashfs -o where the AppImage itself cannot be started (packages.yml:
# 32-bit ARM under QEMU).
#   tools/appimage-offset.sh <AppImage>
set -eu
f=${1:?AppImage}
n() { od -An -t "u$2" -j "$1" -N "$2" "$f" | tr -d ' '; }
if [ "$(n 4 1)" = 1 ]; then # 32-bit ELF
  echo $(($(n 32 4) + $(n 46 2) * $(n 48 2)))
else
  echo $(($(n 40 8) + $(n 58 2) * $(n 60 2)))
fi
