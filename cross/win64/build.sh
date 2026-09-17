#!/usr/bin/env bash
# Cross-compiles JTDX for 64-bit Windows in the Fedora mingw64 image.
# The toolchain and Hamlib 5.x are baked into jtdx-builder-win.
set -euo pipefail

SRC=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$PWD/jtdx_win}
mkdir -p "$OUT"

docker run --rm \
  -v "$SRC":/src:ro \
  -v "$OUT":/out \
  jtdx-builder-win bash -eu -c '
PREFIX=/usr/x86_64-w64-mingw32/sys-root/mingw
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig

mkdir -p /build/jtdx && cp -a /src/. /build/jtdx/
cd /build/jtdx && rm -rf build bin && mkdir build && cd build

cat > /tmp/toolchain.cmake <<EOF
set (CMAKE_SYSTEM_NAME Windows)
set (CMAKE_SYSTEM_PROCESSOR x86_64)
set (TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set (CMAKE_C_COMPILER   \${TOOLCHAIN_PREFIX}-gcc)
set (CMAKE_CXX_COMPILER \${TOOLCHAIN_PREFIX}-g++)
set (CMAKE_Fortran_COMPILER \${TOOLCHAIN_PREFIX}-gfortran)
set (CMAKE_RC_COMPILER  \${TOOLCHAIN_PREFIX}-windres)
set (CMAKE_FIND_ROOT_PATH $PREFIX)
set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

echo "=== cmake ==="
if ! cmake -DCMAKE_TOOLCHAIN_FILE=/tmp/toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DJTDX_NATIVE_OPT=OFF \
      -DWSJT_GENERATE_DOCS=OFF \
      -DCMAKE_PREFIX_PATH=$PREFIX \
      .. > /tmp/cmake.log 2>&1; then
  grep -B2 -A10 "CMake Error" /tmp/cmake.log | head -60
  exit 1
fi
echo "cmake OK"

echo "=== make ==="
if ! make -j"$(nproc)" > /tmp/make.log 2>&1; then
  echo "--- primeiros erros ---"
  grep -E "error:|Error [0-9]|undefined reference" /tmp/make.log | head -40
  exit 1
fi
echo "make OK"

find . -name "*.exe" -exec cp -v {} /out/ \;
cp -v $PREFIX/bin/libhamlib-5.dll /out/ 2>/dev/null || true
'
