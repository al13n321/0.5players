void draw_power(const Power power, Vector2 pos) {
    const float outer_radius = 20./70;
    const float thickness = 8./70;
    const float crossing_outer_gap = 4./70;
    const float crossing_inner_gap = 10./70;
    const Color colors[2] = {WHITE, BLACK};
    int wires = power.wires;
    if (wires & WIRE_WHOLE) {
        DrawRectangleV(pos, {1.f, 1.f}, colors[!power.power]);
        wires = WIRE_NONE;
    }
    float offset = 0;
    if (wires & (WIRE_CIRCLE | WIRE_BRIDGE)) {
        // Circle or semicircle.
        bool bridge = wires & WIRE_BRIDGE;
        DrawRing(pos + Vector2{.5f, .5f}, outer_radius - thickness, outer_radius, bridge ? 180 : 0, 360, 30, colors[!(power.power & (WIRE_CIRCLE | WIRE_LEFT | WIRE_RIGHT))]);
        offset = outer_radius - thickness/2;

        if (bridge) {
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
            if (cell.barrier_active & WALL_UP) {
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
            if (tile.moving) {
                float t = w.move.elapsed;
                if (w.move.stage == STAGE_SECOND_HALF)
                    t = 1 - t;
                corner_pos += dir_vec[w.move.dir] * t;
            }
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
            IVec pos {x, y};
            const Cell &cell = w.get_cell(pos);
            draw_floor(w.cells, {x, y}, pos.to_float());
        }
    }
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            IVec pos {x, y};
            const Cell &cell = w.get_cell(pos);
            if (cell.tile != -1) {
                const Tile &tile = w.tiles.at(cell.tile);
                int weld = tile.weld;
                int t = w.cells.at(y).at(x + 1).tile;
                if (t != -1 && (w.tiles.at(t).weld & WALL_LEFT)) weld |= WALL_RIGHT;
                t = w.cells.at(y + 1).at(x).tile;
                if (t != -1 && (w.tiles.at(t).weld & WALL_UP)) weld |= WALL_DOWN;

                Vector2 fpos = pos.to_float();
                if (tile.moving) {
                    float t = w.move.elapsed;
                    if (w.move.stage == STAGE_SECOND_HALF)
                        t = 1 - t;
                    fpos += dir_vec[w.move.dir] * t;
                }

                draw_tile(w.tiles.at(cell.tile), weld, fpos);
            }
        }
    }
    draw_selection_indicator(w);
}

void render_ui(World &w) {
    if (w.editor.on) {
        DrawText(TextFormat("%d %d", w.prev_hover.x, w.prev_hover.y), 10, 50, 20, GREEN);
        
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

    if (w.move.manual_advance != -1)
        DrawText("debug mode, press [ to go back to normal, press ] to advance animation by one step", 10, 10, 20, PURPLE);
    if (w.recording_active != -1)
        DrawText(TextFormat("recording macro number %d (%lu moves)", w.recording_active, w.recordings.at(w.recording_active).size()), 10, 100, 20, GREEN);
    if (w.buffered_actions.size() > 10)
        DrawText(TextFormat("actions in queue: %lu", w.buffered_actions.size()), 10, 130, 20, GREEN);
}
