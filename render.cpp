#include "rlgl.h"

const char *vertex_shader = R"(
#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

in mat4 instanceTransform; // last row contains info about the tile, not part of the transform

uniform mat4 mvp;

// colorMode.w:
// -1 - use vertex colors
// -2 - use tile color for tile type unpacked from instanceTransform
//  0 - use texture
// otherwise use this color
uniform vec4 colorMode;

uniform vec2 skew = vec2(-.35, -.35);
uniform float adjustZ = 0.;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragWireTextureOffset0;
out vec2 fragWireTextureOffset1;
out float fragPowerAnim;
out vec2 fragTexCoord;
out float fragTileInfo;
out float fragPowered;

const mat2 dir_rot[4] = mat2[4](
    mat2(1, 0, 0, 1),
    mat2(0, 1, -1, 0),
    mat2(-1, 0, 0, -1),
    mat2(0, -1, 1, 0));
const vec4 tile_colors[8] = vec4[8](
    vec4(105., 152., 88., 255.) / 255.,
    vec4(206., 183., 111., 255.) / 255.,
    vec4(86., 126., 160., 255.) / 255.,
    vec4(129., 104., 158., 255.) / 255.,
    vec4(176., 121., 49., 255.) / 255.,
    vec4(172., 71., 71., 255.) / 255.,
    vec4(97., 97., 97., 255.) / 255.,
    vec4(47., 47., 47., 255.) / 255.);

vec2 get_wire_texture_offset(float wire_texture_idx) {
    return vec2(mod(wire_texture_idx / 10., 1.), 1. - floor(wire_texture_idx / 10. + 1) / 10.);
}
float is_powered(float wire_texture_idx) {
    return (wire_texture_idx >= 32. && wire_texture_idx != 67.) ? 1. : 0.;
}

void main()
{
    // Unpack information from that we hacked into the one instance info matrix that raylib propagates.
    float wire_texture_idx_0 = mod(instanceTransform[0][3], 128.);
    float wire_texture_idx_1 = floor(instanceTransform[0][3] / 128.);
    float power_anim = instanceTransform[1][3];
    float pre_rotation = instanceTransform[2][3];
    float tile_info = instanceTransform[3][3];
    mat4 unpacked_transform = instanceTransform;
    unpacked_transform[0][3] = 0.;
    unpacked_transform[1][3] = 0.;
    unpacked_transform[2][3] = 0.;
    unpacked_transform[3][3] = 1.;

    vec3 pos = vertexPosition;

    // Blow up the model a tiny bit to hopefully get rid of pixel gaps between tiles
    vec3 center = vec3(.25, .15, -.25);
    pos = (pos - center) * 1.00001 + center;

    // If this instance is a quarter of a tile, rotate the quarter mesh (that spans xy [0, .5]) into the correct quarter of the tile square (xy [-.5, .5]).
    // We can't just include this in instanceTransform matrix because it must apply to wire texture coordinates (fragPosition).
    mat2 pretransform = dir_rot[int(pre_rotation)];
    pos.xz = pretransform * pos.xz;
    vec3 pretransformed_normal = vertexNormal;
    pretransformed_normal.xz = pretransform * pretransformed_normal.xz;

    fragPosition = pos;
    fragNormal = pretransformed_normal;
    if (colorMode.x == -1)
        fragColor = vertexColor;
    else if (colorMode.x == -2)
        fragColor = tile_colors[int(tile_info)];
    else
        fragColor = colorMode;
    fragWireTextureOffset0 = get_wire_texture_offset(wire_texture_idx_0);
    fragWireTextureOffset1 = get_wire_texture_offset(wire_texture_idx_1);
    fragPowerAnim = power_anim;
    fragPowered = mix(is_powered(wire_texture_idx_0), is_powered(wire_texture_idx_1), power_anim);

    fragTexCoord = vertexTexCoord;

    vec4 p = unpacked_transform*vec4(pos, 1.0);
    p.xz += p.y*skew;
    fragPosition.y = p.y;
    p.y += adjustZ;
    gl_Position = mvp*p;
}
)";

