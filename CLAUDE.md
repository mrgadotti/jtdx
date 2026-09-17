# JTDX — notes for working in this repo

Personal fork of JTDX (itself a fork of WSJT-X), on branch `pp5mgt`, versioned
independently from **4.0.0** onwards. Upstream JTDX lineage ends at 2.3.1.

Standing goal: keep FT8/FT4 decoding cheap enough for weak hardware (a dual-core
Celeron station box, a Raspberry Pi 3) **without changing the decoding algorithm
or sensitivity**.

## Layout

- Qt5 GUI (~80 `.cpp` at top level; `mainwindow.cpp` is 8k lines, `Configuration.cpp` 6k)
- Fortran decoder in `lib/` (~300 files), FT8 v2 LDPC/OSD in `lib/ft8v2/`
- The decoder is a **separate long-lived process**, `jtdxjt9`, launched from
  `mainwindow.cpp` and driven over POSIX shared memory plus a `.lock` sentinel
  file. Decoder CPU is not in the GUI process.
- Qt5 only. No Qt6 path exists (`QAudioInput`/`QAudioOutput` are used throughout).

## Build

```sh
cd build && cmake .. && make -j$(nproc)
```

`CMAKE_BUILD_TYPE` defaults to RELEASE. Two CMake options matter:

- `JTDX_NATIVE_OPT` (default **ON**) adds `-march=native`. Fine when building on the
  machine that will run it. **Must be OFF for any package**, or the binary picks up
  the build host's ISA — the Celeron target has no AVX and would die with SIGILL.
- `WSJT_GENERATE_DOCS` — turn OFF unless `asciidoctor` (the Ruby one, not `asciidoc`)
  is installed. Shipped packages contain no generated manual anyway.

The whole Fortran tree is compiled twice, into `wsjt_fort` and `wsjt_fort_omp`; only
the `_omp` one is linked by the two shipping binaries.

## Packaging

Two local Docker builder images: `jtdx-builder` (Ubuntu 24.04, also covers Linux Mint
22.x) and `jtdx-builder-2604` (Ubuntu 26.04). A custom **hamlib 5.x** `.deb` must be
installed into the container first — the distros ship 4.6.x (`libhamlib.so.4`), which
is ABI-incompatible; that package carries headers and libs under `/usr/local`.
Packages and the install guide are collected under `~/dev/github/jtdx_install*/`.

Always verify a package before shipping it:

```sh
objdump -d <extracted>/usr/local/bin/jtdxjt9 | grep -cE 'vfmadd|vmulps|vaddps|ymm'   # must be 0
```

## Data directories — easy to get wrong

`Configuration::data_dir()` is the **installed** share directory
(`/usr/local/share/jtdx`) and is root-owned. Anything that *writes* a data file must
use `QStandardPaths::writableLocation(QStandardPaths::DataLocation)`
(`~/.local/share/JTDX`). That writable copy is what `LogBook::init` and `MainWindow`
read back, treating it as a user override over the compiled-in Qt resource.

The decoder's `share_dir` (the `-r` argument to `jtdxjt9`) is used for exactly one
thing, `ALLCALL7.TXT`, and points at the writable directory, seeded from the installed
copy on first run.

Data file sources: cty.dat from country-files.com, `lotw-user-activity.csv` from ARRL,
CALL3.TXT / CALL3_EME.TXT from wsjt-x-improved.sourceforge.io. ALLCALL7.TXT is the odd
one — published on the JTDX SourceForge project as a dated zip with no stable URL, so
it is discovered via `sourceforge.net/projects/jtdx/rss?path=/` and unpacked.

## Changing the decoder

`jtdxjt9` **cannot decode WAV files from the CLI** in this fork — it only runs via the
GUI's shared memory. So there is no end-to-end decode regression harness. Verify a
decoder change by proving bit-identity per routine instead:

1. `git show HEAD:lib/.../foo.f90 | sed 's/foo/foo_ref/g' > ref.f90`
   (rename every subroutine the file defines, or you get duplicate symbols)
2. Write a driver that feeds both the same randomised inputs — use
   `encode174_91` to build valid codewords and add noise, so the converging path runs
3. Link against `build/libwsjt_fort_omp.a` and `build/libwsjt_cxx.a`
   (the latter provides `crc14`, needed by `chkcrc14a`), plus `-lfftw3f -lstdc++`

**Measure, do not reason, about speed.** Two "obvious" LDPC optimisations were tried
and dropped: a precomputed reverse index was exactly neutral, and restricting a `tanh`
array section to the real row weight cost 12%, because a variable-length section
defeats gfortran's vectorisation of the fixed 7-element loop.

Per-thread scratch buffers in Fortran use `ALLOCATABLE, SAVE` plus
`!$omp threadprivate(...)` — `lib/osd174.f90` is the reference example. Plain `save` in
a routine reachable from the `!$omp parallel do` in `lib/decoder.f90` is a race.

`perf` is installed on the dev machine but `perf_event_paranoid=4`, so profiling needs
root. Use gprof or manual `cpu_time` instrumentation instead.

## Do not harvest performance work from `../wsjtx`

That checkout is `github.com/WSJTX/wsjtx` v3.0.2.0 and it already contains this
decoder, imported as `lib/ft8var/`. Where they differ JTDX is usually the faster side.
Specifically do **not** port: `-fbounds-check` in the Fortran RELEASE flags, mainline
`four2a`'s full plan-table scan inside an OpenMP critical, `ft8_decodevar`'s widened
`critical(find_dupes)`, `ft8/subtractft8.f90`'s four 180k-point FFT passes, or
`decoder.f90`'s 700-line hand-unrolled `parallel sections` chain.

Feature ports from there are fine — the two trees' GUI code has diverged a lot, so
expect to re-implement against JTDX's structures rather than copy files.

## Conventions

- A `.cpp` ends with `#include "moc_<name>.cpp"`; `CMAKE_AUTOMOC` is ON. New top-level
  sources go in `wsjt_qt_CXXSRCS` in `CMakeLists.txt`. There is no `Network/` subdir —
  keep new files flat.
- Adding a persisted setting touches six places, all greppable from an existing one
  (`beepOnMyCall` is a good template): accessor in `Configuration.hpp`, member +
  accessor impl + load-into-UI + `settings_->value(...)` + `settings_->setValue(...)` +
  read-back-from-UI in `Configuration.cpp`.
- `Configuration.ui` is large; edit it as XML and validate with
  `python3 -c "import xml.etree.ElementTree as ET; ET.parse('Configuration.ui')"`
  before building.
- Message boxes go through `JTDXMessageBox`, whose `query_message` takes
  `(parent, title, text, informative, detail, buttons, default)` — note upstream
  WSJT-X's `MessageBox` has a different parameter order.

## Known remaining opportunity

The biggest untaken decoder win is in `lib/sync8.f90`: `nfawide/nfbwide` are the full
band for every thread, so each thread recomputes the same 372 FFTs and the same
full-band sync metric; only from line ~202 does per-thread slicing begin. Sharing it
would need a barrier per pass, because `dd8` is mutated concurrently by `subtractft8` —
which also means multi-threaded results are already non-deterministic today. Sharing
would make them deterministic, i.e. decodes change. Irrelevant on the 2-core Celeron,
where the auto rule in `decoder.f90` gives `numthreads=1`.
