#!/bin/sh

set -e
set -x

cmake --version

if [ "$TRAVIS_OS_NAME" = "linux" -o -z "$TRAVIS_OS_NAME" ]; then
    export CC=gcc-6
    export CXX=g++-6
    export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH

    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j4

    ctest -VV -C Release
elif [ "$TRAVIS_OS_NAME" = "osx" ]; then
    set -o pipefail

    export MACOSX_DEPLOYMENT_TARGET=10.9

    mkdir build && cd build
    cmake .. -GXcode -DDYNARMIC_TESTS=0
    xcodebuild -configuration Release
fi
