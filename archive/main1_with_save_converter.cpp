#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <iostream>
#include <cassert>
#include <cstring>
#include <fstream>
#include <cstdint>

enum Direction {
    DIR_LEFT = 0,
    DIR_UP = 1,
    DIR_RIGHT = 2,
    DIR_DOWN = 3,
};

enum Wires {
    WIRE_NONE = 0,

    // These can be combined.
    WIRE_LEFT = 1 << DIR_LEFT,
    WIRE_UP = 1 << DIR_UP,
    WIRE_RIGHT = 1 << DIR_RIGHT,
    WIRE_DOWN = 1 << DIR_DOWN,
    WIRE_CIRCLE = 0x10,

    // Union of WIRE_{LEFT,RIGHT,UP,DOWN}.
    WIRE_ALL_DIRECTIONS = 0xf,

    // The below can't be combined.

    // Requires WIRE_ALL_DIRECTIONS to also be set.
    WIRE_JUMP_CROSS = 0x20,
    // If not FLOOR_VOID then WIRE_ALL_DIRECTIONS must also be set.
    // If FLOOR_VOID, direction flags may be unset for non-FLOOR_VOID neighbors that are not connected to the void through a wall.
    WIRE_WHOLE = 0x40,
};

enum FloorType : uint8_t {
    FLOOR_PASSABLE,
    FLOOR_WALL,
    FLOOR_VOID,
    FLOOR_TRIGGER,
};

enum TileType : uint8_t {
    TILE_GREEN,
    TILE_YELLOW,
    TILE_BLUE,
    TILE_PURPLE,
    TILE_ORANGE,
    TILE_RED,
    TILE_GRAY,
    TILE_BLACK, // WIRE_WHOLE must also be set
};

enum Walls {
    WALL_NONE = 0,

    WALL_LEFT = 1 << DIR_LEFT,
    WALL_UP = 1 << DIR_UP,

    WALL_RIGHT = 1 << DIR_RIGHT,
    WALL_DOWN = 1 << DIR_DOWN,
};

struct IVec {
    int x = 0;
    int y = 0;

    bool operator==(const IVec &) const = default;
    bool operator!=(const IVec &) const = default;
};

struct Power {
    uint8_t wires = WIRE_NONE;
    uint8_t power = WIRE_NONE;

    bool operator==(const Power &) const = default;
};

struct Tile { // (i.e. movable piece)
    TileType type = TILE_GREEN;
    // Which tiles are joined together. This flag is set on the right/bottom of the two tiles that are joined.
    uint8_t weld = WALL_NONE; // only WALL_LEFT and WALL_UP are allowed
    Power power;
    IVec pos;
    // End of serialized part.

    bool operator==(const Tile &) const = default;
};

struct Cell {
    FloorType floor : 4 = FLOOR_VOID;
    uint8_t weld : 4 = WALL_NONE; // only WALL_LEFT and WALL_UP are allowed
    uint8_t barrier : 4 = WALL_NONE; // retractable wall; only WALL_LEFT and WALL_UP are allowed
    uint8_t barrier_active : 4 = WALL_NONE;
    Power floor_power;
    // End of serialized part.

    int tile = -1;

    bool operator==(const Cell &) const = default;
};

enum EditorCellType {
    EDITOR_NONE,
    EDITOR_FLOOR,
    EDITOR_TILE,
};

struct EditorCell {
    EditorCellType type = EDITOR_NONE;
    Cell floor;
    Tile tile;

    Rectangle rect; // in palette_camera's "world" space

    bool equivalent(const EditorCell &r) const {
        if (type != r.type) return false;
        if (type == EDITOR_NONE) return true;
        if (type == EDITOR_FLOOR) return floor == r.floor;
        if (type == EDITOR_TILE) return tile == r.tile;
        assert(false);
    }
};

struct Editor {
    bool on = true;
    std::vector<EditorCell> palette;
    std::vector<int> palette_columns;
    EditorCell held;

    // In this camera's "world" space, the palette occupies rect x=[-palette_width, 0], and each palette cell has size 1
    // (so tile rendering code doesn't need to worry about scaling).
    Camera2D palette_camera = {};
    float palette_width = 0;

