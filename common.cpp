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

// Set of Direction-s.
enum Walls {
    WALL_NONE = 0,

    WALL_LEFT = 1 << DIR_LEFT,
    WALL_UP = 1 << DIR_UP,

    WALL_RIGHT = 1 << DIR_RIGHT,
    WALL_DOWN = 1 << DIR_DOWN,
};

enum Wires {
    WIRE_NONE = 0,

    // These can be combined.
    WIRE_LEFT = WALL_LEFT,
    WIRE_UP = WALL_UP,
    WIRE_RIGHT = WALL_RIGHT,
    WIRE_DOWN = WALL_DOWN,
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

struct IVec {
    int x = 0;
    int y = 0;

    bool operator==(const IVec &) const = default;
    bool operator!=(const IVec &) const = default;
    IVec operator+(const IVec &r) const { return {x + r.x, y + r.y}; }
    IVec operator-(const IVec &r) const { return {x - r.x, y - r.y}; }
    IVec operator*(int r) const { return {x * r, y * r}; }
    IVec & operator+=(const IVec &r) { *this = *this + r; return *this; }
    IVec & operator-=(const IVec &r) { *this = *this - r; return *this; }
    IVec & operator*=(int r) { *this = *this * r; return *this; }

    Vector2 to_float() const { return {(float)x, (float)y}; }
    Vector2 operator*(float r) const { return {(float)x * r, (float)y * r}; }

    bool operator<(const IVec &r) const { return std::make_pair(x, y) < std::make_pair(r.x, r.y); }

    int manhattan(const IVec &r) const { return abs(x - r.x) + abs(y - r.y); }
};

const IVec dir_vec[4] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};

struct Power {
    uint8_t wires = WIRE_NONE;
    uint8_t power = WIRE_NONE;

    // Information for building a graph.
    std::array<int, 5> vertex {-1, -1, -1, -1, -1}; // Direction -> vertex idx in Graph; last element is for cell center
    int edge_middle_exposed = WALL_NONE; // which sides of the square have their midpoints conductive and not covered by tiles
    int edge_rear_end_exposed = WALL_NONE; // same but instead of midpoint it's the corner in direction opposite of move.dir

    bool operator==(const Power &r) const { return wires == r.wires && power == r.power; }

    void clear_graph_state() {
        vertex = {-1, -1, -1, -1, -1};
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
    DIST_HALF_MINUS_EPSILON,

    // (After HALF we move the tiles over to their new cells and switch to counting distance in reverse.)
};
const std::array<float, 4> dist_timing {0.05, 9./70, 0.25, 0.45};

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

// A thing for repeating an action if key is held, like when typing goes brrrrrrrrrrrrrrrrr.
struct KeyRepeater {
    double pressed_at = 0;
    long repeats = 0;

    bool check(std::initializer_list<int> keys) {
        bool pressed = false;
        bool down = false;
        for (int key: keys) {
            pressed |= IsKeyPressed(key);
            down |= IsKeyDown(key);
        }
        if (pressed) {
            pressed_at = GetTime();
            repeats = 0;
            return true;
        }
        if (!down) {
            pressed_at = 0;
            repeats = 0;
            return false;
        }
        assert(pressed_at != 0);

        double now = GetTime();
        double elapsed = now - pressed_at;
        long prev = repeats;
        repeats = (long)(std::max(0., elapsed - .2) * 20.);
        return repeats > prev;
    }
};

struct MoveState {
    MoveStage stage = STAGE_NONE;
    MoveDist dist = DIST_EPSILON;
    float elapsed = 0; // where we are in the animation, [0, 1]
    Direction dir = DIR_LEFT;

