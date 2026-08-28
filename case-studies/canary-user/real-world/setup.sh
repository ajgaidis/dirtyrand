#!/bin/bash

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"

INSTALL_PREFIX="${SCRIPT_DIR}/install"

MUSL_VERSION="1.1.7"
MUSL_SRC_DIR="${SCRIPT_DIR}/musl-${MUSL_VERSION}"
MUSL_GCC="${INSTALL_PREFIX}/bin/musl-gcc"

#LIBCAP_VERSION="2.76"
#LIBCAP_SRC_DIR="${SCRIPT_DIR}/libcap-${LIBCAP_VERSION}"

IPUTILS_VERSION="20240905"
IPUTILS_SRC_DIR="${SCRIPT_DIR}/iputils-${IPUTILS_VERSION}"


build_musl()
{
  pushd "${MUSL_SRC_DIR}"

  # Configure musl
  CFLAGS="-fstack-protector-strong" \
  ./configure \
    --prefix="${INSTALL_PREFIX}" \
    --syslibdir="${INSTALL_PREFIX}/lib" \
    --enable-debug \
    --enable-gcc-wrapper \
    --disable-static

  # Build musl
  make -j`nproc`

  # Install musl
  make install

  # Link extra includes
  ln -s /usr/include/linux "${INSTALL_PREFIX}/include/linux"
  ln -s /usr/include/asm "${INSTALL_PREFIX}/include/asm"
  ln -s /usr/include/asm-generic "${INSTALL_PREFIX}/include/asm-generic"

  popd
}

#build_libcap()
#{
#  # Download and unpack libcap
#  wget "https://www.kernel.org/pub/linux/libs/security/linux-privs/libcap2/libcap-${LIBCAP_VERSION}.tar.xz"
#  tar -xf "libcap-${LIBCAP_VERSION}.tar.xz"
#
#  pushd "${LIBCAP_SRC_DIR}"
#
#  # Make libcap
#  make CC="${MUSL_GCC}" prefix="${INSTALL_PREFIX}" lib="lib" -j`nproc`
#
#  # Install libcap
#  make CC="${MUSL_GCC}" prefix="${INSTALL_PREFIX}" lib="lib" install
#
#  popd
#}

build_ping()
{
  # Download and unpack iputils (ping)
  wget "https://github.com/iputils/iputils/releases/download/${IPUTILS_VERSION}/iputils-${IPUTILS_VERSION}.tar.xz"
  tar -xf "iputils-${IPUTILS_VERSION}.tar.xz"

  pushd "${IPUTILS_SRC_DIR}"

  # Apply some modifications to get the thing to compile
  cp "${SCRIPT_DIR}/in6_flowlabel.h" "ping/"
  sed -i 's/#include <linux\/in6\.h>/#include "in6_flowlabel\.h"/g' "ping/ping.h"

  # Configure iputils
  CC="${MUSL_GCC}" \
  CFLAGS="-fstack-protector-strong" \
  LDFLAGS="-Wl,-rpath,${INSTALL_PREFIX}/lib" \
  meson setup builddir \
    --prefix="${INSTALL_PREFIX}" \
    --bindir="${INSTALL_PREFIX}/bin" \
    -DUSE_CAP=false \
    -DUSE_IDN=false \
    -DBUILD_PING=true \
    -DBUILD_ARPING=false \
    -DBUILD_CLOCKDIFF=false \
    -DBUILD_TRACEPATH=false \
    -DBUILD_MANS=false \
    -DNO_SETCAP_OR_SUID=false \
    -DSETCAP_OR_SUID_PING=true \
    -DUSE_GETTEXT=false \
    -DSKIP_TESTS=true

  pushd builddir

  # Compile ping
  meson compile

  # Install ping
  sudo meson install

  popd
  popd
}

main()
{
  pushd "${SCRIPT_DIR}"

  # Install dependencies (if the user can)
  sudo apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    file \
    git \
    meson \
    pkg-config \
    tar \
    wget

  # Create the install prefix
  mkdir -p "${INSTALL_PREFIX}"

  build_musl
  # build_libcap
  build_ping

  make clean
  make

  popd
}
main
