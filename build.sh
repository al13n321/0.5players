#!/bin/bash

g++ -g -Og -std=c++20 -I raylib-5.5_linux_amd64/include main.cpp raylib-5.5_linux_amd64/lib/libraylib.a

# Windows:
#clang++ -O2 -std=c++20 --target=x86_64-w64-mingw32 -pthread -I raylib-5.5_win64_mingw-w64/include main.cpp raylib-5.5_win64_mingw-w64/lib/libraylib.a -o a.exe -lwinmm -lgdi32 -lopengl32 -static-libgcc -static-libstdc++ -static