    void init() {
        palette_columns.push_back(palette.size());
        palette.push_back({.type = EDITOR_FLOOR, .floor = {.floor = FLOOR_VOID, .floor_power = {.wires = WIRE_WHOLE}}});
        palette.push_back({.type = EDITOR_FLOOR, .floor = {.floor = FLOOR_PASSABLE}});
        palette.push_back({.type = EDITOR_FLOOR, .floor = {.floor = FLOOR_WALL}});
        palette.push_back({.type = EDITOR_FLOOR, .floor = {.floor = FLOOR_TRIGGER}});
        palette_columns.push_back(palette.size());
        palette.push_back({.type = EDITOR_TILE, .tile = {.type = TILE_GREEN}});
        palette.push_back({.type = EDITOR_TILE, .tile = {.type = TILE_YELLOW}});
        palette.push_back({.type = EDITOR_TILE, .tile = {.type = TILE_BLUE}});
        palette.push_back({.type = EDITOR_TILE, .tile = {.type = TILE_PURPLE}});
        palette.push_back({.type = EDITOR_TILE, .tile = {.type = TILE_ORANGE}});
        palette.push_back({.type = EDITOR_TILE, .tile = {.type = TILE_RED}});
        palette.push_back({.type = EDITOR_TILE, .tile = {.type = TILE_GRAY}});
        palette.push_back({.type = EDITOR_TILE, .tile = {.type = TILE_BLACK, .power = {.wires = WIRE_WHOLE}}});
        palette_columns.push_back(palette.size());
    }

    void layout() {
        const float spacing = 0.3;
        const int column_cells = 30;
        float x = 0;
        for (size_t col_idx = 0; col_idx + 1 < palette_columns.size(); ++col_idx) {
            x -= spacing + 1.f;
            float y = spacing;
            int count = 0;
            for (size_t i = palette_columns[col_idx]; i < palette_columns[col_idx + 1]; ++i) {
                EditorCell &cell = palette[i];
                if (count == column_cells) {
                    y = spacing;
                    x -= spacing + 1.f;
                    count = 0;
                }
                ++count;
                cell.rect = {x, y, 1.f, 1.f};
                y += spacing + 1.f;
            }
        }
        x -= spacing;
        palette_width = -x;

        palette_camera.zoom = (float)GetRenderHeight() / (column_cells * (spacing + 1.f) + spacing);
        palette_camera.offset.x = (float)GetRenderWidth();
    }
};

template <typename T>
void save_pod(const T *x, std::vector<char> &out) {
    size_t i = out.size();
    out.resize(i + sizeof(T));
    memcpy(&out[i], x, sizeof(T));
}
template <typename T>
void load_pod(T *x, const char **in, const char *end) {
    if (end - *in < sizeof(T))
        std::abort();
    memcpy(x, *in, sizeof(T));
    *in += sizeof(T);
}

// A thing for repeating an action if key is held, like when typing goes brrrrrrrrrrrrrrrrr.
struct KeyRepeater {
    double pressed_at = 0;
    long repeats = 0;

    bool check(int key) {
        if (IsKeyPressed(key)) {
            pressed_at = GetTime();
            repeats = 0;
            return true;
        }
        if (!IsKeyDown(key)) {
            pressed_at = 0;
            repeats = 0;
            return false;
        }
        assert(pressed_at != 0);

        double now = GetTime();
        double elapsed = now - pressed_at;
        long prev = repeats;
        repeats = (long)(std::max(0., elapsed - .5) * 20.);
        return repeats > prev;
    }
};

struct World {
    IVec size; // first and last cell of each row and column are sentinel cells that are not rendered and not updated
    std::vector<std::vector<Cell>> cells;
    std::vector<Tile> tiles;

    std::vector<std::vector<char>> undo;
    size_t undo_idx = 0; // undo[undo_idx - 1] is the current state
    KeyRepeater undo_repeat;
    KeyRepeater redo_repeat;

    Editor editor;

    Camera2D camera {};

    IVec prev_hover {-1, -1};

    std::string save_file_path = "save.bin";

    void init_start(IVec s) {
        size = s;
        cells.assign(size.y, std::vector<Cell>(size.x));
    }

    // To init, call either init_start() or load(), then call init_finish().
    void init_finish() {
        editor.init();

        Vector2 world_size {(float)size.x, (float)size.y};
        camera.target = world_size * .5f;
        Vector2 resolution {(float)GetRenderWidth(), (float)GetRenderHeight()};
        Vector2 ratio = resolution / world_size;
        camera.zoom = std::min(ratio.x, ratio.y);

        push_undo();
    }