const char *fragment_shader = R"(
#version 330

in vec3 fragPosition; // [-.5, .5]
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragWireTextureOffset0;
in vec2 fragWireTextureOffset1;
in float fragPowerAnim;
in vec2 fragTexCoord;
in float fragTileInfo;
in float fragPowered;

uniform sampler2D texture0;
uniform sampler2D texture1;

uniform vec3 lightDirection;
uniform float whichMesh;

out vec4 finalColor;

const float MESH_FLOOR = 27.;
const float MESH_BLACK_TILE_QUARTER_0 = 16.;
const float MESH_BARRIER = 26.;

void main()
{
    vec4 color = vec4(fragPowered, fragPowered, fragPowered, 1.);
    vec4 texel = texture(texture1, fragTexCoord);
    color.xyz = mix(color.xyz, texel.xyz, texel.w);
    color.xyz = mix(color.xyz, fragColor.xyz, fragColor.w);
    if (whichMesh == MESH_FLOOR) {
        const float thickness = .14 * .5;
        vec2 t = abs(abs(fragPosition.xz) - .5);
        float f = step(thickness*.5, min(t.x, t.y));
        color = vec4(.256, .256, .256, 1.) * f + vec4(.288, .288, .288, 1.) * (1. - f);
    }
    float light = clamp(-dot(lightDirection, normalize(fragNormal)), 0., 1.);
    vec2 wire_tex_coord_0 = fragWireTextureOffset0 + clamp(fragPosition.xz + .5, 1./200, 1.-1./200) / 10.;
    vec2 wire_tex_coord_1 = fragWireTextureOffset1 + clamp(fragPosition.xz + .5, 1./200, 1.-1./200) / 10.;
    vec4 wire_texel = mix(texture(texture0, wire_tex_coord_0), texture(texture0, wire_tex_coord_1), fragPowerAnim);
    if (whichMesh >= MESH_BLACK_TILE_QUARTER_0 && whichMesh <= MESH_BLACK_TILE_QUARTER_0 + 7) {
        finalColor = color;
    } else if (whichMesh == MESH_BARRIER) {
        float h = mix(mix(.015, .206, clamp(fragPosition.y / .373, 0., 1.)), .665, clamp((fragPosition.y - .373) / (1. - .373), 0., 1.));
        finalColor = vec4(mix(color.xyz * light, vec3(h, h, h), .5), 1.);
    } else {
        finalColor = vec4(mix(color.xyz * light, wire_texel.xyz, wire_texel.w), 1.);
    }
}
)";

Mesh alloc_mesh(int vertex_count, int triangle_count) {
    return Mesh {
        .vertexCount = vertex_count, .triangleCount = triangle_count,
        .vertices = (float*)RL_CALLOC(vertex_count, sizeof(Vector3)),
        .texcoords = (float*)RL_CALLOC(vertex_count, sizeof(Vector2)),
        .normals = (float*)RL_CALLOC(vertex_count, sizeof(Vector3)),
        .colors = (unsigned char*)RL_CALLOC(vertex_count, sizeof(Color)),
        .indices = (unsigned short*)RL_CALLOC(triangle_count, 3*sizeof(short))};
}
void set_mesh_verts(Mesh &mesh, int idx, std::initializer_list<Vector3> il) {
    for (Vector3 v: il) {
        assert(idx < mesh.vertexCount);
        memcpy(mesh.vertices + idx*3, &v, sizeof(Vector3));
        idx += 1;
    }
}
void set_mesh_normals(Mesh &mesh, int idx, std::initializer_list<Vector3> il) {
    for (Vector3 v: il) {
        assert(idx < mesh.vertexCount);
        memcpy(mesh.normals + idx*3, &v, sizeof(Vector3));
        idx += 1;
    }
}
void set_mesh_indices(Mesh &mesh, int idx, std::initializer_list<unsigned short> il) {
    for (unsigned short v: il) {
        assert(idx < mesh.triangleCount*3);
        mesh.indices[idx] = v;
        idx += 1;
    }
}

