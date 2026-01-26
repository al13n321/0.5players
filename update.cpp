void map_fixup(World &w) {
    for (int y = 0; y < w.size.y; ++y) {
        for (int x = 0; x < w.size.x; ++x) {
            Cell &cell = w.cells.at(y).at(x);

            if (x == 0 || y == 0 || x + 1 == w.size.x || y + 1 == w.size.y) {
                cell = {};
                continue;
            }

            auto fixup_power = [](Power &power) {
                if (power.wires & WIRE_WHOLE)
                    power.wires = WIRE_WHOLE | WIRE_ALL_DIRECTIONS;
                if (power.wires & WIRE_BRIDGE)
                    power.wires = WIRE_BRIDGE | WIRE_ALL_DIRECTIONS;
                power.power &= power.wires;
            };

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
        if (IsKeyDown(KEY_LEFT_ALT) && IsKeyPressed(KEY_R)) {
            w.clear_grid({10, 10});
            undoable = true;
        }
        if (IsKeyDown(KEY_LEFT_ALT)) {
            IVec new_size = w.size;
            if (IsKeyPressed(KEY_LEFT))
                new_size.x -= 1;
            if (IsKeyPressed(KEY_RIGHT))
                new_size.x += 1;
            if (IsKeyPressed(KEY_UP))
                new_size.y -= 1;
            if (IsKeyPressed(KEY_DOWN))
                new_size.y += 1;
            new_size.x = std::max(new_size.x, 3);
            new_size.y = std::max(new_size.y, 3);

            if (new_size != w.size) {
                w.cells.resize(std::max(w.size.y, new_size.y));
                for (int y = 0; y < std::max(w.size.y, new_size.y); ++y) {
                    w.cells.at(y).resize(std::max(w.size.x, new_size.x));
                    for (int x = 0; x < std::max(w.size.x, new_size.x); ++x) {
                        Cell &cell = w.cells.at(y).at(x);
                        bool existed = x > 0 && y > 0 && x + 1 < w.size.x && y + 1 < w.size.y;
                        bool will_exist = x > 0 && y > 0 && x + 1 < new_size.x && y + 1 < new_size.y;
                        if (existed == will_exist)
                            continue;
                        if (will_exist) {
                            cell.floor = FLOOR_PASSABLE;
                        } else {
                            if (cell.tile != -1)
                                w.delete_tile(cell.tile);
                            cell = {};
                        }
                    }
                    w.cells.at(y).resize(new_size.x);
                }
                w.cells.resize(new_size.y);
                w.size = new_size;
                undoable = true;
            }
        }

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
            if (w.editor.held.type == EDITOR_FLOOR || cell.floor != FLOOR_PASSABLE || cell.floor_power.wires != WIRE_NONE) {
                Cell floor = w.editor.held.type == EDITOR_FLOOR ? w.editor.held.floor : Cell {.floor = FLOOR_PASSABLE};
                if (floor.floor == FLOOR_VOID)
                    floor.floor_power.wires = WIRE_NONE;
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
                if (floor.floor == FLOOR_WALL) {
                    if (w.cells.at(hover.y).at(hover.x - 1).floor == FLOOR_WALL)
                        floor.weld |= WALL_LEFT;
                    if (w.cells.at(hover.y - 1).at(hover.x).floor == FLOOR_WALL)
                        floor.weld |= WALL_UP;
                    if (w.cells.at(hover.y).at(hover.x + 1).floor == FLOOR_WALL)
                        w.cells.at(hover.y).at(hover.x + 1).weld |= WALL_LEFT;
                    if (w.cells.at(hover.y + 1).at(hover.x).floor == FLOOR_WALL)
                        w.cells.at(hover.y + 1).at(hover.x).weld |= WALL_UP;
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
            power.anim = {};
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

        if (undoable)
            w.num_static_vertices = -1;

        if (IsKeyPressed(KEY_F5))
            w.save_to_file("save.bin");
        if (IsKeyPressed(KEY_F9)) {
            if (!w.load_from_file("save.bin"))
                w.load_from_file("assets/level.bin");
            undoable = true;
        }
    } else {
        /*
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            w.render.skew += GetMouseDelta() * 1e-3;
        else
            w.render.skew = default_skew;*/
    }

    if (w.undo_repeat.check({KEY_Z}) && w.undo_idx > 1) {
        w.undo_idx -= 1;
        w.load(w.undo.at(w.undo_idx - 1), true);

        // (This is a probably unreliable, I haven't thought through how this interacts with everything else.)
        if (w.recording_active != -1 && !w.recordings.at(w.recording_active).empty())
            w.recordings.at(w.recording_active).pop_back();
    }
    if (w.redo_repeat.check({KEY_X}) && w.undo_idx < w.undo.size() && w.recording_active == -1) {
        w.undo_idx += 1;
        w.load(w.undo.at(w.undo_idx - 1), true);
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
    } else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hover.x != -1 && w.prev_hover.x != -1 && hover != w.prev_hover) {
        if (w.buffered_actions.size() > 0 && w.buffered_actions.back().type == ACTION_DRAG)
            // Don't accumulate lots of inputs if the player flails the mouse around.
            w.buffered_actions.back().pos = hover;
        else
            w.buffered_actions.push_back(Action {.type = ACTION_DRAG, .pos = hover});
    }

    if (w.dir_key_repeat[DIR_LEFT].check({KEY_LEFT, KEY_A}))
        w.buffered_actions.push_back(Action {.type = ACTION_MOVE, .dir = DIR_LEFT});
    if (w.dir_key_repeat[DIR_UP].check({KEY_UP, KEY_W}))
        w.buffered_actions.push_back(Action {.type = ACTION_MOVE, .dir = DIR_UP});
    if (w.dir_key_repeat[DIR_RIGHT].check({KEY_RIGHT, KEY_D}))
        w.buffered_actions.push_back(Action {.type = ACTION_MOVE, .dir = DIR_RIGHT});
    if (w.dir_key_repeat[DIR_DOWN].check({KEY_DOWN, KEY_S}))
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
    //assert(res.size() == seen.size());
    return res;
}

bool select_tile(World &w, IVec pos) {
    Cell &cell = w.cells.at(pos.y).at(pos.x);
    if (cell.tile != -1 && w.tiles.at(cell.tile).selected)
        return true; // keep all selected tiles selected
    for (Tile &tile : w.tiles)
        tile.selected = false;
    if (cell.tile == -1)
        return false;
    std::vector<int> tiles = find_welded_tile_group(w, cell.tile);
    for (int ti: tiles)
        w.tiles.at(ti).selected = true;
    w.push_undo();
    return true;
}

bool move_start(World &w, Direction dir, int required_tile) {
    assert(w.move.stage == STAGE_NONE);
    bool any_moving = false;
    for (int ti0 = 0; ti0 < w.tiles.size(); ++ti0) {
        assert(any_moving || !w.tiles[ti0].moving);
        if (w.tiles[ti0].moving || !w.tiles[ti0].selected)
            continue;

        std::unordered_set<int> seen {ti0}; // for ti0 to move, these tiles must move too
        std::vector<int> stack {ti0};
        bool blocked = false;
        while (!stack.empty() && !blocked) {
            int ti = stack.back();
            stack.pop_back();
            Tile &tile = w.tiles.at(ti);

            for (int dir2 = 0; dir2 < 4; ++dir2) {
                int ti2 = get_welded_neighbor_tile(w, ti, dir2);
                if (ti2 != -1) {
                    if (dir2 == dir_clockwise(dir) || dir2 == dir_counterclockwise(dir))
                        blocked |= has_active_barrier(w, tile.pos + dir_vec[dir], dir2);
                    if (!seen.count(ti2)) {
                        seen.insert(ti2);
                        stack.push_back(ti2);
                    }
                }
            }

            Cell &cell2 = w.get_cell(tile.pos + dir_vec[dir]);
            blocked |= cell2.floor != FLOOR_PASSABLE;
            blocked |= has_active_barrier(w, tile.pos, dir);
            if (cell2.tile != -1 && !seen.count(cell2.tile)) {
                seen.insert(cell2.tile);
                stack.push_back(cell2.tile);
            }
        }

        if (!blocked) {
            any_moving = true;
            for (int ti: seen) {
                w.tiles.at(ti).moving = true;
            }
        }
    }
    if (!any_moving)
        return false;
    if (required_tile != -1 && !w.tiles.at(required_tile).moving) {
        for (Tile &tile: w.tiles)
            tile.moving = false;
        return false;
    }

    w.move.elapsed = 0;
    w.move.stage = STAGE_FIRST_HALF;
    w.move.dist = DIST_EPSILON;
    w.move.dir = dir;

    if (w.move.manual_advance != -1)
        w.move.elapsed = dist_timing.at(w.move.dist);

    return true;
}

std::vector<std::pair<IVec, Direction>> find_barriers_for_trigger(World &w, IVec trigger_pos) {
    std::vector<std::pair<IVec, Direction>> res;
    for (int dir = 0; dir < 4; ++dir) {
        for (int offset = 0; offset < 2; ++offset) {
            Direction cell_dir = std::min(dir_clockwise(dir), dir_counterclockwise(dir));
            IVec pos = trigger_pos + dir_vec[dir];
            if (offset)
                pos -= dir_vec[cell_dir];
            while (true) {
                if (!(w.get_cell(pos).barrier & (1 << cell_dir)))
                    break;
                res.emplace_back(pos, cell_dir);
                pos += dir_vec[dir];
            }
        }
    }
    return res;
}

void update_barriers_and_perform_cuts(World &w) {
    std::vector<std::pair<IVec, Direction>> seen; // just for assert
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            IVec pos {x, y};
            Cell &cell = w.get_cell(pos);
            if (cell.floor != FLOOR_TRIGGER)
                continue;
            bool active = cell.floor_power.power == WIRE_NONE;
            std::vector<std::pair<IVec, Direction>> barriers = find_barriers_for_trigger(w, pos);
            assert(!barriers.empty());
            seen.insert(seen.end(), barriers.begin(), barriers.end());
            for (auto [pos, dir]: barriers) {
                Cell &cell2 = w.get_cell(pos);
                assert(cell2.barrier & (1 << dir));
                bool was_active = !!(cell2.barrier_active & (1 << dir));
                if (was_active && !active) {
                    cell2.barrier_active &= ~(1 << dir);
                } else if (active && !was_active) {
                    cell2.barrier_active |= 1 << dir;

                    // Cut.
                    if (cell2.tile != -1)
                        w.tiles.at(cell2.tile).weld &= ~(1 << dir);
                }
            }
        }
    }

    // Assert that barriers and triggers are matched properly.
    /*
    std::sort(seen.begin(), seen.end());
    for (size_t i = 0; i + 1 < seen.size(); ++i)
        assert(seen[i] != seen[i + 1]);
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            IVec pos {x, y};
            Cell &cell = w.get_cell(pos);
            for (int dir = 0; dir < 2; ++dir) {
                if (cell.barrier & (1 << dir))
                    assert(std::binary_search(seen.begin(), seen.end(), std::make_pair(pos, (Direction)dir)));
            }
        }
    }*/
}

