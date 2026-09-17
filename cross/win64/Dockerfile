FROM fedora:41

# Fedora packages a complete mingw64 toolchain including gfortran and Qt5,
# which is why this is the cross-compilation base rather than Ubuntu.
RUN dnf -y install --setopt=install_weak_deps=False \
      mingw64-gcc mingw64-gcc-c++ mingw64-gcc-gfortran \
      mingw64-qt5-qtbase mingw64-qt5-qtbase-devel \
      mingw64-qt5-qtmultimedia mingw64-qt5-qtserialport \
      mingw64-qt5-qtwebsockets mingw64-qt5-qttools \
      mingw64-fftw mingw64-boost mingw64-libusbx mingw64-zlib \
      mingw64-winpthreads mingw64-pkg-config mingw64-libgomp \
      cmake make git autoconf automake libtool pkgconf-pkg-config \
      qt5-qttools-devel gcc gcc-c++ patch file which findutils curl \
    && dnf clean all

WORKDIR /build

# Fedora's mingw64 Qt5 ships the target libraries but not the Linguist tools;
# .ts -> .qm conversion runs on the host anyway, so point them at the native ones.
RUN for t in lrelease lupdate lconvert; do \
      ln -sf /usr/lib64/qt5/bin/$t /usr/x86_64-w64-mingw32/bin/qt5/$t; \
    done

# Fedora's mingw64-fftw is built without threads, and the decoder links
# fftwf_init_threads/fftwf_plan_with_nthreads. Rebuild it with threads folded
# into the single-precision library.
RUN curl -fsSL https://www.fftw.org/fftw-3.3.10.tar.gz -o /tmp/fftw.tar.gz \
    && tar -xzf /tmp/fftw.tar.gz -C /tmp \
    && cd /tmp/fftw-3.3.10 \
    && ./configure --host=x86_64-w64-mingw32 \
         --prefix=/usr/x86_64-w64-mingw32/sys-root/mingw \
         --enable-float --enable-threads --with-combined-threads \
         --disable-static --enable-shared \
    && make -j"$(nproc)" \
    && make install \
    && rm -rf /tmp/fftw-3.3.10 /tmp/fftw.tar.gz

# Hamlib 5.x cross-built from source: Fedora only packages mingw64-hamlib 3.3
# and JTDX 4.x needs the 5.x API. Baked in so JTDX rebuilds stay fast.
# Pinned to the same vintage as the Linux package in use (5.0.0~git.20251223).
# Hamlib master has since made rig->state private, which JTDX does not compile against.
RUN git clone --shallow-since=2025-12-15 https://github.com/Hamlib/Hamlib.git /tmp/hamlib-src \
    && cd /tmp/hamlib-src \
    && git checkout "$(git rev-list -1 --before=2025-12-24 HEAD)" \
    && ./bootstrap \
    && ./configure --host=x86_64-w64-mingw32 \
         --prefix=/usr/x86_64-w64-mingw32/sys-root/mingw \
         --disable-static --enable-shared \
         --without-cxx-binding --disable-winradio \
         --without-perl-binding --without-python-binding --without-tcl-binding \
    && make -j"$(nproc)" \
    && make install \
    && rm -rf /tmp/hamlib-src