void load_assets(World &w) {
    w.render.material.maps[0].texture = LoadTexture("assets/wires.png");
    w.render.material.maps[1].texture = LoadTexture("assets/walls.png");
    GenTextureMipmaps(&w.render.material.maps[0].texture);
    GenTextureMipmaps(&w.render.material.maps[1].texture);

    Model m = LoadModel("assets/tiles.glb");
    assert(m.meshCount == 27);
    for (int i = 0; i < 27; ++i) {
        w.render.meshes[i].mesh = m.meshes[i];
    }
}

void render_init(World &w) {
    w.render.material = LoadMaterialDefault();
    w.render.material.shader = LoadShaderFromMemory(vertex_shader, fragment_shader);
    w.render.material.shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(w.render.material.shader, "instanceTransform");

    {
        Mesh m = alloc_mesh(4, 2);
        set_mesh_verts(m, 0, {{0, 0, -.5}, {0, 0, 0}, {.5, 0, 0}, {.5, 0, -.5}});
        for (int i = 0; i < m.vertexCount * 3; ++i) {
            float &v = m.vertices[i];
            if (v < 0)
                v -= 1e-5;
            if (v > 0)
                v += 1e-5;
        }
        set_mesh_normals(m, 0, {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}});
        set_mesh_indices(m, 0, {0,1,2,  2,3,0});
        UploadMesh(&m, false);
        w.render.meshes[MESH_FLOOR].mesh = m;
    }

    for (int i = MESH_TILE_QUARTER_0; i <= MESH_TILE_QUARTER_7; ++i)
        w.render.meshes[i].color_mode = {-2, -2, -2, -2};
    for (int i = MESH_VOID_QUARTER_0; i <= MESH_TRIGGER_CORNER; ++i)
        w.render.meshes[i].color_mode = {0, 0, 0, 0};
    w.render.meshes[MESH_BARRIER].color_mode = {1., 1., 1., 1.};

    load_assets(w);
}

void draw_selection_indicator(const World &w) {
    const float thickness = 0.07;
    const float length = 0.2;
    const float gap = 0.04 + 0.02 * sin(GetTime() * 3.);
    const Color col {192, 192, 192, 255};

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
            corner_pos += Vector2{-.05, -.05};
            for (Vector2 &p : points)
                p += corner_pos;
            //DrawTriangleFan(&points[0], 6, col);

            for (int i = 1; i + 1 < 6; ++i) {
                DrawTriangle3D({points[0].x, .8, points[0].y}, {points[i].x, .8, points[i].y}, {points[i + 1].x, .8, points[i + 1].y}, col);
            }
        }
    }
}

Camera3D make_camera_3d(Camera2D camera, float absolute_zoom) {
    Vector2 target = camera.target;
    target += GetScreenToWorld2D({GetRenderWidth() * .5f, GetRenderHeight() * .5f}, camera) - GetScreenToWorld2D(camera.offset, camera);
    return {.position = {target.x, 100, target.y}, .target = {target.x, 90, target.y}, .up = {0, 0, -1}, .fovy = 2.f / absolute_zoom, .projection = CAMERA_ORTHOGRAPHIC};
}

int get_wire_texture_idx(const World &w, const Power &power, bool is_void) {
    if (power.wires & WIRE_BRIDGE)
        return 63 + ((power.power & WIRE_LEFT) ? 1 : 0) + ((power.power & WIRE_UP) ? 2 : 0);
    if ((power.wires & WIRE_WHOLE) && !is_void)
        return 67 + ((power.power & WIRE_UP) ? 1 : 0);

    int idx = (power.wires & (WIRE_UP | WIRE_DOWN)) | ((power.wires & 1) << 2) | ((power.wires & 4) >> 2);
    if (power.wires & WIRE_CIRCLE)
        idx += 16;
    if (power.power)
        idx += 31;
    return idx;
}

