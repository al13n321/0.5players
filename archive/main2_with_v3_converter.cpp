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

enum Direction {
    DIR_LEFT = 0,
    DIR_UP = 1,
    DIR_RIGHT = 2,
    DIR_DOWN = 3,
};
Direction dir_opposite(int d) {
    assert(d >= 0 && d < 4);
    return (Direction)((d + 2) & 3);
}
Direction dir_clockwise(int d) {
    assert(d >= 0 && d < 4);
    return (Direction)((d + 1) & 3);
}
Direction dir_counterclockwise(int d) {
    assert(d >= 0 && d < 4);
    return (Direction)((d + 3) & 3);
}

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
    WIRE_BRIDGE = 0x20,
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
    IVec operator+(const IVec &r) const { return {x + r.x, y + r.y}; }
    IVec operator-(const IVec &r) const { return {x - r.x, y - r.y}; }
    IVec & operator+=(const IVec &r) { *this = *this + r; return *this; }
    IVec & operator-=(const IVec &r) { *this = *this - r; return *this; }

    Vector2 to_float() const { return {(float)x, (float)y}; }
    Vector2 operator*(float r) const { return {(float)x * r, (float)y * r}; }
};

const IVec dir_vec[4] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};

struct Power {
    uint8_t wires = WIRE_NONE;
    uint8_t power = WIRE_NONE;

    // Information for building a graph.
    std::array<int, 4> vertex {-1, -1, -1, -1}; // Direction -> vertex idx in Graph
    int edge_middle_exposed = WALL_NONE;
    int edge_rear_end_exposed = WALL_NONE;

    bool operator==(const Power &r) const { return wires == r.wires && power == r.power; }

    void clear_graph_state() {
        vertex = {-1, -1, -1, -1};
        edge_middle_exposed = WALL_NONE;
        edge_rear_end_exposed = WALL_NONE;
    }
};

struct Tile { // (i.e. movable piece)
    TileType type = TILE_GREEN;
    // Which tiles are joined together. This flag is set on the right/bottom of the two tiles that are joined.
    uint8_t weld = WALL_NONE; // only WALL_LEFT and WALL_UP are allowed
    bool selected = false;
    IVec pos;
    // End of memcpy-serialized part.
    Power power;

    bool moving = false;

    bool operator==(const Tile &) const = default;
};

struct Cell {
    FloorType floor : 4 = FLOOR_VOID;
    uint8_t weld : 4 = WALL_NONE; // only WALL_LEFT and WALL_UP are allowed
    uint8_t barrier = WALL_NONE; // retractable wall; only WALL_LEFT and WALL_UP are allowed
    // End of memcpy-serialized part.
    Power floor_power;

    uint8_t barrier_active = WALL_NONE;

    // Tile that occupies at least half of this cell. If Tile.moving is true, World.move.{dist,dir} tells how this tile is offset from the center of this cell.
    int tile = -1;

    bool operator==(const Cell &) const = default;
};

enum MoveDist {
    // Moved by less than wire width. Sheared wires are still connected.
    // Gap between black tiles became wide enough to count as a donut hole.
    DIST_EPSILON,
    // Moved by a little more than wire width. Sheared wires are disconnected.
    DIST_OVER_WIRE_WIDTH,
    // Moved by around 1/4 of a cell. Tile edge can touch a floor circle.
    DIST_CIRCLE_RADIUS,
    // Tile edges can touch floor half-lines on both sides.
    DIST_HALF,

    // (After HALF we move the tiles over to their new cells and switch to counting distance in reverse.)
};
const float dist_timing[] = {0, 0.05, 9./70, 0.25, 0.5};

enum MoveStage {
    STAGE_NONE,
    STAGE_FIRST_HALF,
    STAGE_SECOND_HALF,
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
    bool on = false;
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

struct MoveState {
    MoveStage stage = STAGE_NONE;
    MoveDist dist = DIST_HALF;
    float elapsed = 0; // where we are in the animation, [0, 1]
    Direction dir = DIR_LEFT;
};

enum ActionType {
    ACTION_SELECT,
    ACTION_MOVE,
};
struct Action {
    ActionType type = ACTION_SELECT;
    IVec pos; // if ACTION_SELECT
    Direction dir; // if ACTION_MOVE
};

struct World {
    IVec size; // first and last cell of each row and column are sentinel cells that are not rendered and not updated
    std::vector<std::vector<Cell>> cells;
    std::vector<Tile> tiles;