    void save(std::vector<char> &out) const {
        //double start_time = GetTime();

        size_t version = 2;
        save_pod(&version, out);
        save_pod(&size, out);
        size_t t = tiles.size();
        save_pod(&t, out);

        const size_t cell_bytes = offsetof(Cell, floor_power) + sizeof(Power);
        static_assert(cell_bytes == 4, "");
        for (int y = 1; y + 1 < size.y; ++y) {
            for (int x = 1; x + 1 < size.x; ++x) {
                size_t pos = out.size();
                out.resize(pos + cell_bytes);
                memcpy(&out[pos], &cells[y][x], cell_bytes);
            }
        }
        const size_t tile_bytes = offsetof(Tile, pos) + sizeof(IVec);
        static_assert(tile_bytes == 12, "");
        for (int i = 0; i < tiles.size(); ++i) {
            size_t pos = out.size();
            out.resize(pos + tile_bytes);
            memcpy(&out[pos], &tiles[i], tile_bytes);
        }

        // bulk memcpy save v1 with -Og takes 0.12ms
        // loop memcpy save v1 takes about the same
        // loop memcpy save v2 takes 0.08ms
        //std::cout << "save took " << (GetTime() - start_time) << " seconds" << std::endl;
    }
    void load(const std::vector<char> &in_vec) {
        //double start_time = GetTime();

        const char *in = &in_vec[0];
        const char *end = in + in_vec.size();
        size_t version = 0;
        load_pod(&version, &in, end);

        if (version < 1 || version > 2)
            std::abort();

        load_pod(&size, &in, end);
        if (size.x < 2 || size.y < 2 || 1000000 / size.x / size.y == 0)
            std::abort();
        cells.resize(size.y);
        for (int y = 0; y < size.y; ++y) {
            cells[y].assign(size.x, {});
        }
        if (version > 1) {
            size_t t;
            load_pod(&t, &in, end);
            if (t > 1000000)
                std::abort();
            tiles.assign(t, {});
        }

        if (version == 1) {
            struct PowerV1 {
                int wires;
                int power;
            };
            struct TileV1 {
                int type;
                PowerV1 power;
                int weld;
                IVec pos;
            };
            struct CellV1 {
                int floor;
                PowerV1 floor_power;
                int weld;
                int barrier;
                int barrier_active;
                int tile;
            };
            static_assert(sizeof(CellV1) == 28 && sizeof(TileV1) == 24, "");

            for (int y = 1; y + 1 < size.y; ++y) {
                for (int x = 1; x + 1 < size.x; ++x) {
                    CellV1 a;
                    load_pod(&a, &in, end);
                    Cell &b = cells[y][x];
                    b.floor = (FloorType)a.floor;
                    b.floor_power.wires = a.floor_power.wires;
                    b.floor_power.power = a.floor_power.power;
                    b.weld = a.weld;
                    b.barrier = a.barrier;
                }
            }

            size_t t;
            load_pod(&t, &in, end);
            if (t > 1000000)
                std::abort();
            tiles.assign(t, {});
            for (int i = 0; i < tiles.size(); ++i) {
                TileV1 a;
                load_pod(&a, &in, end);
                Tile &b = tiles[i];
                b.type = (TileType)a.type;
                b.power.wires = a.power.wires;
                b.power.power = a.power.power;
                b.weld = a.weld;
                b.pos = a.pos;
            }
        } else if (version == 2) {
            const size_t cell_bytes = offsetof(Cell, floor_power) + sizeof(Power);
            static_assert(cell_bytes == 4, "");
            for (int y = 1; y + 1 < size.y; ++y) {
                for (int x = 1; x + 1 < size.x; ++x) {
                    if (end - in < cell_bytes)
                        std::abort();
                    memcpy(&cells[y][x], in, cell_bytes);
                    in += cell_bytes;
                }
            }
            const size_t tile_bytes = offsetof(Tile, pos) + sizeof(IVec);
            static_assert(tile_bytes == 12, "");
            for (int i = 0; i < tiles.size(); ++i) {
                if (end - in < tile_bytes)
                    std::abort();
                memcpy(&tiles[i], in, tile_bytes);
                in += tile_bytes;
            }
        }

        for (int i = 0; i < tiles.size(); ++i) {
            Tile &tile = tiles.at(i);
            cells.at(tile.pos.y).at(tile.pos.x).tile = i;
        }

        // (tl;dr: everyting is fast, careful clever optimized implementation is not needed, don't worry about it)
        // bulk memcpy load v1 with -Og takes 0.04ms
        // loop memcpy load v1 takes 0.05ms
        // loop memcpy load v2 takes 0.02ms
        // v1 -> v2 conversion load takes 0.04ms
        //std::cout << "load took " << (GetTime() - start_time) << " seconds" << std::endl;
    }

    void save_to_file() {
        std::vector<char> data;
        save(data);

        std::ofstream file(save_file_path, std::ios::binary);
        file.write(data.data(), data.size());
    }