void update_power_anim(const World &w, Power &power, bool is_void) {
    int idx = get_wire_texture_idx(w, power, is_void);
    if (idx != power.anim.wire_texture_idx[1]) {
        if (idx == power.anim.wire_texture_idx[0]) {
            power.anim.wire_texture_idx[0] = power.anim.wire_texture_idx[1];
            power.anim.anim = 1 - power.anim.anim;
        } else if (power.anim.wire_texture_idx[1] == -1) {
            power.anim.wire_texture_idx[0] = power.anim.wire_texture_idx[1] = idx;
            power.anim.anim = 1;
        } else {
            power.anim.wire_texture_idx[0] = power.anim.wire_texture_idx[1];
            power.anim.anim = 0;
        }
        power.anim.wire_texture_idx[1] = idx;
    }
    power.anim.anim = Clamp(power.anim.anim + w.animation_delta * w.power_animation_rate, 0, 1);
}

int get_weld_mask(const World &w, IVec pos, int dir, bool is_tile, bool is_editor) {
    if (is_editor)
        return 0;
    int d0 = dir_opposite(dir);
    int d1 = (dir + 1) & 3;
    int weld_mask = has_weld(w, pos, d0, is_tile) | ((int)has_weld(w, pos, d1, is_tile) << 1);
    if (((weld_mask & 1) && has_weld(w, pos + dir_vec[d0], d1, is_tile)) || ((weld_mask & 2) && has_weld(w, pos + dir_vec[d1], d0, is_tile)))
        weld_mask |= 4;
    return weld_mask;
}

void draw_under_floor(World &w, IVec pos, const Cell &cell, const Matrix &transform, PowerAnim power, bool is_editor) {
    for (int dir = 0; dir < 4; ++dir) {
        Instance floor_instance {.transform = transform, .power = power, .pre_rotation = (Direction)dir};
        if (!is_editor && cell.floor == FLOOR_WALL) {
            // Cover up the corner case (pun intended) where a wall's rounded corner stands in the middle of conductive floor and exposes a tiny patch of non-conductive floor under it.
            // This is the only reason we split the FLOOR_PASSABLE floor tile into quarters.
            bool ok = true;
            auto is_conductive_floor = [&](const Cell &cell2) {
                return cell2.floor == FLOOR_PASSABLE && (cell2.floor_power.wires & WIRE_WHOLE);
            };
            PowerAnim neighbor_power;
            int conductive_neighbors = 0;
            std::array<int, 2> dirs {dir_opposite(dir), dir_clockwise(dir)};
            for (int d: dirs) {
                ok &= !has_weld(w, pos, d, false);
                const Cell &cell2 = w.get_cell(pos + dir_vec[d]);
                if (is_conductive_floor(cell2)) {
                    neighbor_power = cell2.floor_power.anim;
                    conductive_neighbors += 1;
                }
            }
            if (ok && conductive_neighbors > 0 && (conductive_neighbors == 1 || is_conductive_floor(w.get_cell(pos + dir_vec[dirs[0]] + dir_vec[dirs[1]])))) {
                floor_instance.power = neighbor_power;
            }
        }
        w.render.meshes.at(MESH_FLOOR).instances.push_back(floor_instance);
    }
}

int get_void_mask(const World &w, IVec pos, bool is_editor) {
    int void_mask = 0;
    if (!is_editor) {
        if (w.cells.at(pos.y).at(pos.x - 1).floor == FLOOR_VOID)
            void_mask |= 1 << DIR_LEFT;
        if (w.cells.at(pos.y).at(pos.x + 1).floor == FLOOR_VOID)
            void_mask |= 1 << DIR_RIGHT;
        if (w.cells.at(pos.y - 1).at(pos.x).floor == FLOOR_VOID)
            void_mask |= 1 << DIR_UP;
        if (w.cells.at(pos.y + 1).at(pos.x).floor == FLOOR_VOID)
            void_mask |= 1 << DIR_DOWN;
    }
    return void_mask;
}