    std::string save_file_path = "save.bin";
    float default_animation_rate = 2.; // moves per second
    float animation_speedup = 4.; // animations get this many times faster for every buffered input

    std::vector<std::vector<char>> undo;
    size_t undo_idx = 0; // undo[undo_idx - 1] is the current state
    KeyRepeater undo_repeat;
    KeyRepeater redo_repeat;
    Editor editor;

    Camera2D camera {};
    IVec prev_hover {-1, -1};

    MoveState move;
    std::vector<Action> buffered_actions;
    float animation_rate = 0;

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

        size_t version = 3;
        save_pod(&version, out);
        save_pod(&size, out);
        size_t t = tiles.size();
        save_pod(&t, out);

        const size_t power_bytes = offsetof(Power, power) + sizeof(uint8_t);
        static_assert(power_bytes == 2, "");
        const size_t cell_bytes = offsetof(Cell, barrier) + sizeof(uint8_t);
        static_assert(cell_bytes == 2, "");
        const size_t tile_bytes = offsetof(Tile, pos) + sizeof(IVec);
        static_assert(tile_bytes == 12, "");
        for (int y = 1; y + 1 < size.y; ++y) {
            for (int x = 1; x + 1 < size.x; ++x) {
                size_t pos = out.size();
                out.resize(pos + cell_bytes + power_bytes);
                memcpy(&out[pos], &cells[y][x], cell_bytes);
                memcpy(&out[pos + cell_bytes], &cells[y][x].floor_power, power_bytes);
            }
        }
        for (int i = 0; i < tiles.size(); ++i) {
            size_t pos = out.size();
            out.resize(pos + tile_bytes + power_bytes);
            memcpy(&out[pos], &tiles[i], tile_bytes);
            memcpy(&out[pos + tile_bytes], &tiles[i].power, power_bytes);
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

        if (version != 2 && version != 3)
            std::abort();

        load_pod(&size, &in, end);
        if (size.x < 2 || size.y < 2 || 1000000 / size.x / size.y == 0)
            std::abort();
        cells.resize(size.y);
        for (int y = 0; y < size.y; ++y) {
            cells[y].assign(size.x, {});
        }
        size_t t;
        load_pod(&t, &in, end);
        if (t > 1000000)
            std::abort();
        tiles.assign(t, {});

        if (version == 2) {
            struct PowerV2 {
                uint8_t wires;
                uint8_t power;
            };
            struct TileV2 {
                TileType type;
                uint8_t weld;
                PowerV2 power;
                IVec pos;
            };
            struct CellV2 {
                FloorType floor : 4;
                uint8_t weld : 4;
                uint8_t barrier : 4;
                uint8_t barrier_active : 4;
                PowerV2 floor_power;
            };

            const size_t cell_bytes = offsetof(CellV2, floor_power) + sizeof(PowerV2);
            static_assert(cell_bytes == 4, "");
            for (int y = 1; y + 1 < size.y; ++y) {
                for (int x = 1; x + 1 < size.x; ++x) {
                    if (end - in < cell_bytes)
                        std::abort();
                    CellV2 a;
                    memcpy(&a, in, cell_bytes);
                    Cell &b = cells[y][x];
                    b.floor = a.floor;
                    b.weld = a.weld;
                    b.barrier = a.barrier;
                    b.floor_power.wires = a.floor_power.wires;
                    b.floor_power.power = a.floor_power.power;
                    in += cell_bytes;
                }
            }
            const size_t tile_bytes = offsetof(TileV2, pos) + sizeof(IVec);
            static_assert(tile_bytes == 12, "");
            for (int i = 0; i < tiles.size(); ++i) {
                if (end - in < tile_bytes)
                    std::abort();
                TileV2 a;
                memcpy(&a, in, tile_bytes);
                Tile &b = tiles[i];
                b.type = a.type;
                b.weld = a.weld;
                b.power.wires = a.power.wires;
                b.power.power = a.power.power;
                b.pos = a.pos;
                in += tile_bytes;
            }
        } else {
            const size_t power_bytes = offsetof(Power, power) + sizeof(uint8_t);
            static_assert(power_bytes == 2, "");
            const size_t cell_bytes = offsetof(Cell, barrier) + sizeof(uint8_t);
            static_assert(cell_bytes == 2, "");
            const size_t tile_bytes = offsetof(Tile, pos) + sizeof(IVec);
            static_assert(tile_bytes == 12, "");
            for (int y = 1; y + 1 < size.y; ++y) {
                for (int x = 1; x + 1 < size.x; ++x) {
                    if (end - in < cell_bytes + power_bytes)
                        std::abort();
                    memcpy(&cells[y][x], in, cell_bytes);
                    in += cell_bytes;
                    memcpy(&cells[y][x].floor_power, in, power_bytes);
                    in += power_bytes;
                }
            }
            for (int i = 0; i < tiles.size(); ++i) {
                if (end - in < tile_bytes + power_bytes)
                    std::abort();
                memcpy(&tiles[i], in, tile_bytes);
                in += tile_bytes;
                memcpy(&tiles[i].power, in, power_bytes);
                in += power_bytes;
            }
        }

        /*
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
            }*/

        for (int i = 0; i < tiles.size(); ++i) {
            Tile &tile = tiles.at(i);
            cells.at(tile.pos.y).at(tile.pos.x).tile = i;
        }

        // (tl;dr: everyting is fast, optimization not needed, don't worry about it)
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

    Cell & get_cell(IVec p) { return cells.at(p.y).at(p.x); }
    const Cell & get_cell(IVec p) const { return cells.at(p.y).at(p.x); }
};

struct Edge {
    int from = -1;
    int to = -1;
    IVec dir; // rough direction in space, for walking around donut holes