    int manual_advance = -1;
};

enum ActionType {
    ACTION_SELECT = 0,
    ACTION_MOVE = 1,
    // Mouse moved to tile at `pos`.
    // Note that at the time of input buffering we don't know what tile it is or how far it needs to move.
    // This action is not saved in replay, it's translated into a sequence of ACTION_MOVEs instead.
    ACTION_DRAG = 2,
};
struct Action {
    ActionType type = ACTION_SELECT;
    // If ACTION_SELECT, position of the clicked tile.
    // If ACTION_DRAG, position to which the tile is being dragged.
    IVec pos;
    // If ACTION_MOVE, move direction.
    Direction dir;
};

enum WhichMesh {
    MESH_TILE_QUARTER_0 = 0,
    MESH_TILE_QUARTER_7 = 7,
    MESH_BLACK_TILE_QUARTER_0 = 8,
    MESH_BLACK_TILE_QUARTER_7 = 15,
    MESH_VOID_QUARTER_0 = 16,
    MESH_VOID_QUARTER_7 = 23,

    MESH_TRIGGER = 24,
    MESH_TRIGGER_CORNER,
    MESH_BARRIER,
    MESH_FLOOR,

    MESH_COUNT,
};

struct Instance {
    Matrix transform {};
    Vector2 wire_texture_offset {};
    Direction wire_texture_rotation = DIR_LEFT;
};

struct MeshState {
    Mesh mesh {};
    std::vector<Instance> instances;
    size_t wire_state_start_idx = 0;
};

struct Render {
    std::array<MeshState, MESH_COUNT> meshes {};
    Material material {};
    Texture wire_state_texture {};
    Image wire_state_image {};
};

struct World {
    IVec size; // first and last cell of each row and column are sentinel cells that are not rendered and not updated
    std::vector<std::vector<Cell>> cells;
    std::vector<Tile> tiles;

    std::string save_file_path = "save.bin";
    std::string replays_file_path = "replays.bin";
    std::array<float, 6> animation_rates = {5., 10., 20., 40., 80., 10000.}; // moves per second, depending on how many buffered inputs there are

    std::vector<std::vector<char>> undo;
    size_t undo_idx = 0; // undo[undo_idx - 1] is the current state
    KeyRepeater undo_repeat;
    KeyRepeater redo_repeat;
    std::array<KeyRepeater, 4> dir_key_repeat;
    Editor editor;

    Render render;

    float absolute_zoom = 0; // like camera.zoom but independent of resolution; (world_coords.y - camera.target.y) * absolute_zoom = screen_coords.y, where whole screen is [-1, 1]
    Camera2D camera {};
    IVec prev_hover {-1, -1};

    MoveState move;
    float animation_rate = 0;
    std::deque<Action> buffered_actions;
    int dragging_tile = -1; // as of the time of input execution, not at the time of input buffering

    std::array<std::vector<Action>, 10> recordings;
    int recording_active = -1;

    void init_start(IVec s) {
        size = s;
        //cells.assign(size.y, std::vector<Cell>(size.x));
        cells.assign(size.y, std::vector<Cell>(size.x, {.floor = FLOOR_PASSABLE}));
    }

    // To init, call either init_start() or load(), then call init_finish().
    void init_finish() {
        editor.init();

        if (absolute_zoom == 0) {
            camera.target = size.to_float() * .5f;
            float aspect = (float)GetRenderWidth() / (float)GetRenderHeight();
            Vector2 ratio = Vector2 {aspect, 1.f} / size.to_float();
            absolute_zoom = std::min(ratio.x, ratio.y) * 2.f;
        }

        push_undo();
    }

    void save(std::vector<char> &out) const;
    void load(const std::vector<char> &in_vec);
    void save_to_file();
    bool load_from_file();
    void save_replays(std::vector<char> &out) const;
    void load_replays(const std::vector<char> &in_vec);
    void save_replays_to_file();
    bool load_replays_from_file();

    void push_undo() {
        undo.resize(undo_idx);
        if (undo.size() > 1000)
            undo.erase(undo.begin(), undo.begin() + undo.size() / 2);

        std::vector<char> out;
        save(out);
        if (undo.empty() || out != undo.back()) {
            undo.push_back(out);
            undo_idx = undo.size();
        }
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

bool has_active_barrier(const World &w, IVec pos, int dir) {
    if (dir >= 2) {
        pos += dir_vec[dir];
        dir = dir_opposite(dir);
    }
    return w.get_cell(pos).barrier_active & (1 << dir);
}
