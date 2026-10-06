#!/usr/bin/env bash
# Downloads, checks and builds the libraries Vanilla Conquer needs for the PS5 into .deps/ps5, with
# the toolchain in cmake/ps5.cmake. Each library is built once; delete its stamp in build/deps to
# rebuild it. Runs inside the build image, after tools/build-runtime.sh.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
downloads="$root/.deps/downloads"
sources="$root/.deps/src"
build="$root/build/deps"
prefix="$root/.deps/ps5"
toolchain="$root/cmake/ps5.cmake"
mkdir -p "$downloads" "$sources" "$build" "$prefix"

# fetch <file> <sha256> <url>...: downloads the first URL that gives the expected file.
fetch() {
    local file=$1 sha256=$2
    shift 2
    if [[ -f $downloads/$file ]] && echo "$sha256  $downloads/$file" | sha256sum --check --quiet 2>/dev/null; then
        return
    fi
    for url in "$@"; do
        if curl -fsSL -o "$downloads/$file.part" "$url" &&
            echo "$sha256  $downloads/$file.part" | sha256sum --check --quiet; then
            mv "$downloads/$file.part" "$downloads/$file"
            return
        fi
    done
    rm -f "$downloads/$file.part"
    echo "could not fetch $file" >&2
    exit 1
}

# fetch_git <directory> <url> <commit>: a fresh copy of the source at that commit in .deps/src, for
# sources that publish no release archive.
fetch_git() {
    local directory=$1 url=$2 commit=$3
    if [[ ! -d $downloads/$directory.git ]]; then
        git clone -q --bare --filter=blob:none "$url" "$downloads/$directory.git"
    fi
    git -C "$downloads/$directory.git" cat-file -e "$commit^{commit}" 2>/dev/null ||
        git -C "$downloads/$directory.git" fetch -q origin "$commit"
    rm -rf "${sources:?}/$directory"
    mkdir -p "$sources/$directory"
    git -C "$downloads/$directory.git" archive "$commit" | tar -x -C "$sources/$directory"
}

# unpack <file> <directory> [tar options]: a fresh copy of the source in .deps/src.
unpack() {
    local file=$1 directory=$2
    shift 2
    rm -rf "${sources:?}/$directory"
    case $file in
    *.zip) unzip -q "$downloads/$file" -d "$sources" ;;
    *) tar -xf "$downloads/$file" -C "$sources" "$@" ;;
    esac
}

built() { [[ -f $build/$1.stamp ]]; }
done_with() { touch "$build/$1.stamp"; }

# cmake_install <name> <source dir> [cmake options]
cmake_install() {
    local name=$1 source=$2
    shift 2
    rm -rf "${build:?}/$name"
    cmake -S "$source" -B "$build/$name" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" "$@" >"$build/$name.log"
    ninja -C "$build/$name" install >>"$build/$name.log"
}

# SDL2 with ps5-payload-dev's PS5 drivers: VideoOut framebuffer, AudioOut, ScePad and the IME
# dialog, plus the OpenRCT2 port's changes in patches/sdl. No OpenGL.
if ! built sdl2; then
    sdl="$sources/SDL-ee4c47dc"
    fetch_git SDL-ee4c47dc https://github.com/ps5-payload-dev/SDL.git ee4c47dc0d617b3bc8f35108f9956baf228a1322
    for patch in "$root"/patches/sdl/*.patch; do
        patch -d "$sdl" -p1 --quiet <"$patch"
    done
    cmake_install sdl2 "$sdl" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_OPENGL=OFF \
        -DSDL_LIBSAMPLERATE=OFF -DSDL_LOADSO=OFF
    done_with sdl2
fi

# OpenAL Soft, which Vanilla Conquer plays its sound, speech, music and movie audio through, with
# SDL2 as its only output (SDL drives AudioOut). Nothing is loaded at run time.
if ! built openal; then
    fetch openal-soft-1.24.3.tar.bz2 cb5e6197a1c0da0edcf2a81024953cc8fa8545c3b9474e48c852af709d587892 \
        https://github.com/kcat/openal-soft/releases/download/1.24.3/openal-soft-1.24.3.tar.bz2
    unpack openal-soft-1.24.3.tar.bz2 openal-soft-1.24.3
    options=(-DLIBTYPE=STATIC -DALSOFT_DLOPEN=OFF -DALSOFT_RTKIT=OFF -DALSOFT_UTILS=OFF
        -DALSOFT_NO_CONFIG_UTIL=ON -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_EAX=OFF
        -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF
        -DALSOFT_INSTALL_EXAMPLES=OFF -DALSOFT_INSTALL_UTILS=OFF -DALSOFT_UPDATE_BUILD_VERSION=OFF
        -DALSOFT_BACKEND_SDL2=ON -DALSOFT_REQUIRE_SDL2=ON)
    for backend in PIPEWIRE PULSEAUDIO ALSA OSS SOLARIS SNDIO PORTAUDIO JACK OBOE OPENSL COREAUDIO WAVE; do
        options+=("-DALSOFT_BACKEND_$backend=OFF")
    done
    cmake_install openal "$sources/openal-soft-1.24.3" "${options[@]}"
    done_with openal
fi