    bool operator<(const Edge &r) const {
        if (from != r.from)
            return from < r.from;
        // Sort by dir angle. Cw vs ccw doesn't matter.
        bool half1 = std::make_pair(dir.x, dir.y) < std::make_pair(0, 0);
        bool half2 = std::make_pair(r.dir.x, r.dir.y) < std::make_pair(0, 0);
        if (half1 != half2)
            return half1 < half2;
        return dir.x * r.dir.y < dir.y * r.dir.x;
    }
};
enum VertexType {
    VERTEX_FLOOR,
    VERTEX_TILE,
};
struct Vertex {
    VertexType type = VERTEX_FLOOR;
    IVec pos;
    int ti = -1;
    bool vertical = false; // if WIRE_BRIDGE, this tells whether this is the horizontal or the vertical wire
    bool whole = false; // WIRE_WHOLE
    bool lit = false;
    bool lit_circle = false;

    // Indices in sorted `e`.
    int e_start = 0;
    int e_end = 0;

    // DFS tree state.
    int parent = -1; // -1 means not visited, -2 means root
    int parent_edge_id = -1;
    int wires_above = 0; // number of ancestors with `whole` == false, not counting this Vertex itself
};
struct Graph {
    std::vector<Vertex> v;
    std::vector<Edge> e;
};

void update_power(World &w) {
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            w.cells.at(y).at(x).floor_power.clear_graph_state();
        }
    }
    for (int ti = 0; ti < w.tiles.size(); ++ti) {
        w.tiles.at(ti).power.clear_graph_state();
    }

    Graph g;
    MoveDist dist = w.move.dist;

    // Create all vertices.
    auto add_vertices = [&](Power &power, VertexType type, IVec pos, int ti) {
        if (power.wires == WIRE_NONE)
            return;
        if (power.wires & WIRE_BRIDGE) {
            int horizontal = g.v.size();
            g.v.push_back({.type = type, .pos = pos, .ti = ti, .lit = power.power & (WIRE_LEFT | WIRE_RIGHT)});
            int vertical = g.v.size();
            g.v.push_back({.type = type, .pos = pos, .ti = ti, .vertical = true, .lit = power.power & (WIRE_UP | WIRE_DOWN)});
            power.vertex = {horizontal, vertical, horizontal, vertical};
        } else {
            int v = g.v.size();
            g.v.push_back({.type = type, .pos = pos, .ti = ti, .whole = power.wires & WIRE_WHOLE, .lit = power.power, .lit_circle = power.power & WIRE_CIRCLE});
            for (int dir = 0; dir < 4; ++dir)
                if (power.wires & (1 << dir))
                    power.vertex[dir] = v;
        }
    };
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            IVec pos {x, y};
            Cell &cell = w.get_cell(pos);