bool move_advance_if_needed(World &w) {
    if (w.move.stage == STAGE_NONE)
        return false;

    float advance_time = w.move.stage == STAGE_FIRST_HALF ? (w.move.dist == DIST_HALF_MINUS_EPSILON ? .5f : dist_timing.at(w.move.dist + 1)) : 1 - (w.move.dist == 0 ? 0.f : dist_timing.at(w.move.dist - 1));
    if (w.move.manual_advance != -1) {
        if (w.move.manual_advance == 0)
            return false;
        w.move.manual_advance -= 1;
        w.move.elapsed = advance_time;
    } else {
        if (w.move.elapsed < advance_time)
            return false;
    }

    if (w.move.stage == STAGE_FIRST_HALF) {
        if (w.move.dist == DIST_HALF_MINUS_EPSILON) {
            w.move.stage = STAGE_SECOND_HALF;

            for (Tile &tile: w.tiles) {
                if (tile.moving) {
                    w.get_cell(tile.pos).tile = -1;
                }
            }
            for (int ti = 0; ti < w.tiles.size(); ++ti) {
                Tile &tile = w.tiles[ti];
                if (tile.moving) {
                    tile.pos += dir_vec[w.move.dir];
                    Cell &cell = w.get_cell(tile.pos);
                    assert(cell.tile == -1);
                    cell.tile = ti;
                }
            }
            w.move.dir = dir_opposite(w.move.dir);
        } else {
            w.move.dist = (MoveDist)(w.move.dist + 1);
        }
    } else {
        if (w.move.dist == 0) {
            w.move.stage = STAGE_NONE;
            for (Tile &t: w.tiles)
                t.moving = false;

            update_power(w);
            update_barriers_and_perform_cuts(w);

            w.push_undo();
        } else {
            w.move.dist = (MoveDist)(w.move.dist - 1);
        }
    }

    return true;
}

