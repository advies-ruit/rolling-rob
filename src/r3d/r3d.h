#ifndef ROLLING_ROB_R3D_H
#define ROLLING_ROB_R3D_H

#include <stdbool.h>
#include <stdint.h>

/*
 * A small 3D rasterizer for a processor with no GPU (say, four small
 * ARM cores), in the spirit of the N64: triangles with per-vertex
 * colour and light (Gouraud), textures (power-of-two sizes, mip levels picked
 * per triangle, perspective-correct every 16 pixels, nearest texel, multiplied
 * by the vertex colour and light), a depth buffer, fog into a sky gradient.
 *
 * A frame: r3_begin() with the camera, r3_draw() per object (meshes stay
 * the caller's; only the pointer and the matrix are kept), then
 * r3_render(), which transforms and lights the vertices, clips at the near
 * plane, culls back faces, sorts the triangles into bands of rows and
 * rasterizes the bands on every core at once. Each finished band is scaled
 * up by an integer factor and handed to a sink -- the same band sink the
 * game's own renderer feeds (the simulator copies it into a canvas).
 *
 * Right-handed, y up, the camera looks down -z; triangles wind counter-
 * clockwise seen from the front.
 */

typedef struct { float x, y, z; } r3_vec;
typedef struct { float m[16]; } r3_mat; /* column-major: m[col * 4 + row] */

typedef struct {
    float px, py, pz;
    float nx, ny, nz;
    uint32_t rgb; /* 0xRRGGBB; on a textured mesh it tints the texture */
    float u, v;   /* texture coordinates, in repeats of the texture */
} r3_vertex;

/* A texture: 2^wl x 2^hl XRGB pixels and its smaller copies (each half the
 * size, averaged), down to 4 pixels a side. */
#define R3_MIPS 8
typedef struct {
    int levels;
    struct { uint32_t * px; int wl, hl; } lv[R3_MIPS];
} r3_texture;
/* Copies px (2^wl x 2^hl) and makes the levels. */
bool r3_texture_init(r3_texture * t, const uint32_t * px, int wl, int hl);
void r3_texture_free(r3_texture * t);

typedef struct {
    r3_vertex * v;
    int nv, cap_v;
    uint32_t * idx;
    int ni, cap_i;
    r3_vec center; /* bounding sphere, for culling (r3_mesh_bounds) */
    float radius;
    const r3_texture * tex; /* NULL: plain colours */
    /* the vertices again for the transform: blocks of four, field by field
     * (x x x x, y y y y, ..., one stream to read), so NEON takes four at a time */
    float * soa;
    int soa_cap;
} r3_mesh;

/* ---- vectors and matrices ---- */
static inline r3_vec r3v(float x, float y, float z) { r3_vec v = { x, y, z }; return v; }
r3_vec r3_add(r3_vec a, r3_vec b);
r3_vec r3_sub(r3_vec a, r3_vec b);
r3_vec r3_scale(r3_vec a, float s);
float r3_dot(r3_vec a, r3_vec b);
r3_vec r3_cross(r3_vec a, r3_vec b);
float r3_len(r3_vec a);
r3_vec r3_norm(r3_vec a);
r3_mat r3_identity(void);
r3_mat r3_mul(const r3_mat * a, const r3_mat * b);
r3_mat r3_translate(r3_vec t);
r3_mat r3_rotate_x(float rad);
r3_mat r3_rotate_y(float rad);
r3_mat r3_rotate_axis(r3_vec axis, float rad);
r3_mat r3_scale_m(float s);
r3_mat r3_perspective(float fov_y_rad, float aspect, float near_, float far_);
r3_mat r3_look_at(r3_vec eye, r3_vec target, r3_vec up);
r3_vec r3_transform(const r3_mat * m, r3_vec p); /* a point (w = 1) */

/* ---- meshes ---- */
void r3_mesh_init(r3_mesh * m);
void r3_mesh_free(r3_mesh * m);
void r3_mesh_clear(r3_mesh * m);
int r3_mesh_vertex(r3_mesh * m, r3_vec p, r3_vec n, uint32_t rgb);
int r3_mesh_vertex_uv(r3_mesh * m, r3_vec p, r3_vec n, uint32_t rgb, float u, float v);
void r3_mesh_tri(r3_mesh * m, int a, int b, int c);
/* faces with their own normals (flat), each face its colour; texture
 * coordinates in metres (one repeat per unit) */
void r3_mesh_box(r3_mesh * m, r3_vec lo, r3_vec hi, uint32_t top, uint32_t side);
/* smooth sides; caps flat; along +y from base */
void r3_mesh_cylinder(r3_mesh * m, r3_vec base, float r, float h, int seg, uint32_t side, uint32_t cap);
void r3_mesh_cone(r3_mesh * m, r3_vec base, float r, float h, int seg, uint32_t rgb);
/* smooth; subdiv 0 = icosahedron (20 triangles), each step x4 */
void r3_mesh_sphere(r3_mesh * m, r3_vec c, float r, int subdiv, uint32_t rgb);
/* After building or changing a mesh: its bounding sphere and the packed copy
 * the transform reads. Draw a mesh only after this. */
void r3_mesh_bounds(r3_mesh * m);

/* ---- a frame ---- */
void r3_begin(const r3_mat * view, const r3_mat * proj, r3_vec eye);
void r3_set_light(r3_vec to_light, float ambient);
void r3_set_fog(uint32_t rgb, float start, float end);
void r3_set_sky(uint32_t top, uint32_t horizon);
void r3_draw(const r3_mesh * m, const r3_mat * model);

typedef void (*r3_sink_t)(uint32_t * band, int stride_px, int y0, int rows, void * user);
/* out_w x out_h on screen, drawn at (out / scale), bands handed over as they
 * finish (any thread, any order). */
void r3_render(int out_w, int out_h, int scale, r3_sink_t sink, void * user);

/* Bands bottom-up, for a picture turned 180 degrees. */
void r3_set_band_order(bool bottom_up);
/* 0: one per core (at most 4). Before the first frame. */
void r3_set_threads(int n);
/* The renderer's threads, for other work between frames (the fractals):
 * fn(job, thread) for jobs 0..jobs-1, spread over all threads, this one
 * included; returns when every job is done. Not while r3_render runs. */
void r3_parallel(int jobs, void (*fn)(int job, int tid));
int r3_thread_count(void);

typedef struct {
    int w, h, scale, threads;
    int objects, objects_drawn;
    int verts, tris_in, tris_drawn;
    float geo_ms, raster_ms, total_ms;
    float xform_ms, tri_ms, bin_ms; /* parts of geo_ms */
    float draw_cpu_ms, out_cpu_ms;  /* raster: drawing and handing out, CPU summed over threads */
    uint32_t frame; /* counts r3_render() calls */
} r3_stats_t;
const r3_stats_t * r3_stats(void);

#endif /* ROLLING_ROB_R3D_H */