            if (cell.tile != -1) {
                // Extinguish floor circle if covered.
                cell.floor_power.power &= ~WIRE_CIRCLE;

                Tile &tile = w.tiles.at(cell.tile);
                add_vertices(tile.power, VERTEX_TILE, pos, cell.tile);
            }

            // (This must be after extinguishing floor circle above.)
            add_vertices(cell.floor_power, VERTEX_FLOOR, pos, -1);

            // Check which floor edges are covered by tiles.
            for (int dir = 0; dir < 4; ++dir) {
                if (cell.floor_power.vertex[dir] == -1)
                    continue;
                const Cell &cell2 = w.get_cell(pos + dir_vec[dir_opposite(w.move.dir)]); // tile may be moving into `cell` from `cell2`

                // Cases where the whole edge is covered.
                if (w.move.dir == dir_opposite(dir) && cell2.tile != -1 && w.tiles.at(cell2.tile).moving)
                    continue;
                if (cell.tile != -1 && (w.move.dir == dir || !w.tiles.at(cell.tile).moving))
                    continue;
                // Whole edge is not covered.
                if (w.move.dir == dir || w.move.dir == dir_opposite(dir)) {
                    cell.floor_power.edge_middle_exposed |= 1 << dir;
                    cell.floor_power.edge_rear_end_exposed |= 1 << dir;
                    continue;
                }

                // Tiles are moving sideways. Edge may be partially covered.

                // Hack: we consider wire covered by sideways moving tile even if it's moved by DIST_HALF. That's geometrically incorrect (wire is only half covered),
                // but the incorrectness doesn't cause problems with current rules. We can't just mark it as uncovered because then we'd get
                // incorrect connections across diagonally touching blocks (both sides will say their wire is "uncovered", but actually they have different halves of the wire uncovered, with no overlap).
                if (cell.tile == -1)
                    cell.floor_power.edge_middle_exposed |= 1 << dir;
                if (cell2.tile == -1)
                    cell.floor_power.edge_rear_end_exposed |= 1 << dir;
            }
            if (!(cell.floor_power.wires & WIRE_WHOLE))
                cell.floor_power.edge_rear_end_exposed = WALL_NONE;
        }
    }

    // Create edges.
    // We're careless about duplicate edges because our dfs ignores them.
    // E.g. edges between floor tiles are added twice (from each side), but edges from partially moved tile may be added once or twice.
    /*
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            IVec pos {x, y};
            Cell &cell = w.get_cell(pos);
            for (int dir = 0; dir < 4; ++dir) {
                Cell &cell2 = w.get_cell(pos + dir_vec[dir]);
                if (cell.floor_power.wire & cell2.floor_power.wire & WIRE_WHOLE) {
                    asdqwe;
                } else {
                if (cell.tile != -1) {
                    Tile &tile = w.tiles.at(cell.tile);
                    if (!tile.moving || w.move.dir != dir_opposite(dir))
                        continue; // floor edge wire is covered by cell's tile
                    if (w.move_dir == dir_opposite(dir) && cell2.tile && w.tiles.at(cell2.tile).moving)
                        continue; // floor edge wire is covered by cell2's tile
                }
                if (w.move.dir == dir_opposite(dir)) {
                    
                    // Determine if this floor edge wire is covered by a tile. `continue` if so.
                    asdqwe;
                } else 
                int ti2 = w.get_cell(pos + dir_vec[dir])
                asdqwe;
            }
        }
        }*/
}