void update(World &w) {
    const float zoom_sensitivity = 0.3f;
    float zoom_amount = std::exp(zoom_sensitivity * GetMouseWheelMove());
    if (zoom_amount != 1) {
        // Zoom such that mouse remains stationary in the world.
        float new_zoom = w.absolute_zoom * zoom_amount;
        Vector2 p = GetMousePosition();
        w.camera.target += (p / w.camera.offset - Vector2 {1.f, 1.f}) * (1/w.absolute_zoom - 1/new_zoom);
        w.absolute_zoom = new_zoom;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        w.camera.target -= GetMouseDelta() / w.camera.zoom;
    }
    w.camera.target = Vector2Max(Vector2{0, 0}, Vector2Min(w.size.to_float(), w.camera.target));
    w.camera.offset = {(float)GetRenderWidth()/2, (float)GetRenderHeight()/2};
    w.camera.zoom = w.absolute_zoom * w.camera.offset.y;

    IVec hover = w.hovered_cell();

    size_t effective_buffered_action_count = w.buffered_actions.size();
    if (w.dragging_tile != -1 && w.dragging_tile >= (int)w.tiles.size())
        w.dragging_tile = -1;
    if (w.buffered_actions.size() == 1 && w.buffered_actions[0].type == ACTION_DRAG && w.dragging_tile != -1) {
        // (I'm not sure whether this does anything useful in practice.)
        effective_buffered_action_count = w.tiles.at(w.dragging_tile).pos.manhattan(w.buffered_actions[0].pos);
    }
    float new_animation_rate = w.animation_rates.at(std::min(w.animation_rates.size() - 1, effective_buffered_action_count));
    assert(new_animation_rate > 0);
    if (w.move.stage != STAGE_NONE)
        w.animation_rate = std::max(w.animation_rate, new_animation_rate);
    else
        w.animation_rate = new_animation_rate;
    w.animation_delta = w.animation_rate * GetFrameTime();

    if (IsKeyPressed(KEY_F10))
        w.editor.on ^= 1;
    if (w.editor.on)
        w.editor.layout();
    if (w.move.stage == STAGE_NONE)
        handle_misc_input(w, hover);

    for (int i = 0; i < 10; ++i) {
        if (IsKeyPressed(KEY_ZERO + i)) {
            if (IsKeyDown(KEY_LEFT_CONTROL)) {
                if (w.recording_active == i) {
                    w.recording_active = -1;
                    w.save_replays_to_file();
                } else {
                    w.recording_active = i;
                    if (!IsKeyDown(KEY_LEFT_SHIFT))
                        w.recordings.at(i).clear();
                }
            } else {
                w.buffered_actions.insert(w.buffered_actions.end(), w.recordings.at(i).begin(), w.recordings.at(i).end());
            }
        }
    }

    if (IsKeyDown(KEY_LEFT_ALT)) {
        if (IsKeyPressed(KEY_LEFT_BRACKET))
            w.debug_fudge_parameter -= 1;
        if (IsKeyPressed(KEY_RIGHT_BRACKET))
            w.debug_fudge_parameter += 1;
    }

    if (IsKeyPressed(KEY_LEFT_BRACKET)) {
        if (w.move.manual_advance == -1)
            w.move.manual_advance = 0;
        else
            w.move.manual_advance = -1;
    }
    if (IsKeyPressed(KEY_RIGHT_BRACKET) && w.move.manual_advance != -1)
        w.move.manual_advance += 1;
    
    if (w.editor.on) {
        w.buffered_actions.clear();
        w.dragging_tile = -1;
    } else {
        buffer_inputs(w, hover);
    }

    while (!w.buffered_actions.empty() && w.move.stage == STAGE_NONE) {
        Action action = w.buffered_actions.front();
        bool acted = false;
        bool dequeue = true;
        if (action.type == ACTION_SELECT) {
            acted = select_tile(w, action.pos);
            if (acted) {
                w.dragging_tile = w.get_cell(action.pos).tile;
            } else {
                w.dragging_tile = -1;
                w.buffered_actions.clear(); // subsequent moves would probably move unintended tiles
                dequeue = false;
            }
        } else if (action.type == ACTION_MOVE) {
            acted = move_start(w, action.dir, -1);
        } else if (action.type == ACTION_DRAG && w.dragging_tile != -1) {
            IVec d = action.pos - w.tiles.at(w.dragging_tile).pos;
            std::array<int, 2> dirs {-1, -1};
            if (d.x != 0)
                dirs[0] = d.x < 0 ? DIR_LEFT : DIR_RIGHT;
            if (d.y != 0)
                dirs[1] = d.y < 0 ? DIR_UP : DIR_DOWN;
            if (abs(d.x) < abs(d.y))
                std::swap(dirs[0], dirs[1]);
            for (int dir: dirs) {
                if (dir == -1)
                    continue;

                action.type = ACTION_MOVE;
                action.dir = (Direction)dir;
                acted = move_start(w, action.dir, w.dragging_tile);
                if (acted)
                    break;
            }
            dequeue = !acted;
        }
        if (dequeue)
            w.buffered_actions.pop_front();
        if (w.recording_active != -1 && acted)
            w.recordings.at(w.recording_active).push_back(action);
    }

    if (w.move.stage != STAGE_NONE && w.move.manual_advance == -1)
        w.move.elapsed = std::min(1.f, w.move.elapsed + w.animation_delta);
    bool updated_power = false;
    while (move_advance_if_needed(w)) {
        update_power(w);
        updated_power = true;
    }

    if (!updated_power && !w.editor.on) {
        if (w.move.stage == STAGE_NONE)
            update_barriers_and_perform_cuts(w);

        update_power(w);
    }

    check_consistency(w);

    w.prev_hover = hover;
}
