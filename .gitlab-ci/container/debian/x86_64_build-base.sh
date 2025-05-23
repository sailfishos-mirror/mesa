#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_BUILD_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

export DEBIAN_FRONTEND=noninteractive
: "${LLVM_VERSION:?llvm version not set!}"

apt-get install -y ca-certificates curl gnupg2
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list.d/*
echo "deb [trusted=yes] https://gitlab.freedesktop.org/gfx-ci/ci-deb-repo/-/raw/${PKG_REPO_REV}/ ${FDO_DISTRIBUTION_VERSION%-*} main" | tee /etc/apt/sources.list.d/gfx-ci_.list

. .gitlab-ci/container/debian/maybe-add-llvm-repo.sh

# Ephemeral packages (installed for this script and removed again at
# the end)
EPHEMERAL=(
)

DEPS=(
    apt-utils
    bison
    ccache
    curl
    "clang-${LLVM_VERSION}"
    "clang-format-${LLVM_VERSION}"
    dpkg-cross
    dpkg-dev
    findutils
    flex
    flatbuffers-compiler
    g++
    cmake
    gcc
    git
    glslang-tools
    kmod
    "libclang-${LLVM_VERSION}-dev"
    "libclang-cpp${LLVM_VERSION}-dev"
    "libclang-common-${LLVM_VERSION}-dev"
    libelf-dev
    libepoxy-dev
    libexpat1-dev
    libflatbuffers-dev
    libgtk-3-dev
    "libllvm${LLVM_VERSION}"
    libpciaccess-dev
    libunwind-dev
    libva-dev
    libvdpau-dev
    libvulkan-dev
    libx11-dev
    libx11-xcb-dev
    libxext-dev
    libxml2-utils
    libxrandr-dev
    libxrender-dev
    libxshmfence-dev
    libxtensor-dev
    libxxf86vm-dev
    libwayland-egl-backend-dev
    "llvm-${LLVM_VERSION}-dev"
    make
    ninja-build
    openssh-server
    pkgconf
    python3-mako
    python3-pil
    python3-pip
    python3-ply
    python3-pycparser
    python3-requests
    python3-setuptools
    python3-yaml
    qemu-user
    valgrind
    x11proto-dri2-dev
    x11proto-gl-dev
    x11proto-randr-dev
    xz-utils
    zlib1g-dev
    zstd
)

apt-get update

apt-get install -y --no-remove "${DEPS[@]}" "${EPHEMERAL[@]}" \
        $EXTRA_LOCAL_PACKAGES

. .gitlab-ci/container/build-llvm-spirv.sh

. .gitlab-ci/container/build-libclc.sh

# Needed for ci-fairy s3cp
pip3 install --break-system-packages "ci-fairy[s3] @ git+https://gitlab.freedesktop.org/freedesktop/ci-templates@$MESA_TEMPLATES_COMMIT"

. .gitlab-ci/container/install-meson.sh

. .gitlab-ci/container/build-rust.sh

############### Uninstall ephemeral packages

apt-get purge -y "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh
