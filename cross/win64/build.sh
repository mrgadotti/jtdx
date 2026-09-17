#!/usr/bin/env bash
# Cross-compiles JTDX for 64-bit Windows in the Fedora mingw64 image and lays
# out a runnable tree. The toolchain, Hamlib 5.x and FFTW are baked into the
# image by the Dockerfile.
#
#   docker build -t jtdx-builder-win cross/win64
#   cross/win64/build.sh [output-dir]
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
      -DCMAKE_INSTALL_PREFIX=/ \
      -DCMAKE_PREFIX_PATH=$PREFIX \
      -DRIGCTL_EXE=$PREFIX/bin/rigctl.exe \
      -DRIGCTLD_EXE=$PREFIX/bin/rigctld.exe \
      -DRIGCTLCOM_EXE=$PREFIX/bin/rigctlcom.exe \
      .. > /tmp/cmake.log 2>&1; then
  grep -B2 -A10 "CMake Error" /tmp/cmake.log | head -60; exit 1
fi

echo "=== make ==="
if ! make -j"$(nproc)" > /tmp/make.log 2>&1; then
  grep -E "error:|Error [0-9]|undefined reference" /tmp/make.log | head -40; exit 1
fi

# install gives the data files their proper layout: Configuration::data_path()
# looks for <exe>/../share/jtdx at run time
echo "=== install ==="
STAGE=/build/stage
rm -rf $STAGE
make install DESTDIR=$STAGE > /tmp/install.log 2>&1 || { tail -20 /tmp/install.log; exit 1; }

# the install rules put the executables under <prefix>/bin and the Qt plugins
# where the generated qt.conf points; find where jtdx.exe actually landed and
# put the runtime DLLs beside it
BIN=$(dirname "$(find $STAGE -name jtdx.exe | head -1)")
echo "executaveis em: ${BIN#$STAGE}"

# resolve the DLL closure against the sysroot, ignoring Windows system libraries
cd $BIN
is_system () {
  case "${1,,}" in
    kernel32.dll|msvcrt.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|ole32.dll|\
    oleaut32.dll|ws2_32.dll|winmm.dll|imm32.dll|dwmapi.dll|uxtheme.dll|dxgi.dll|\
    setupapi.dll|cfgmgr32.dll|version.dll|crypt32.dll|netapi32.dll|userenv.dll|\
    iphlpapi.dll|mpr.dll|wtsapi32.dll|powrprof.dll|avicap32.dll|msvfw32.dll|\
    oleacc.dll|comdlg32.dll|shlwapi.dll|bcrypt.dll|ntdll.dll|rpcrt4.dll|\
    winspool.drv|opengl32.dll|glu32.dll|msimg32.dll|dnsapi.dll|authz.dll|\
    secur32.dll|d3d9.dll|d3d11.dll|api-ms-*|ext-ms-*) return 0 ;;
  esac
  return 1
}
for pass in 1 2 3 4 5 6; do
  added=0
  for f in $(find . $STAGE/plugins -name "*.exe" -o -name "*.dll" 2>/dev/null); do
    for d in $(x86_64-w64-mingw32-objdump -p "$f" 2>/dev/null | grep "DLL Name" | awk "{print \$3}"); do
      is_system "$d" && continue
      [ -f "$d" ] && continue
      if [ -f "$PREFIX/bin/$d" ]; then cp "$PREFIX/bin/$d" . ; added=1
      else echo "  ! nao encontrada: $d (exigida por $f)"; fi
    done
  done
  [ $added -eq 0 ] && break
done

find $STAGE -name "*.exe" -o -name "*.dll" | while read f; do
  x86_64-w64-mingw32-strip --strip-unneeded "$f" 2>/dev/null || true
done

# flatten the unix-ish <prefix>/usr level into a layout that makes sense on
# Windows: bin/ beside share/ and plugins/.  qt.conf is rewritten to match,
# and Configuration::data_path() still resolves <exe>/../share/jtdx.
if [ -d $STAGE/usr ]; then
  mv $STAGE/usr/* $STAGE/ && rmdir $STAGE/usr
fi
printf "[Paths]\nPlugins = ../plugins\n" > $STAGE/bin/qt.conf

rm -rf /out/*
cp -a $STAGE/. /out/
echo "=== arvore instalada ==="
find /out -maxdepth 2 -type d | sort
echo "executaveis: $(find /out -name "*.exe" | wc -l)  dlls: $(find /out -name "*.dll" | wc -l)"
'
