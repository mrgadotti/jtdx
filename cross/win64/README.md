# Cross-compiling JTDX for 64-bit Windows

Builds `jtdx.exe` from Linux, without a Windows machine.

```sh
docker build -t jtdx-builder-win cross/win64
cross/win64/build.sh /path/to/output
```

The image takes a while the first time: it compiles Hamlib and FFTW from
source. After that a JTDX rebuild is a few minutes.

## Why Fedora

Fedora is the only mainstream distribution that packages a complete mingw64
Qt5 alongside gfortran. Debian and Ubuntu ship the mingw compilers but no
mingw Qt5, which rules them out — this project needs both.

Three dependencies still have to be built from source inside the image:

- **Hamlib** — Fedora packages `mingw64-hamlib` 3.3; JTDX 4.x needs the 5.x
  API. Pinned to the 2025-12-23 vintage because Hamlib master has since made
  `rig->state` private, which `HamlibTransceiver.cpp` does not compile against.
- **FFTW** — Fedora's `mingw64-fftw` is built without threads and the decoder
  links `fftwf_init_threads`. It also needs the Fortran interface (`sfftw_*`),
  which `lib/four2a.f90` calls.
- Qt's Linguist tools are symlinked from the native Qt5, since Fedora's mingw
  Qt5 does not ship them and `.ts` → `.qm` conversion runs on the host anyway.

`mingw64-libgomp` is a separate package and is easy to miss — without it
`-fopenmp` compiles but will not link, and `lib/decoder.f90` calls
`omp_get_num_procs` unconditionally.

## OmniRig

Not built. Generating its interface requires `dumpcpp` to read the OmniRig
COM type library from the Windows registry, which does not exist when
cross-compiling. `JTDX_WITH_OMNIRIG` turns off automatically whenever CMake
reports `CMAKE_CROSSCOMPILING`, and the `TransceiverFactory` entries are
compiled out with it. Hamlib, TCI, HRD and DXLab Commander are unaffected.

A native Windows build still gets OmniRig, since the option defaults to ON.

## Packaging the result

`build.sh` copies the executables and `libhamlib-5.dll` to the output
directory. A runnable folder additionally needs the Qt and GCC runtime DLLs
plus the `platforms/qwindows.dll` and `audio/qtaudio_windows.dll` plugins —
without the first the GUI never appears, without the second there is no
audio. Resolve the DLL closure with `x86_64-w64-mingw32-objdump -p`, copying
from `/usr/x86_64-w64-mingw32/sys-root/mingw/bin` until nothing non-system is
missing, then `x86_64-w64-mingw32-strip --strip-unneeded` everything.
