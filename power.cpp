struct Edge {
    int from = -1;
    int to = -1;
    IVec dir; // rough direction in space, for walking around donut holes
    bool visited = false; // by the donut detection walk

    bool operator<(const Edge &r) const {
        if (from != r.from)
            return from < r.from;
        // Sort by dir angle counterclockwise.
        bool half1 = dir < IVec{};
        bool half2 = r.dir < IVec{};
        if (half1 != half2)
            return half1 < half2;
        return dir.x * r.dir.y < dir.y * r.dir.x;
    }
    bool operator==(const Edge &e) const = default;
};
enum VertexType {
    VERTEX_FLOOR,
    VERTEX_TILE,
};
struct Vertex {
    VertexType type = VERTEX_FLOOR;
    IVec pos;
    int ti = -1;
    IVec virtual_pos; // for edge directions
    bool vertical = false; // if WIRE_BRIDGE, this tells whether this is the horizontal or the vertical wire
    bool whole = false; // WIRE_WHOLE
    bool lit = false;
    bool lit_circle = false;

    bool donut = false; // found a hole in the (connected component of) planar graph of `whole` vertices

    // DFS tree state.
    bool visited = false;
    int wires_above = 0; // number of ancestors with `whole` == false, not counting this Vertex itself

    // Indices in sorted `e`.
    int e_start = 0;
    int e_end = 0;

};
struct Graph {
    std::vector<Vertex> v;
    std::vector<Edge> e;

    void add_edge(int a, int b) {
        e.push_back({a, b});
        e.push_back({b, a});
    }
};