    bool load_from_file() {
        std::vector<char> data;
        {
            std::ifstream file(save_file_path, std::ios::binary | std::ios::ate);
            if (!file) return false;
            size_t size = file.tellg();
            if (size > 1000000000)
                std::abort();
            file.seekg(0);
            data.resize(size);
            file.read(data.data(), size);
        }
        load(data);
        return true;
    }

    void push_undo() {
        undo.resize(undo_idx);
        if (undo.size() > 1000)
            undo.erase(undo.begin(), undo.begin() + undo.size() / 2);

        std::vector<char> out;
        save(out);
        undo.push_back(out);
        undo_idx = undo.size();
    }

    IVec hovered_cell() const {
        Vector2 p = GetMousePosition();
        if (editor.on && GetScreenToWorld2D(p, editor.palette_camera).x >= -editor.palette_width)
            return {-1, -1};
        p = GetScreenToWorld2D(p, camera);
        IVec r {(int)p.x, (int)p.y};
        if (r.x <= 0 || r.x + 1 >= size.x || r.y <= 0 || r.y >= size.y)
            return {-1, -1};
        return r;
    }

    void delete_tile(int ti) {
        Tile &t = tiles.at(ti);
        cells.at(t.pos.y).at(t.pos.x).tile = -1;
        if (ti + 1 != tiles.size()) {
            std::swap(t, tiles.back());
            cells.at(t.pos.y).at(t.pos.x).tile = ti;
        }
        tiles.pop_back();
    }

    Power & get_power(Cell &cell) {
        return cell.tile == -1 ? cell.floor_power : tiles.at(cell.tile).power;
    }
};

void map_fixup(World &w) {
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            auto fixup_power = [](Power &power) {
                if (power.wires & WIRE_WHOLE)
                    power.wires = WIRE_WHOLE | WIRE_ALL_DIRECTIONS;
                if (power.wires & WIRE_JUMP_CROSS)
                    power.wires = WIRE_JUMP_CROSS | WIRE_ALL_DIRECTIONS;
                power.power &= power.wires;
            };

            Cell &cell = w.cells.at(y).at(x);
            if (cell.floor == FLOOR_VOID) {
                cell.floor_power.wires &= WIRE_ALL_DIRECTIONS;
                cell.floor_power.wires |= WIRE_WHOLE;
                if (w.cells.at(y).at(x - 1).floor == FLOOR_VOID) cell.floor_power.wires |= WIRE_LEFT;
                if (w.cells.at(y).at(x + 1).floor == FLOOR_VOID) cell.floor_power.wires |= WIRE_RIGHT;
                if (w.cells.at(y - 1).at(x).floor == FLOOR_VOID) cell.floor_power.wires |= WIRE_UP;
                if (w.cells.at(y + 1).at(x).floor == FLOOR_VOID) cell.floor_power.wires |= WIRE_DOWN;
                cell.floor_power.power &= cell.floor_power.wires;
            } else {
                fixup_power(cell.floor_power);
            }

            if (cell.floor != FLOOR_WALL || w.cells.at(y).at(x - 1).floor != FLOOR_WALL)
                cell.weld &= ~WALL_LEFT;
            if (cell.floor != FLOOR_WALL || w.cells.at(y - 1).at(x).floor != FLOOR_WALL)
                cell.weld &= ~WALL_UP;

            if (cell.tile != -1) {
                Tile &tile = w.tiles.at(cell.tile);
                fixup_power(tile.power);
                if (w.cells.at(y).at(x - 1).tile == -1)
                    tile.weld &= ~WALL_LEFT;
                if (w.cells.at(y - 1).at(x).tile == -1)
                    tile.weld &= ~WALL_UP;
            }
        }
    }
}