void map_fixup(World &w) {
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            auto fixup_power = [](Power &power) {
                if (power.wires & WIRE_WHOLE)
                    power.wires = WIRE_WHOLE | WIRE_ALL_DIRECTIONS;
                if (power.wires & WIRE_BRIDGE)
                    power.wires = WIRE_BRIDGE | WIRE_ALL_DIRECTIONS;
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

// Not called during moves.
void handle_misc_input(World &w, IVec hover) {
    bool undoable = false;

    if (w.editor.on) {
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
        if (IsKeyPressed(KEY_J)) toggle_wires |= WIRE_BRIDGE;
        if (IsKeyPressed(KEY_K)) toggle_wires |= WIRE_WHOLE;

        auto get_power = [&](Cell &cell) -> Power & {
            return cell.tile == -1 ? cell.floor_power : w.tiles.at(cell.tile).power;
        };

        if (toggle_wires != WIRE_NONE && hover.x != -1) {
            Power &power = get_power(w.cells.at(hover.y).at(hover.x));
            power.wires ^= toggle_wires;
            power.power &= (power.wires & WIRE_CIRCLE);
            undoable = true;
        }

        if (IsKeyPressed(KEY_P) && hover.x != -1) {
            Power &power = get_power(w.cells.at(hover.y).at(hover.x));
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
        if (IsKeyPressed(KEY_F9)) {
            w.load_from_file();
            undoable = true;
        }
    }

    if (w.move.stage == STAGE_NONE) {
        if (w.undo_repeat.check(KEY_Z) && w.undo_idx > 1) {
            w.undo_idx -= 1;
            w.load(w.undo.at(w.undo_idx - 1));
        }
        if (w.redo_repeat.check(KEY_X) && w.undo_idx < w.undo.size()) {
            w.undo_idx += 1;
            w.load(w.undo.at(w.undo_idx - 1));
        }
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
}

void check_consistency(const World &w) {
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            const Cell &cell = w.cells.at(y).at(x);
            if (cell.tile != -1)
                assert(w.tiles.at(cell.tile).pos == (IVec{x, y}));
        }
    }
    for (int i = 0; i < w.tiles.size(); ++i) {
        const Tile &tile = w.tiles.at(i);
        assert(w.cells.at(tile.pos.y).at(tile.pos.x).tile == i);
    }    
}

void buffer_inputs(World &w, IVec hover) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (hover.x != -1)
            w.buffered_actions.push_back(Action {.type = ACTION_SELECT, .pos = hover});
    } else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hover.x != -1 && w.prev_hover.x != -1 && std::abs(hover.x - w.prev_hover.x) + std::abs(hover.y - w.prev_hover.y) == 1) {
        Direction dir = hover.x == w.prev_hover.x
            ? hover.y < w.prev_hover.y ? DIR_UP : DIR_DOWN
            : hover.x < w.prev_hover.x ? DIR_LEFT : DIR_RIGHT;
        w.buffered_actions.push_back(Action {.type = ACTION_MOVE, .dir = dir});
    }

    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))
        w.buffered_actions.push_back(Action {.type = ACTION_MOVE, .dir = DIR_LEFT});
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
        w.buffered_actions.push_back(Action {.type = ACTION_MOVE, .dir = DIR_UP});
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
        w.buffered_actions.push_back(Action {.type = ACTION_MOVE, .dir = DIR_RIGHT});
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        w.buffered_actions.push_back(Action {.type = ACTION_MOVE, .dir = DIR_DOWN});
}

int get_welded_neighbor_tile(const World &w, int ti, int dir) {
    const Tile &tile = w.tiles.at(ti);
    IVec p = tile.pos + dir_vec[dir];
    const Cell &cell = w.cells.at(p.y).at(p.x);
    if (cell.tile == -1)
        return -1;
    const Tile &tile2 = w.tiles.at(cell.tile);
    if (dir < 2 && !(tile.weld & (1 << dir)))
        return -1;
    if (dir >= 2 && !(tile2.weld & (1 << (dir - 2))))
        return -1;
    return cell.tile;
}

std::vector<int> find_welded_tile_group(World &w, int ti) {
    std::vector<int> res;
    std::vector<int> stack {ti};
    std::unordered_set<int> seen {ti};
    while (!stack.empty()) {
        ti = stack.back();
        stack.pop_back();
        res.push_back(ti);
        const Tile &tile = w.tiles.at(ti);
        for (int dir = 0; dir < 4; ++dir) {
            int ti2 = get_welded_neighbor_tile(w, ti, dir);
            if (ti2 != -1 && !seen.count(ti2)) {
                seen.insert(ti2);
                stack.push_back(ti2);
            }
        }
    }
    assert(res.size() == seen.size());
    return res;
}

bool select_tile(World &w, IVec pos) {
    for (Tile &tile : w.tiles)
        tile.selected = false;
    Cell &cell = w.cells.at(pos.y).at(pos.x);
    if (cell.tile == -1)
        return false;
    std::vector<int> tiles = find_welded_tile_group(w, cell.tile);
    for (int ti: tiles)
        w.tiles.at(ti).selected = true;
    return true;
}

void move_start(World &w, Direction dir) {
}

bool move_advance_if_needed(World &w) {
    return false;
}