void update_power(World &w) {
    double start_time = GetTime();
    
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
    auto add_vertices = [&](Power &power, VertexType type, IVec pos, int ti, bool moving) {
        if (power.wires == WIRE_NONE)
            return;

        IVec virtual_pos {pos.x * 2, pos.y * 2};
        if (moving)
            virtual_pos += dir_vec[w.move.dir];

        if (power.wires & WIRE_BRIDGE) {
            int horizontal = g.v.size();
            g.v.push_back({.type = type, .pos = pos, .ti = ti, .virtual_pos = virtual_pos, .lit = !!(power.power & (WIRE_LEFT | WIRE_RIGHT))});
            int vertical = g.v.size();
            g.v.push_back({.type = type, .pos = pos, .ti = ti, .virtual_pos = virtual_pos, .vertical = true, .lit = !!(power.power & (WIRE_UP | WIRE_DOWN))});
            power.vertex = {horizontal, vertical, horizontal, vertical, -1};
        } else {
            int v = g.v.size();
            g.v.push_back({.type = type, .pos = pos, .ti = ti, .virtual_pos = virtual_pos, .whole = !!(power.wires & WIRE_WHOLE), .lit = !!power.power, .lit_circle = !!(power.power & WIRE_CIRCLE)});
            power.vertex[4] = v;
            for (int dir = 0; dir < 4; ++dir)
                if (power.wires & (1 << dir))
                    power.vertex[dir] = v;
        }

        power.power = WIRE_NONE;
    };
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            IVec pos {x, y};
            Cell &cell = w.get_cell(pos);

            if (cell.tile != -1) {
                // Extinguish floor circle if covered.
                cell.floor_power.power &= ~WIRE_CIRCLE;

                Tile &tile = w.tiles.at(cell.tile);
                add_vertices(tile.power, VERTEX_TILE, pos, cell.tile, tile.moving);

                tile.power.edge_middle_exposed = tile.power.wires & WIRE_ALL_DIRECTIONS;
                if (tile.power.wires & WIRE_WHOLE)
                    tile.power.edge_rear_end_exposed = tile.power.edge_middle_exposed;
            }

            // (This must be after extinguishing floor circle above.)
            add_vertices(cell.floor_power, VERTEX_FLOOR, pos, -1, false);

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

                // Hack: we consider wire covered by sideways moving tile even if it's moved by DIST_HALF_MINUS_EPSILON. That's geometrically incorrect (wire is only half covered),
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
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            IVec pos {x, y};
            Cell &cell = w.get_cell(pos);

            // Connections between aligned cells/tiles.
            auto handle_aligned_pair = [&](Power *power, Power *power2, int dir, bool moving) {
                // Check contact at just two points along the edge. This is sufficient with current rules.
                int dir2 = dir_opposite(dir);
                bool connected = (power->edge_middle_exposed & (1 << dir)) && (power2->edge_middle_exposed & (1 << dir2));
                connected |= (power->edge_rear_end_exposed & (1 << dir)) && (power2->edge_rear_end_exposed & (1 << dir2));
                if (has_active_barrier(w, pos, dir)) {
                    if (moving && w.move.dir != dir && w.move.dir != dir_opposite(dir) && (power->wires & WIRE_WHOLE) && (power2->wires & WIRE_WHOLE) && !has_active_barrier(w, pos + dir_vec[w.move.dir], dir)) {
                        // The one case where barrier doesn't separate tiles: if tiles are moving, and the barrier doesn't cover the whole edge, and the whole edge is conductive.
                    } else {
                        connected = false;
                    }
                }
                if (connected) {
                    g.add_edge(power->vertex[dir], power2->vertex[dir2]);
                }
            };
            {
                Power *power = &cell.floor_power;
                Power *moving_power = NULL;
                if (cell.tile != -1) {
                    Tile &tile = w.tiles.at(cell.tile);
                    if (tile.moving)
                        moving_power = &tile.power;
                    else
                        power = &tile.power;
                }
                for (int dir = 0; dir < 2; ++dir) { // only look left/up, since this is symmetric and we're adding bidirectional edge
                    Cell &cell2 = w.get_cell(pos + dir_vec[dir]);
                    Power *power2 = &cell2.floor_power;
                    Power *moving_power2 = NULL;
                    if (cell2.tile != -1) {
                        Tile &tile2 = w.tiles.at(cell2.tile);
                        if (tile2.moving)
                            moving_power2 = &tile2.power;
                        else
                            power2 = &tile2.power;
                    }

                    handle_aligned_pair(power, power2, dir, false);

                    // Two tiles moving together.
                    if (moving_power && moving_power2)
                        handle_aligned_pair(moving_power, moving_power2, dir, true);
                }
            }

            // Connections between a moving tile and a stationary cell/tile.
            if (cell.tile != -1 && w.tiles.at(cell.tile).moving) {
                Power &power = w.tiles.at(cell.tile).power;
                Direction forward = w.move.dir;
                Direction backward = dir_opposite(w.move.dir);
                for (int dir = 0; dir < 4; ++dir) {
                    if (power.vertex[dir] == -1)
                        continue;
                    assert(power.edge_middle_exposed & (1 << dir));
                    Direction undir = dir_opposite(dir);

                    const Cell &cell2 = w.get_cell(pos + dir_vec[dir]);
                    const Power *power2 = &cell2.floor_power;
                    if (cell2.tile != -1) {
                        const Tile &tile2 = w.tiles.at(cell2.tile);
                        if (tile2.moving)
                            continue;
                        assert(dir != forward); // not moving into another tile
                        power2 = &tile2.power;
                    }

                    if (dir == forward) { // leading edge of a moving tile
                        int v2 = -1;
                        if (w.move.dist >= DIST_HALF_MINUS_EPSILON || (power2->wires & WIRE_WHOLE) || ((power2->wires & WIRE_CIRCLE) && w.move.dist >= DIST_CIRCLE_RADIUS))
                            v2 = power2->vertex[4];
                        else
                            v2 = power2->vertex[backward];

                        if (v2 != -1)
                            g.add_edge(power.vertex[dir], v2);
                    } else if (dir == backward) { // trailing edge
                        if (cell.floor_power.edge_middle_exposed & (1 << backward))
                            g.add_edge(power.vertex[dir], cell.floor_power.vertex[backward]);
                    } else { // sideways edge
                        if ((power2->edge_middle_exposed & (1 << undir)) && ((power.wires & WIRE_WHOLE) || (power2->wires & WIRE_WHOLE) || w.move.dist < DIST_OVER_WIRE_WIDTH) && !has_active_barrier(w, pos, dir))
                            g.add_edge(power.vertex[dir], power2->vertex[undir]);

                        if (power.wires & WIRE_WHOLE) {
                            const Cell &cell3 = w.get_cell(pos + dir_vec[dir] + dir_vec[forward]);
                            const Power *power3 = &cell3.floor_power;
                            if (cell3.tile != -1) {
                                const Tile &tile3 = w.tiles.at(cell3.tile);
                                if (!tile3.moving)
                                    power3 = &tile3.power;
                            }
                            if ((power3->edge_rear_end_exposed & (1 << undir)) && !has_active_barrier(w, pos + dir_vec[forward], dir))
                                g.add_edge(power.vertex[dir], power3->vertex[undir]);
                        }
                    }
                }
            }
        }
    }

    for (Edge &e: g.e)
        e.dir = g.v.at(e.to).virtual_pos - g.v.at(e.from).virtual_pos;
    std::sort(g.e.begin(), g.e.end());
    for (int start = 0; start < g.e.size(); ) {
        int from = g.e[start].from;
        int end = start + 1;
        while (end < g.e.size() && g.e[end].from == from) {
            assert(g.e[end].to != g.e[end - 1].to); // we avoid duplicated edges by construction; donut search may be sensitive to duplicates
            assert(g.e[end].dir != g.e[end - 1].dir); // we avoid duplicated edges by construction; donut search may be sensitive to duplicates
            ++end;
        }
        g.v.at(from).e_start = start;
        g.v.at(from).e_end = end;
        start = end;
    }

    // Detect donuts, i.e. connected components consisting of only solid black/white cells/tiles (`whole` = true) that have a hole.
    // Walk the perimeter of each face of the planar graph.
    // We walk along tile boundaries, touching solid tiles with right hand, until we return to where we started,
    // tracing some closed path on a plane; if that path goes clockwise, it's the outside perimeter rather than a hole;
    // if it's <= 4 cells/tiles long, it's just 2-4 tiles touching without a gap between them (not true in general, but works with current rules);
    // otherwise it's a donut hole.
    for (int e0 = 0; e0 < g.e.size(); ++e0) {
        if (g.e[e0].visited || !g.v[g.e[e0].from].whole || !g.v[g.e[e0].to].whole)
            continue;
        long area = 0;
        int count = 0;
        IVec pt;
        int e = e0;
        while (true) {
            Edge &edge = g.e[e];
            if (edge.visited)
                break;

            IVec pt2 = pt + edge.dir;
            area += (long)(pt2.y + pt.y) * (long)edge.dir.x;
            pt = pt2;

            edge.visited = true;
            count += 1;
            Vertex &vert = g.v[edge.to];

            // Find the reverse edge.
            int e2 = vert.e_start;
            while (true) {
                assert(e2 < vert.e_end);
                if (g.e[e2].to == edge.from)
                    break;
                ++e2;
            }
            // Take the next eligible edge counterclockwise.
            assert(g.v[g.e[e2].to].whole);
            while (true) {
                ++e2;
                if (e2 == vert.e_end)
                    e2 = vert.e_start;
                if (g.v[g.e[e2].to].whole)
                    break;
            }

            e = e2;
        }
        assert(e == e0);
        assert(pt == IVec{});

        if (area < 0 && count > 4)
            g.v[g.e[e].from].donut = true;
    }

    for (Vertex &v: g.v)
        v.visited = false;
    for (int v0 = 0; v0 < g.v.size(); ++v0) {
        if (g.v[v0].visited)
            continue;
        bool any_lit = false;
        bool has_cycle = false;
        std::vector<int> verts;

        struct StackEntry {
            int v;
            int parent;
            int wires_above;
        };

        std::vector<StackEntry> stack {{v0, -1, 0}};
        while (!stack.empty()) {
            StackEntry en = stack.back();
            stack.pop_back();
            Vertex &vert = g.v[en.v];

            // (Can't do this when pushing to stack because it must happen after we visited previous siblings' subtrees, otherwise it wouldn't be dfs.)
            if (vert.visited) {
                has_cycle |= vert.wires_above < en.wires_above;
                continue;
            }

            verts.push_back(en.v);
            vert.visited = true;
            vert.wires_above = en.wires_above;
            any_lit |= vert.lit;
            has_cycle |= vert.lit_circle;
            has_cycle |= vert.donut;
            int next_wires_above = vert.wires_above + !vert.whole;

            for (int ei = vert.e_start; ei < vert.e_end; ++ei) {
                int v2 = g.e[ei].to;
                if (v2 == en.parent)
                    continue;
                stack.push_back({v2, en.v, next_wires_above});
            }
        }

        if (has_cycle && any_lit) {
            for (int v: verts) {
                Vertex &vert = g.v[v];
                Power &power = vert.type == VERTEX_FLOOR ? w.get_cell(vert.pos).floor_power : w.tiles.at(vert.ti).power;
                if (power.wires & WIRE_BRIDGE)
                    power.power |= vert.vertical ? (WIRE_UP | WIRE_DOWN) : (WIRE_LEFT | WIRE_RIGHT);
                else
                    power.power = power.wires;
            }
        }
    }

    //if (GetRandomValue(0, 200) == 0) std::cout << "update_power took " << GetTime() - start_time << " seconds" << std::endl;
}