void update(World &w) {
    const float zoom_sensitivity = 0.2f;
    w.camera.zoom *= std::exp(zoom_sensitivity * GetMouseWheelMove());
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        w.camera.target -= GetMouseDelta() / w.camera.zoom;
    }
    w.camera.offset = {(float)GetRenderWidth()/2, (float)GetRenderHeight()/2};

    IVec hover = w.hovered_cell();

    bool undoable = false;

    if (IsKeyPressed(KEY_F10))
        w.editor.on ^= 1;
    if (w.editor.on) {
        w.editor.layout();
        bool pipette = IsKeyPressed(KEY_Q);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || pipette) {
            Vector2 p = GetScreenToWorld2D(GetMousePosition(), w.editor.palette_camera);
            for (const EditorCell &cell : w.editor.palette) {
                if (CheckCollisionPointRec(p, cell.rect)) {
                    w.editor.held = cell;
                }
            }
        }
        if (pipette && hover.x != -1) {
            const Cell &cell = w.cells.at(hover.y).at(hover.x);
            EditorCell ec {};
            if (cell.tile == -1) {
                ec.type = EDITOR_FLOOR;
                ec.floor = cell;
                ec.floor.tile = -1;
                ec.floor.weld = WALL_NONE;
                ec.floor.barrier = WALL_NONE;
            } else {
                ec.type = EDITOR_TILE;
                ec.tile = w.tiles.at(cell.tile);
                ec.tile.weld = WALL_NONE;
            }
            w.editor.held = ec;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && w.editor.held.type != EDITOR_NONE && hover.x != -1 && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || hover != w.prev_hover)) {
            Cell &cell = w.cells.at(hover.y).at(hover.x);
            if (cell.tile != -1)
                w.delete_tile(cell.tile);
            assert(cell.tile == -1);
            if (w.editor.held.type == EDITOR_FLOOR || cell.floor != FLOOR_PASSABLE) {
                Cell floor = w.editor.held.type == EDITOR_FLOOR ? w.editor.held.floor : Cell {.floor = FLOOR_PASSABLE};
                if (cell.floor == FLOOR_VOID && floor.floor != FLOOR_VOID) {
                    auto fixup = [&](int dx, int dy, Wires wire) {
                        Cell &neighbor = w.cells.at(hover.y + dy).at(hover.x + dx);
                        if (neighbor.floor == FLOOR_VOID)
                            neighbor.floor_power.wires &= ~wire;
                    };
                    fixup(-1, 0, WIRE_RIGHT);
                    fixup(+1, 0, WIRE_LEFT);
                    fixup(0, -1, WIRE_DOWN);
                    fixup(0, +1, WIRE_UP);
                }
                cell = floor;
                assert(cell.tile == -1);
            }
            if (w.editor.held.type == EDITOR_TILE) {
                cell.tile = w.tiles.size();
                w.tiles.push_back(w.editor.held.tile);
                w.tiles.back().pos = hover;
            }
            undoable = true;
        }

        int toggle_wires = WIRE_NONE;
        if (IsKeyPressed(KEY_A)) toggle_wires |= WIRE_LEFT;
        if (IsKeyPressed(KEY_D)) toggle_wires |= WIRE_RIGHT;
        if (IsKeyPressed(KEY_W)) toggle_wires |= WIRE_UP;
        if (IsKeyPressed(KEY_S)) toggle_wires |= WIRE_DOWN;
        if (IsKeyPressed(KEY_O)) toggle_wires |= WIRE_CIRCLE;
        if (IsKeyPressed(KEY_J)) toggle_wires |= WIRE_JUMP_CROSS;
        if (IsKeyPressed(KEY_K)) toggle_wires |= WIRE_WHOLE;
        if (toggle_wires != WIRE_NONE && hover.x != -1) {
            Power &power = w.get_power(w.cells.at(hover.y).at(hover.x));
            power.wires ^= toggle_wires;
            power.power &= (power.wires & WIRE_CIRCLE);
            undoable = true;
        }

        if (IsKeyPressed(KEY_P) && hover.x != -1) {
            Power &power = w.get_power(w.cells.at(hover.y).at(hover.x));
            if (power.wires & WIRE_CIRCLE) {
                power.power ^= WIRE_CIRCLE;
                undoable = true;
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && hover.x != -1 && w.prev_hover.x != -1 && std::abs(hover.x - w.prev_hover.x) + std::abs(hover.y - w.prev_hover.y) == 1) {
            Cell &cell = w.cells.at(std::max(hover.y, w.prev_hover.y)).at(std::max(hover.x, w.prev_hover.x));
            Direction dir = hover.x == w.prev_hover.x ? DIR_UP : DIR_LEFT;
            if (IsKeyDown(KEY_LEFT_CONTROL)) {
                cell.barrier ^= 1 << dir;
            } else if (cell.tile == -1) {
                cell.weld ^= 1 << dir;
            } else {
                Tile &tile = w.tiles.at(cell.tile);
                tile.weld ^= 1 << dir;
            }
            undoable = true;
        }

        map_fixup(w);

        if (IsKeyPressed(KEY_F5))
            w.save_to_file();
        if (IsKeyPressed(KEY_F9))
            w.load_from_file();
    }

    if (w.undo_repeat.check(KEY_Z) && w.undo_idx > 1) {
        w.undo_idx -= 1;
        w.load(w.undo.at(w.undo_idx - 1));
    }
    if (w.redo_repeat.check(KEY_X) && w.undo_idx < w.undo.size()) {
        w.undo_idx += 1;
        w.load(w.undo.at(w.undo_idx - 1));
    }

    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            Cell &cell = w.cells.at(y).at(x);
            if (cell.tile != -1)
                assert(w.tiles.at(cell.tile).pos == (IVec{x, y}));
        }
    }
    for (int i = 0; i < w.tiles.size(); ++i) {
        Tile &tile = w.tiles.at(i);
        assert(w.cells.at(tile.pos.y).at(tile.pos.x).tile == i);
    }

    if (undoable)
        w.push_undo();

    w.prev_hover = hover;
}

