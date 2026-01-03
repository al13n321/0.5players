#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <iostream>
#include <cassert>
#include <cstring>
#include <fstream>
#include <cstdint>
#include <unordered_set>
#include <array>
#include <algorithm>
#include <deque>

#include "common.cpp"
#include "serialization.cpp"
#include "power.cpp"
#include "update.cpp"
#include "render.cpp"

int main() {
    InitWindow(2000, 2000, "hello world");
    SetTargetFPS(144);

    World w;
    if (!w.load_from_file())
    {
        w.init_start({10, 10});
        w.editor.on = true;
    }
    w.load_replays_from_file();
    w.init_finish();

    while (true) {
        BeginDrawing();
        SetWindowTitle(TextFormat("fps: %d", GetFPS()));

        update(w);

        BeginMode2D(w.camera);

        render(w);

        EndMode2D();

        render_ui(w);

        EndDrawing();

        if (WindowShouldClose())
            break;
    }
}
