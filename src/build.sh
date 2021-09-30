#!/bin/bash
COMPILER_FLAGS="-g -DDEBUG=0 -DINTERNAL=1 -DCOMPILER_GCC -O2 -Wall -Wno-pedantic -Wextra -Werror -fno-rtti -Wno-switch -Wno-logical-not-parentheses -Wno-unused-parameter -Wno-write-strings -Wno-unused-function -Wno-unused-variable -Wno-maybe-uninitialized -fno-exceptions -std=gnu++11"

mkdir -p ../build
g++ chess_asset_packer.cpp -o ../build/chess_asset_packer $COMPILER_FLAGS -DCOMPILER_GCC

g++ -shared -o ../build/chess.willbeso -fPIC chess.cpp $COMPILER_FLAGS
mv ../build/chess.willbeso ../build/chess.so
g++ linux_chess.cpp -o ../build/linux_chess  $COMPILER_FLAGS -ldl -lpthread -lxcb -lX11-xcb -lGL -lX11