void draw_power(const Power power, Vector2 pos) {
    const float outer_radius = 20./70;
    const float thickness = 8./70;
    const float crossing_outer_gap = 4./70;
    const float crossing_inner_gap = 10./70;
    const Color colors[2] = {WHITE, BLACK};
    int wires = power.wires;
    if (wires & WIRE_WHOLE) {
        DrawRectangleV(pos, {1.f, 1.f}, colors[!(power.power & WIRE_LEFT)]);
        wires = WIRE_NONE;
    }
    float offset = 0;
    if (wires & (WIRE_CIRCLE | WIRE_JUMP_CROSS)) {
        // Circle or semicircle.
        bool cross = wires & WIRE_JUMP_CROSS;
        DrawRing(pos + Vector2{.5f, .5f}, outer_radius - thickness, outer_radius, cross ? 180 : 0, 360, 30, colors[!(power.power & (WIRE_CIRCLE | WIRE_JUMP_CROSS))]);
        offset = outer_radius - thickness/2;

        if (cross) {
            // Rounded corners.
            DrawCircleV({pos.x + .5f - offset, pos.y + .5f}, thickness * .5f, colors[!(power.power & WIRE_LEFT)]);
            DrawCircleV({pos.x + .5f + offset, pos.y + .5f}, thickness * .5f, colors[!(power.power & WIRE_LEFT)]);
            // Up.
            DrawRectangleV({pos.x + .5f - thickness * .5f, pos.y}, {thickness, .5f - outer_radius - crossing_outer_gap}, colors[!(power.power & WIRE_UP)]);
            // Down.
            float t = outer_radius - thickness - crossing_inner_gap;
            DrawRectangleV({pos.x + .5f - thickness * .5f, pos.y + .5f - t}, {thickness, .5f + t}, colors[!(power.power & WIRE_DOWN)]);
            // Rounded tip.
            DrawCircleV({pos.x + .5f, pos.y + .5f - t}, thickness * .5f, colors[!(power.power & WIRE_DOWN)]);
            wires &= WIRE_LEFT | WIRE_RIGHT;
        }
    }
    if (wires & WIRE_LEFT)
        DrawRectangleV({pos.x, pos.y + .5f - thickness * .5f}, {.5f - offset, thickness}, colors[!(power.power & WIRE_LEFT)]);
    if (wires & WIRE_RIGHT)
        DrawRectangleV({pos.x + .5f + offset, pos.y + .5f - thickness * .5f}, {.5f - offset, thickness}, colors[!(power.power & WIRE_RIGHT)]);
    if (wires & WIRE_UP)
        DrawRectangleV({pos.x + .5f - thickness * .5f, pos.y}, {thickness, .5f - offset}, colors[!(power.power & WIRE_UP)]);
    if (wires & WIRE_DOWN)
        DrawRectangleV({pos.x + .5f - thickness * .5f, pos.y + .5f + offset}, {thickness, .5f - offset}, colors[!(power.power & WIRE_DOWN)]);
    // Rounded corner.
    if ((wires & WIRE_ALL_DIRECTIONS) && offset == 0 && ((~wires) & (WIRE_LEFT | WIRE_RIGHT)) && ((~wires) & (WIRE_UP | WIRE_DOWN)))
        DrawCircleV(pos + Vector2{.5f, .5f}, thickness * .5f, colors[!(power.power & WIRE_ALL_DIRECTIONS)]);
}