void draw_floor(World &w, IVec pos, const Cell &cell, const Matrix &transform, PowerAnim power, bool is_editor) {
    if (cell.floor == FLOOR_WALL) {
        for (int dir = 0; dir < 4; ++dir) {
            MeshState &mesh = w.render.meshes.at(MESH_TILE_QUARTER_0 + get_weld_mask(w, pos, dir, false, is_editor));
            mesh.instances.push_back({.transform = transform, .power = power, .pre_rotation = (Direction)dir, .tile_info = TILE_BLACK});
        }
    } else if (cell.floor == FLOOR_TRIGGER) {
        w.render.meshes.at(MESH_TRIGGER).instances.push_back({.transform = transform, .power = power, .pre_rotation = DIR_UP});
        if (!is_editor) {
            auto add_corner = [&](Direction dir) {
                w.render.meshes.at(MESH_TRIGGER_CORNER).instances.push_back({.transform = transform, .pre_rotation = dir});
            };
            if ((w.cells.at(pos.y - 1).at(pos.x).barrier & WALL_LEFT) || (w.cells.at(pos.y).at(pos.x - 1).barrier & WALL_UP))
                add_corner(DIR_DOWN);
            if ((w.cells.at(pos.y - 1).at(pos.x + 1).barrier & WALL_LEFT) || (w.cells.at(pos.y).at(pos.x + 1).barrier & WALL_UP))
                add_corner(DIR_LEFT);
            if ((w.cells.at(pos.y + 1).at(pos.x).barrier & WALL_LEFT) || (w.cells.at(pos.y + 1).at(pos.x - 1).barrier & WALL_UP))
                add_corner(DIR_RIGHT);
            if ((w.cells.at(pos.y + 1).at(pos.x + 1).barrier & WALL_LEFT) || (w.cells.at(pos.y + 1).at(pos.x + 1).barrier & WALL_UP))
                add_corner(DIR_UP);
        }
    } else if (cell.floor == FLOOR_VOID) {
        int void_mask = get_void_mask(w, pos, is_editor);
        for (int dir = 0; dir < 4; ++dir) {
            int d0 = dir_opposite(dir);
            int d1 = (dir + 1) & 3;
            int weld_mask = 0;
            if (!is_editor)
                weld_mask = ((void_mask >> dir_opposite(dir)) & 1) | (((void_mask >> d1) & 1) << 1) | ((w.get_cell(pos + dir_vec[d0] + dir_vec[d1]).floor == FLOOR_VOID) << 2);
            MeshState &mesh = w.render.meshes.at(MESH_VOID_QUARTER_0 + weld_mask);
            mesh.instances.push_back({.transform = transform, .power = power, .pre_rotation = (Direction)dir});
        }
    }
}

void draw_tile(World &w, IVec pos, const Tile &tile, Matrix transform, bool is_editor) {
    if (!is_editor && tile.moving) {
        float t = w.move.elapsed;
        if (w.move.stage == STAGE_SECOND_HALF)
            t = 1 - t;
        Vector2 v = pos.to_float() + Vector2(.5f, .5f) + dir_vec[w.move.dir].to_float() * t;
        transform = MatrixTranslate(v.x, 0, v.y);
    }
    for (int dir = 0; dir < 4; ++dir) {
        MeshState &mesh = w.render.meshes.at((tile.type == TILE_BLACK ? MESH_BLACK_TILE_QUARTER_0 : MESH_TILE_QUARTER_0) + get_weld_mask(w, pos, dir, true, is_editor));
        mesh.instances.push_back({.transform = transform, .power = tile.power.anim, .pre_rotation = (Direction)dir, .tile_info = (float)tile.type});
    }
}

