#!/bin/sh
cmake -S . -B build 
-DCMAKE_TOOLCHAIN_FILE=toolchains/arm-mix410-linux.cmake 
-DCMAKE_BUILD_TYPE=Release

cmake --build build -j