void update(World &w) {
    const float zoom_sensitivity = 0.3f;
    w.camera.zoom *= std::exp(zoom_sensitivity * GetMouseWheelMove());
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        w.camera.target -= GetMouseDelta() / w.camera.zoom;
    }
    w.camera.offset = {(float)GetRenderWidth()/2, (float)GetRenderHeight()/2};

    w.animation_rate = w.default_animation_rate * powf(w.animation_speedup, (float)w.buffered_actions.size());

    IVec hover = w.hovered_cell();

    if (IsKeyPressed(KEY_F10))
        w.editor.on ^= 1;
    if (w.editor.on)
        w.editor.layout();
    if (w.move.stage == STAGE_NONE)
        handle_misc_input(w, hover);

    if (w.editor.on)
        w.buffered_actions.clear();
    else if (w.buffered_actions.size() < 10)
        buffer_inputs(w, hover);

    if (!w.buffered_actions.empty() && w.move.stage == STAGE_NONE) {
        Action action = w.buffered_actions[0];
        w.buffered_actions.erase(w.buffered_actions.begin());
        if (action.type == ACTION_SELECT) {
            if (!select_tile(w, action.pos))
                w.buffered_actions.clear();
        } else if (action.type == ACTION_MOVE) {
            move_start(w, action.dir);
        } else {
            assert(false);
        }
    }

    if (w.move.stage != STAGE_NONE)
        w.move.elapsed += w.animation_rate * GetFrameTime();
    while (move_advance_if_needed(w));

    check_consistency(w);

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
    if (wires & (WIRE_CIRCLE | WIRE_BRIDGE)) {
        // Circle or semicircle.
        bool cross = wires & WIRE_BRIDGE;
        DrawRing(pos + Vector2{.5f, .5f}, outer_radius - thickness, outer_radius, cross ? 180 : 0, 360, 30, colors[!(power.power & (WIRE_CIRCLE | WIRE_BRIDGE))]);
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

void draw_selection_indicator(const World &w) {
    const float thickness = 0.07;
    const float length = 0.2;
    const float gap = 0.04 + 0.02 * sin(GetTime() * 3.);
    const Color col = WHITE;

    auto have_selected_tile_at = [&](IVec p) {
        int ti = w.get_cell(p).tile;
        return ti != -1 && w.tiles.at(ti).selected;
    };
    for (int ti = 0; ti < w.tiles.size(); ++ti) {
        const Tile &tile = w.tiles[ti];
        if (!tile.selected)
            continue;

        // Walk the perimeter of the cell clockwise, look where the edge turns, render the corresponding line/angle shape.
        // It's as if we're walking the perimeter of the whole selected group of tiles, but out of order.
        // So every needed shape gets drawn exactly once even in complicated cases like two tiles touching corners diagonally.
        for (int side = 0; side < 4; ++side) {
            IVec up = dir_vec[side];
            IVec down = dir_vec[dir_opposite(side)];
            IVec right = dir_vec[dir_clockwise(side)];
            IVec left = dir_vec[dir_counterclockwise(side)];
            if (have_selected_tile_at(tile.pos + up))
                continue;
            std::array<Vector2, 6> points; // triangle fan
            IVec gap_dir = up;
            if (have_selected_tile_at(tile.pos + up + right)) {
                // Inner corner.
                gap_dir += left;
                points = {Vector2{}, up*(thickness + length), up*(thickness + length) + left*thickness, up*thickness + left*thickness, up*thickness + left*(thickness + length), left*(thickness + length)};
            } else if (have_selected_tile_at(tile.pos + right)) {
                // Flat side.
                points = {Vector2{}, Vector2{}, right*length, right*length + up*thickness, left*length + up*thickness, left*length};
            } else {
                // Outer corner.
                gap_dir += right;
                points = {Vector2{}, down*length, down*length + right*thickness, up*thickness + right*thickness, up*thickness + left*length, left*length};
            }
            Vector2 corner_pos = tile.pos.to_float() + Vector2{.5f, .5f} + (up + right).to_float()*.5f;
            corner_pos += gap_dir.to_float() * gap;
            for (Vector2 &p : points)
                p += corner_pos;
            DrawTriangleFan(&points[0], 6, col);
        }
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
    draw_selection_indicator(w);
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
    if (!w.load_from_file()) {
        w.init_start({110, 124});
        w.editor.on = true;
    }
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