void render_instances(World &w, float adjust_z = 0) {
    BeginShaderMode(w.render.material.shader);

    Vector3 light_direction{-.25, -1, -.45};
    SetShaderValue(w.render.material.shader, GetShaderLocation(w.render.material.shader, "lightDirection"), &light_direction, SHADER_UNIFORM_VEC3);
    SetShaderValue(w.render.material.shader, GetShaderLocation(w.render.material.shader, "skew"), &w.render.skew, SHADER_UNIFORM_VEC2);
    SetShaderValue(w.render.material.shader, GetShaderLocation(w.render.material.shader, "adjustZ"), &adjust_z, SHADER_UNIFORM_FLOAT);

    std::vector<Matrix> transforms;
    for (int mesh_idx = 0; mesh_idx < w.render.meshes.size(); ++mesh_idx) {
        MeshState &mesh = w.render.meshes[mesh_idx];
        if (mesh.instances.empty())
            continue;
        float mesh_idx_float = (float)mesh_idx;
        SetShaderValue(w.render.material.shader, GetShaderLocation(w.render.material.shader, "colorMode"), &mesh.color_mode, SHADER_UNIFORM_VEC4);
        SetShaderValue(w.render.material.shader, GetShaderLocation(w.render.material.shader, "whichMesh"), &mesh_idx_float, SHADER_UNIFORM_FLOAT);
        transforms.resize(mesh.instances.size());
        for (size_t i = 0; i < mesh.instances.size(); ++i) {
            const Instance &ins = mesh.instances[i];
            transforms[i] = ins.transform;
            transforms[i].m3 = (float)(ins.power.wire_texture_idx[0] + ins.power.wire_texture_idx[1] * 128);
            transforms[i].m7 = ins.power.anim;
            transforms[i].m11 = (float)ins.pre_rotation;
            transforms[i].m15 = ins.tile_info;
        }
        DrawMeshInstanced(mesh.mesh, w.render.material, &transforms[0], (int)mesh.instances.size());
    }

    EndShaderMode();

    for (MeshState &mesh: w.render.meshes) {
        mesh.instances.clear();
    }
}

void render_ui(World &w) {
    if (w.editor.on) {
        for (MeshState &mesh: w.render.meshes) {
            mesh.instances.clear();
        }

        DrawText(TextFormat("%d %d", w.prev_hover.x, w.prev_hover.y), 10, 50, 20, GREEN);

        Camera3D camera3d = make_camera_3d(w.editor.palette_camera, w.editor.palette_camera.zoom / GetRenderHeight() * 2);
        BeginMode3D(camera3d);

        DrawPlane({-w.editor.palette_width/2, 10., 0}, {w.editor.palette_width, 1e5}, DARKBLUE);

        for (const EditorCell &cell : w.editor.palette) {
            if (cell.equivalent(w.editor.held)) {
                const float s = 0.2;
                DrawPlane({cell.rect.x + .5f, 20., cell.rect.y + .5f}, {1.f + s, 1.f + s}, WHITE);
            }
            Matrix transform = MatrixTranslate(cell.rect.x + .5f, 0, cell.rect.y + .5f);
            IVec pos {-10, -10};
            PowerAnim power {.wire_texture_idx = {0, 0}};
            if (cell.type == EDITOR_FLOOR) {
                if (cell.floor.floor == FLOOR_PASSABLE) {
                    draw_under_floor(w, pos, cell.floor, transform, power, true);
                } else {
                    draw_floor(w, pos, cell.floor, transform, power, true);
                }
            } else if (cell.type == EDITOR_TILE) {
                draw_tile(w, pos, cell.tile, transform, true);
            }
        }

        render_instances(w, 30);

        EndMode3D();
    }

    if (w.move.manual_advance != -1)
        DrawText("debug mode, press [ to go back to normal, press ] to advance animation by one step", 10, 10, 20, PURPLE);
    if (w.recording_active != -1)
        DrawText(TextFormat("recording macro number %d (%lu moves)", w.recording_active, w.recordings.at(w.recording_active).size()), 10, 100, 20, GREEN);
    if (w.buffered_actions.size() > 10)
        DrawText(TextFormat("actions in queue: %lu", w.buffered_actions.size()), 10, 130, 20, GREEN);
}

