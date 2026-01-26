#!/bin/bash

case "$1" in
    dbg|"")
        g++ -g -Og -std=c++20 -I raylib/linux_amd64/include main.cpp raylib/linux_amd64/lib/libraylib-debug.a
        ;;
    opt)
        g++ -g -O2 -DNDEBUG -std=c++20 -I raylib/linux_amd64/include main.cpp raylib/linux_amd64/lib/libraylib.a
        ;;
    win)
        clang++ -O2 -DNDEBUG -std=c++20 --target=x86_64-w64-mingw32 -pthread -I raylib/win64_mingw-w64/include main.cpp raylib/win64_mingw-w64/lib/libraylib.a -o a.exe -lwinmm -lgdi32 -lopengl32 -static-libgcc -static-libstdc++ -static
        ;;
esac
