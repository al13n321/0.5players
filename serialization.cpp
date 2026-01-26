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

void World::save(std::vector<char> &out) const {
    //double start_time = GetTime();

    size_t version = 5;
    save_pod(&version, out);
    save_pod(&size, out);
    save_pod(&camera.target, out);
    save_pod(&absolute_zoom, out);
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
            out.resize(pos + cell_bytes + power_bytes + 1);
            memcpy(&out[pos], &cells[y][x], cell_bytes);
            memcpy(&out[pos + cell_bytes], &cells[y][x].floor_power, power_bytes);
            memcpy(&out[pos + cell_bytes + power_bytes], &cells[y][x].barrier_active, 1);
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
void World::load(const std::vector<char> &in_vec, bool ignore_camera) {
    //double start_time = GetTime();

    const char *in = &in_vec[0];
    const char *end = in + in_vec.size();
    size_t version = 0;
    load_pod(&version, &in, end);

    if (version < 3 || version > 5)
        std::abort();

    load_pod(&size, &in, end);
    if (size.x < 2 || size.y < 2 || 1000000 / size.x / size.y == 0)
        std::abort();
    if (version >= 5) {
        Vector2 t;
        float z;
        load_pod(&t, &in, end);
        load_pod(&z, &in, end);
        if (!ignore_camera) {
            camera.target = t;
            absolute_zoom = z;
        }
    }
    cells.resize(size.y);
    for (int y = 0; y < size.y; ++y) {
        cells[y].assign(size.x, {});
    }
    size_t t;
    load_pod(&t, &in, end);
    if (t > 1000000)
        std::abort();
    tiles.assign(t, {});

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

            if (version >= 4) {
                if (end - in < 1)
                    std::abort();
                memcpy(&cells[y][x].barrier_active, in, 1);
                in += 1;
            }
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

void write_whole_file(std::string path, const std::vector<char> &data) {
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), data.size());
}

bool read_whole_file(std::string path, std::vector<char> &data) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    size_t size = file.tellg();
    if (size > 1000000000)
        std::abort();
    file.seekg(0);
    data.resize(size);
    file.read(data.data(), size);
    return true;
}
    
void World::save_to_file() {
    std::vector<char> data;
    save(data);
    write_whole_file(save_file_path, data);
}

bool World::load_from_file() {
    std::vector<char> data;
    if (!read_whole_file(save_file_path, data))
        return false;
    load(data, false);
    return true;
}

void World::save_replays(std::vector<char> &out) const {
    size_t version = 1;
    save_pod(&version, out);
    size_t t = sizeof(Action);
    save_pod(&t, out);
    t = recordings.size();
    save_pod(&t, out);
    for (const std::vector<Action> &rec: recordings) {
        t = rec.size();
        save_pod(&t, out);
        for (const Action &a: rec)
            save_pod(&a, out);
    }
}

void World::load_replays(const std::vector<char> &in_vec) {
    const char *in = &in_vec[0];
    const char *end = in + in_vec.size();
    size_t version = 0;
    load_pod(&version, &in, end);
    size_t action_size = 0;
    load_pod(&action_size, &in, end);
    action_size = std::min(action_size, sizeof(Action));
    if (action_size == 0)
        std::abort();
    size_t num_recs = 0;
    load_pod(&num_recs, &in, end);
    num_recs = std::min(num_recs, recordings.size());
    for (size_t i = 0; i < num_recs; ++i) {
        std::vector<Action> &rec = recordings.at(i);
        size_t t;
        load_pod(&t, &in, end);
        for (size_t i = 0; i < t; ++i) {
            if (end - in < action_size)
                std::abort();
            Action a;
            memcpy(&a, in, action_size);
            rec.push_back(a);
            in += action_size;
        }
    }
}

void World::save_replays_to_file() {
    std::vector<char> data;
    save_replays(data);
    write_whole_file(replays_file_path, data);
}

bool World::load_replays_from_file() {
    std::vector<char> data;
    if (!read_whole_file(replays_file_path, data))
        return false;
    load_replays(data);
    return true;
}