void render(World &w) {
    for (MeshState &mesh: w.render.meshes) {
        mesh.instances.clear();
    }

    // Animate.
    for (int y = 1; y + 1 < w.size.y; ++y) {
        for (int x = 1; x + 1 < w.size.x; ++x) {
            IVec pos {x, y};
            Cell &cell = w.get_cell(pos);
            if (cell.floor == FLOOR_VOID) {
                // Hacks.
                int void_mask = get_void_mask(w, pos, false);
                Power fake_power = cell.floor_power;
                fake_power.wires = (uint8_t)((cell.floor_power.wires & ~void_mask) | WIRE_CIRCLE);
                update_power_anim(w, fake_power, true);
                cell.floor_power.anim = fake_power.anim;
            } else {
                update_power_anim(w, cell.floor_power, false);
            }
            if (cell.tile != -1)
                update_power_anim(w, w.tiles.at(cell.tile).power, false);
        }
    }

    Color clear_color;
    if (w.editor.on) {
        clear_color = {100, 100, 200, 255};
    } else {
        const PowerAnim &p = w.cells.at(1).at(1).floor_power.anim;
        float powered = Lerp(p.wire_texture_idx[0] >= 32, p.wire_texture_idx[1] >= 32, p.anim);
        clear_color = ColorLerp(BLACK, WHITE, powered);
    }
    ClearBackground(clear_color);

    Camera3D camera3d = make_camera_3d(w.camera, w.absolute_zoom);
    BeginMode3D(camera3d);

    for (int y = 0; y < w.size.y; ++y) {
        for (int x = 0; x < w.size.x; ++x) {
            bool is_edge = x == 0 || y == 0 || x + 1 == w.size.x || y + 1 == w.size.y;

            IVec pos {x, y};
            Cell &cell = w.get_cell(pos);
            Matrix transform = MatrixTranslate((float)pos.x + .5f, 0.f, (float)pos.y + .5f);

            if (is_edge) {
                MeshState &mesh = w.render.meshes.at(MESH_VOID_QUARTER_7);
                IVec neighbor {std::max(1, std::min(w.size.x - 2, x)), std::max(1, std::min(w.size.y - 2, y))};
                PowerAnim power = w.get_cell(neighbor).floor_power.anim;
                for (int dir = 0; dir < 4; ++dir) {
                    mesh.instances.push_back({.transform = transform, .power = power, .pre_rotation = (Direction)dir});
                }
                continue;
            }

            PowerAnim power = cell.floor_power.anim;

            draw_under_floor(w, pos, cell, transform, power, false);
            draw_floor(w, pos, cell, transform, power, false);

            if (cell.tile != -1) {
                Tile &tile = w.tiles.at(cell.tile);
                draw_tile(w, pos, tile, transform, false);
            }

            if (cell.barrier) {
                for (int dir: {DIR_LEFT, DIR_UP}) {
                    if (cell.barrier & (1 << dir)) {
                        float target = (cell.barrier_active & (1 << dir)) ? 0 : 1;
                        float &anim = cell.barrier_anim[dir];
                        if (anim < 0)
                            anim = target;
                        else if (target != anim)
                            anim += (target > anim ? +1 : -1) * w.animation_delta * w.barrier_animation_rate;
                        anim = Clamp(anim, 0, 1);
                        float h = anim * .4;
                        w.render.meshes.at(MESH_BARRIER).instances.push_back({.transform = MatrixMultiply(transform, MatrixTranslate(0, h, 0)), .pre_rotation = dir_opposite(dir)});
                    }
                }
            }
        }
    }
    draw_selection_indicator(w);

    render_instances(w);

    EndMode3D();

    render_ui(w);
}

// todo but realistically not: optimize graph traversal more, wire antialiasing of some kind, deal with srgb correctly, higher-poly rounded corners on tiles, sounds, better editor controls, show controls, menus and options