void draw_floor(const std::vector<std::vector<Cell>> &cells, IVec idx, Vector2 pos) {
    const Cell &cell = cells.at(idx.y).at(idx.x);
    DrawRectangleV(pos, {1.f, 1.f}, {73, 73, 73, 255});
    if (cell.floor == FLOOR_PASSABLE) {
        const float gap = 5./70;
        DrawRectangleV({pos.x + gap * .5f, pos.y + gap * .5f}, {1.f - gap, 1.f - gap}, {65, 65, 65, 255});
    } else if (cell.floor == FLOOR_WALL) {
        const float margin = .04;
        Vector2 p = pos;
        Vector2 q = pos + Vector2{1.f, 1.f};
        if (!(cell.weld & WALL_LEFT)) p.x += margin;
        if (!(cell.weld & WALL_UP)) p.y += margin;
        if (!(cells.at(idx.y).at(idx.x + 1).weld & WALL_LEFT)) q.x -= margin;
        if (!(cells.at(idx.y + 1).at(idx.x).weld & WALL_UP)) q.y -= margin;
        DrawRectangleV(p, q - p, {47, 47, 47, 255});
    } else if (cell.floor == FLOOR_TRIGGER) {
        DrawRectangleV(pos + Vector2{.1f, .1f}, {.8f, .8f}, {104, 104, 104, 255});
        DrawRectangleV(pos + Vector2{.2f, .2f}, {.6f, .6f}, {72, 72, 72, 255});
        DrawRectangleV(pos + Vector2{.3f, .3f}, {.4f, .4f}, {104, 104, 104, 255});
        if ((cells.at(idx.y - 1).at(idx.x).barrier & WALL_LEFT) || (cells.at(idx.y).at(idx.x - 1).barrier & WALL_UP))
            DrawRectangleV(pos, {.2f, .2f}, {190, 190, 190, 255});
        if ((cells.at(idx.y - 1).at(idx.x + 1).barrier & WALL_LEFT) || (cells.at(idx.y).at(idx.x + 1).barrier & WALL_UP))
            DrawRectangleV({pos.x + .8f, pos.y}, {.2f, .2f}, {190, 190, 190, 255});
        if ((cells.at(idx.y + 1).at(idx.x).barrier & WALL_LEFT) || (cells.at(idx.y + 1).at(idx.x - 1).barrier & WALL_UP))
            DrawRectangleV({pos.x, pos.y + .8f}, {.2f, .2f}, {190, 190, 190, 255});
        if ((cells.at(idx.y + 1).at(idx.x + 1).barrier & WALL_LEFT) || (cells.at(idx.y + 1).at(idx.x + 1).barrier & WALL_UP))
            DrawRectangleV({pos.x + .8f, pos.y + .8f}, {.2f, .2f}, {190, 190, 190, 255});
    }

    draw_power(cell.floor_power, pos);

    if (cell.floor == FLOOR_VOID) {
        const float margin = 10./70;
        const Color outer = {172, 172, 172, 255};
        const Color inner = {0, 0, 0, 0};
        if (cells.at(idx.y).at(idx.x - 1).floor != FLOOR_VOID)
            DrawRectangleGradientEx({pos.x, pos.y, margin, 1.f}, outer, outer, inner, inner);
        if (cells.at(idx.y - 1).at(idx.x).floor != FLOOR_VOID)
            DrawRectangleGradientEx({pos.x, pos.y, 1.f, margin}, outer, inner, inner, outer);
        if (cells.at(idx.y).at(idx.x + 1).floor != FLOOR_VOID)
            DrawRectangleGradientEx({pos.x + 1.f - margin, pos.y, margin, 1.f}, inner, inner, outer, outer);
        if (cells.at(idx.y + 1).at(idx.x).floor != FLOOR_VOID)
            DrawRectangleGradientEx({pos.x, pos.y + 1.f - margin, 1.f, margin}, inner, outer, outer, inner);

        uint8_t wall_wires = cell.floor_power.wires & WIRE_ALL_DIRECTIONS;
        if (wall_wires)
            draw_power({.wires = wall_wires, .power = (uint8_t)(cell.floor_power.power & wall_wires)}, pos);
    }

    if (cell.barrier) {
        const float thickness = 8./70;
        const Color col {120, 80, 120, 255};
        const int segments = 8;
        if (cell.barrier & WALL_LEFT) {
            if (cell.barrier_active & WALL_LEFT) {
                DrawRectangleV({pos.x - thickness * .5f, pos.y}, {thickness, 1.f}, col);
            } else {
                for (int i = 0; i < segments; i += 2)
                    DrawRectangleV({pos.x - thickness * .5f, pos.y + (float)i / (float)segments}, {thickness, 1.f / (float)segments}, col);
            }
        }
        if (cell.barrier & WALL_UP) {
            if (cell.barrier_active & WALL_LEFT) {
                DrawRectangleV({pos.x, pos.y - thickness * .5f}, {1.f, thickness}, col);
            } else {
                for (int i = 0; i < segments; i += 2)
                    DrawRectangleV({pos.x + (float)i / (float)segments, pos.y - thickness * .5f}, {1.f / (float)segments, thickness}, col);
            }
        }
    }
}

void draw_tile(const Tile &tile, int weld, Vector2 pos) {
    if (tile.type != TILE_BLACK) {
        Color col = PINK;
        switch (tile.type) {
        case TILE_GREEN: col = {105, 152, 88, 255}; break;
        case TILE_YELLOW: col = {206, 183, 111, 255}; break;
        case TILE_BLUE: col = {86, 126, 160, 255}; break;
        case TILE_PURPLE: col = {129, 104, 158, 255}; break;
        case TILE_ORANGE: col = {176, 121, 49, 255}; break;
        case TILE_RED: col = {172, 71, 71, 255}; break;
        case TILE_GRAY: col = {97, 97, 97, 255}; break;
        case TILE_BLACK: assert(false);
        }

        const float margin = .04;
        Vector2 p = pos;
        Vector2 q = pos + Vector2{1.f, 1.f};
        if (!(weld & WALL_LEFT)) p.x += margin;
        if (!(weld & WALL_UP)) p.y += margin;
        if (!(weld & WALL_RIGHT)) q.x -= margin;
        if (!(weld & WALL_DOWN)) q.y -= margin;
        DrawRectangleV(p, q - p, col);
    }

    draw_power(tile.power, pos);

    if (tile.type == TILE_BLACK) {
        const float thickness = 3./70;
        const Color col = {109, 109, 109, 255};
        if (!(weld & WALL_LEFT))
            DrawRectangleV(pos, {thickness, 1.f}, col);
        if (!(weld & WALL_UP))
            DrawRectangleV(pos, {1.f, thickness}, col);
        if (!(weld & WALL_RIGHT))
            DrawRectangleV({pos.x + 1.f - thickness, pos.y}, {thickness, 1.f}, col);
        if (!(weld & WALL_DOWN))
            DrawRectangleV({pos.x, pos.y + 1.f - thickness}, {1.f, thickness}, col);
    }
}

void render(const World &w) {
    Color clear_color;
    if (w.editor.on)
        clear_color = {100, 100, 200, 255};
    else if (w.cells.at(1).at(1).floor_power.power & WIRE_RIGHT)
        clear_color = WHITE;
    else
        clear_color = BLACK;
    ClearBackground(clear_color);

    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            const Cell &cell = w.cells[y][x];
            draw_floor(w.cells, {x, y}, {(float)x, (float)y});
            if (cell.tile != -1) {
                int weld = w.tiles.at(cell.tile).weld;
                int t = w.cells.at(y).at(x + 1).tile;
                if (t != -1 && (w.tiles.at(t).weld & WALL_LEFT)) weld |= WALL_RIGHT;
                t = w.cells.at(y + 1).at(x).tile;
                if (t != -1 && (w.tiles.at(t).weld & WALL_UP)) weld |= WALL_DOWN;
                draw_tile(w.tiles.at(cell.tile), weld, {(float)x, (float)y});
            }
        }
    }
}

void render_ui(World &w) {
    if (w.editor.on) {
        BeginMode2D(w.editor.palette_camera);

        DrawRectangleV({-w.editor.palette_width, 0}, {w.editor.palette_width, (float)GetRenderHeight() / w.editor.palette_camera.zoom}, DARKBLUE);
        std::vector<std::vector<Cell>> cells(3, std::vector<Cell>(3));
        for (const EditorCell &cell : w.editor.palette) {
            if (cell.equivalent(w.editor.held)) {
                const float s = 0.2;
                DrawRectangleV({cell.rect.x - s/2, cell.rect.y - s/2}, {1.f + s, 1.f + s}, WHITE);
            }
            if (cell.type == EDITOR_FLOOR) {
                cells[1][1] = cell.floor;
                draw_floor(cells, {1, 1}, {cell.rect.x, cell.rect.y});
            } else if (cell.type == EDITOR_TILE) {
                draw_tile(cell.tile, 0, {cell.rect.x, cell.rect.y});
            }
        }

        EndMode2D();
    }
}

int main() {
    InitWindow(2000, 2000, "hello world");
    SetTargetFPS(144);

    World w;
    w.init_start({110, 124});
    w.init_finish();

    while (true) {
        BeginDrawing();

        update(w);

        SetWindowTitle(TextFormat("fps: %d", GetFPS()));

        BeginMode2D(w.camera);

        render(w);

        EndMode2D();

        render_ui(w);

        EndDrawing();

        if (WindowShouldClose())
            break;
    }
}
