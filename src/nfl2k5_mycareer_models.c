/* glTF furniture for the MyCareer home's host-drawn rooms (xbox_hostscene.h).
 *
 * mc_model_load() reads a .gltf (cgltf, MIT, src/third_party/cgltf) with its base-colour
 * textures (stb_image, static here so it cannot clash with another copy) and registers one host
 * mesh per primitive: a triangle list in the room's axes (glTF is right-handed, the room is
 * left-handed: z is negated) with the node's world transform applied, metres as glTF has them.
 * The models are CC0 files the player installs beside the game; nothing is in the repository. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "../external/xboxrecomp/src/kernel/xbox_hostscene.h"

#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf/cgltf.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_HDR
#define STBI_NO_STDIO_FALLBACK
#include "third_party/stb/stb_image.h"

/* Flat low-poly colours (the Kenney kit's carpetBlue, carpet, wood...) toned to a modern
 * apartment: charcoal and oatmeal fabrics, walnut, dark metal. Textured materials keep theirs. */
static void modern_colour(const cgltf_material *mat, unsigned char c[4])
{
    static const struct { const char *name; unsigned char rgb[3]; } k[] = {
        { "carpetblue", { 72, 76, 82 } },     { "carpetwhite", { 226, 222, 214 } }, { "carpetred", { 150, 142, 132 } },
        { "carpet", { 190, 178, 160 } },      { "wooddark", { 70, 52, 40 } },       { "wood", { 120, 86, 60 } },
        { "metaldark", { 38, 38, 40 } },      { "metal", { 60, 60, 64 } },          { "fabric", { 120, 116, 110 } },
    };
    char n[64];
    size_t i;
    if (!mat || !mat->name || getenv("NFL2K5_MC_HOME_KIT_COLOURS")) return;
    for (i = 0; i < sizeof n - 1 && mat->name[i]; i++) n[i] = (char)tolower((unsigned char)mat->name[i]);
    n[i] = 0;
    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!strncmp(n, k[i].name, strlen(k[i].name))) { c[0] = k[i].rgb[0]; c[1] = k[i].rgb[1]; c[2] = k[i].rgb[2]; return; }
}

/* The model's textures, decoded once: a 1x1 of the base colour when a material has none. */
static int load_texture(const char *dir, const cgltf_material *mat, uint32_t **out, int *w, int *h)
{
    const cgltf_texture_view *tv = mat && mat->has_pbr_metallic_roughness ? &mat->pbr_metallic_roughness.base_color_texture : NULL;
    const float *f = mat && mat->has_pbr_metallic_roughness ? mat->pbr_metallic_roughness.base_color_factor : NULL;
    if (tv && tv->texture && tv->texture->image && tv->texture->image->uri) {
        char path[1024];
        int n;
        unsigned char *px;
        snprintf(path, sizeof path, "%s/%s", dir, tv->texture->image->uri);
        px = stbi_load(path, w, h, &n, 4);
        if (px) {
            size_t i, count = (size_t)*w * (size_t)*h;
            *out = (uint32_t *)malloc(count * 4);
            if (!*out) { stbi_image_free(px); return 0; }
            memcpy(*out, px, count * 4);
            if (f && (f[0] < 0.999f || f[1] < 0.999f || f[2] < 0.999f))       /* tinted textures */
                for (i = 0; i < count; i++) {
                    unsigned char *c = (unsigned char *)&(*out)[i];
                    c[0] = (unsigned char)(c[0] * f[0]); c[1] = (unsigned char)(c[1] * f[1]); c[2] = (unsigned char)(c[2] * f[2]);
                }
            stbi_image_free(px);
            return 1;
        }
        fprintf(stderr, "[MCMODEL] texture %s: %s\n", path, stbi_failure_reason());
    }
    *w = *h = 1;
    *out = (uint32_t *)malloc(4);
    if (!*out) return 0;
    {
        unsigned char *c = (unsigned char *)*out;
        c[0] = (unsigned char)(255.0f * (f ? f[0] : 0.8f)); c[1] = (unsigned char)(255.0f * (f ? f[1] : 0.8f));
        c[2] = (unsigned char)(255.0f * (f ? f[2] : 0.8f)); c[3] = 255;
        modern_colour(mat, c);
    }
    return 1;
}

/* The surface detail the room's renderer adds (HS_MAT_*), from the material's name: flat
 * low-poly colours read as fabric or wood instead of plastic. */
static int material_hint(const cgltf_material *m)
{
    char n[64];
    size_t i;
    if (!m || !m->name) return HS_MAT_MATTE;
    for (i = 0; i < sizeof n - 1 && m->name[i]; i++) n[i] = (char)tolower((unsigned char)m->name[i]);
    n[i] = 0;
    if (strstr(n, "carpet") || strstr(n, "fabric") || strstr(n, "cloth") || strstr(n, "cushion") || strstr(n, "leather")) return HS_MAT_FABRIC;
    if (strstr(n, "wood")) return HS_MAT_WOOD;
    if (strstr(n, "metal") || strstr(n, "chrome") || strstr(n, "steel")) return HS_MAT_GLASS;
    if (strstr(n, "glass")) return HS_MAT_GLASS;
    return HS_MAT_MATTE;
}

/* Flat colours on fabric and wood get a real surface: an ambientCG CC0 texture (Fabric086,
 * WoodFloor051) mapped by world position, its brightness kept and its colour replaced by the
 * material's. mc_model_textures() names the folder holding <name>/<name>_1K-JPG_Color.jpg. */
static char s_detail_dir[512];
void mc_model_textures(const char *dir) { snprintf(s_detail_dir, sizeof s_detail_dir, "%s", dir ? dir : ""); }

static int detail_texture(int hint, const unsigned char tint[4], uint32_t **out, int *w, int *h)
{
    static struct { int hint, w, h; unsigned char *lum; float avg; } cache[2];
    const char *name = hint == HS_MAT_FABRIC ? "Fabric086" : hint == HS_MAT_WOOD ? "WoodFloor051" : NULL;
    int slot = hint == HS_MAT_FABRIC ? 0 : 1;
    size_t i, count;
    if (!name || !s_detail_dir[0]) return 0;
    if (!cache[slot].lum) {
        char path[1024];
        int n, tw, th;
        unsigned char *px;
        double sum = 0.0;
        snprintf(path, sizeof path, "%s/%s/%s_1K-JPG_Color.jpg", s_detail_dir, name, name);
        px = stbi_load(path, &tw, &th, &n, 4);
        if (!px) { fprintf(stderr, "[MCMODEL] detail %s: %s" "\n", path, stbi_failure_reason()); return 0; }
        count = (size_t)tw * (size_t)th;
        cache[slot].lum = (unsigned char *)malloc(count);
        if (!cache[slot].lum) { stbi_image_free(px); return 0; }
        for (i = 0; i < count; i++) {
            unsigned char *c = px + i * 4;
            cache[slot].lum[i] = (unsigned char)((c[0] * 54 + c[1] * 183 + c[2] * 19) >> 8);
            sum += cache[slot].lum[i];
        }
        stbi_image_free(px);
        cache[slot].w = tw; cache[slot].h = th; cache[slot].avg = (float)(sum / (double)count);
        if (cache[slot].avg < 1.0f) cache[slot].avg = 1.0f;
    }
    count = (size_t)cache[slot].w * (size_t)cache[slot].h;
    *out = (uint32_t *)malloc(count * 4);
    if (!*out) return 0;
    for (i = 0; i < count; i++) {
        float k = cache[slot].lum[i] / cache[slot].avg;
        unsigned char *c = (unsigned char *)&(*out)[i];
        int j;
        for (j = 0; j < 3; j++) { float v = tint[j] * k; c[j] = (unsigned char)(v > 255.0f ? 255.0f : v); }
        c[3] = 255;
    }
    *w = cache[slot].w; *h = cache[slot].h;
    return 1;
}

/* The view out of the windows: an equirectangular .hdr (a Poly Haven CC0 HDRI the player puts
 * beside the game), tone-mapped to 8 bits, on a quad over the window wall (x = wx, z0..z1,
 * y0..y1), turned by turn (0..1 of a full circle). Returns the mesh id or -1. */
int mc_window_view(const char *hdr_path, float wx, float z0, float z1, float y0, float y1, float turn, float exposure)
{
    int w, h, n, id;
    size_t i, count;
    float *px = stbi_loadf(hdr_path, &w, &h, &n, 3);
    uint32_t *rgba;
    float quad[6][8], tri[6][2] = { { z0, y0 }, { z1, y0 }, { z1, y1 }, { z0, y0 }, { z1, y1 }, { z0, y1 } };
    if (!px) { fprintf(stderr, "[MCMODEL] window view %s: %s" "\n", hdr_path, stbi_failure_reason()); return -1; }
    count = (size_t)w * (size_t)h;
    rgba = (uint32_t *)malloc(count * 4);
    if (!rgba) { stbi_image_free(px); return -1; }
    for (i = 0; i < count; i++) {
        unsigned char *c = (unsigned char *)&rgba[i];
        int k;
        for (k = 0; k < 3; k++) {
            float v = 1.0f - expf(-px[i * 3 + k] * exposure);              /* exposure, soft shoulder */
            c[k] = (unsigned char)(255.0f * powf(v < 0.0f ? 0.0f : v, 1.0f / 2.2f) + 0.5f);
        }
        c[3] = 255;
    }
    stbi_image_free(px);
    for (i = 0; i < 6; i++) {
        float *o = quad[i];
        o[0] = wx; o[1] = tri[i][1]; o[2] = tri[i][0];
        o[3] = -1.0f; o[4] = 0.0f; o[5] = 0.0f;
        o[6] = turn; o[7] = 0.0f;
    }
    id = xbox_HostMeshAdd(&quad[0][0], 6, rgba, w, h, HS_MAT_EMISSIVE | HS_MESH_PANORAMA);
    free(rgba);
    fprintf(stderr, "[MCMODEL] window view %s: %dx%d, mesh %d" "\n", hdr_path, w, h, id);
    return id;
}

/* Registers the model's primitives as host meshes; returns how many ids it wrote. The model's
 * footprint (x/z extent, lowest y) in room axes goes to bounds[6] when it is not NULL. */
int mc_model_load(const char *gltf_path, int *ids, int max, float bounds[6])
{
    cgltf_options opt;
    cgltf_data *d = NULL;
    char dir[1024];
    const char *slash;
    size_t ni;
    int count = 0;
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    memset(&opt, 0, sizeof opt);
    if (cgltf_parse_file(&opt, gltf_path, &d) != cgltf_result_success) {
        fprintf(stderr, "[MCMODEL] %s: not readable\n", gltf_path);
        return 0;
    }
    if (cgltf_load_buffers(&opt, d, gltf_path) != cgltf_result_success) {
        fprintf(stderr, "[MCMODEL] %s: buffers missing\n", gltf_path);
        cgltf_free(d);
        return 0;
    }
    slash = strrchr(gltf_path, '/');
    if (!slash || (strrchr(gltf_path, '\\') && strrchr(gltf_path, '\\') > slash)) slash = strrchr(gltf_path, '\\');
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - gltf_path) : 1, slash ? gltf_path : ".");
    for (ni = 0; ni < d->nodes_count && count < max; ni++) {
        const cgltf_node *node = &d->nodes[ni];
        float world[16];
        size_t pi;
        if (!node->mesh) continue;
        cgltf_node_transform_world(node, world);
        for (pi = 0; pi < node->mesh->primitives_count && count < max; pi++) {
            const cgltf_primitive *p = &node->mesh->primitives[pi];
            const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL;
            size_t ai, nidx, t;
            float *pnu;
            uint32_t *rgba = NULL;
            int tw, th, id;
            if (p->type != cgltf_primitive_type_triangles) continue;
            for (ai = 0; ai < p->attributes_count; ai++) {
                const cgltf_attribute *a = &p->attributes[ai];
                if (a->type == cgltf_attribute_type_position) pos = a->data;
                else if (a->type == cgltf_attribute_type_normal) nrm = a->data;
                else if (a->type == cgltf_attribute_type_texcoord && a->index == 0) uv = a->data;
            }
            if (!pos) continue;
            nidx = p->indices ? p->indices->count : pos->count;
            pnu = (float *)calloc(nidx, sizeof(float) * 8);
            if (!pnu) continue;
            for (t = 0; t < nidx; t++) {
                size_t vi = p->indices ? cgltf_accessor_read_index(p->indices, t) : t;
                float v[3] = { 0 }, n[3] = { 0, 1, 0 }, u[2] = { 0 }, *o = pnu + t * 8;
                int k;
                cgltf_accessor_read_float(pos, vi, v, 3);
                if (nrm) cgltf_accessor_read_float(nrm, vi, n, 3);
                if (uv) cgltf_accessor_read_float(uv, vi, u, 2);
                for (k = 0; k < 3; k++) {
                    o[k] = v[0] * world[k] + v[1] * world[4 + k] + v[2] * world[8 + k] + world[12 + k];
                    o[3 + k] = n[0] * world[k] + n[1] * world[4 + k] + n[2] * world[8 + k];
                }
                o[2] = -o[2]; o[5] = -o[5];                 /* glTF right-handed -> the room's left-handed axes */
                o[6] = u[0]; o[7] = u[1];
                for (k = 0; k < 3; k++) { if (o[k] < lo[k]) lo[k] = o[k]; if (o[k] > hi[k]) hi[k] = o[k]; }
            }
            if (!load_texture(dir, p->material, &rgba, &tw, &th)) { free(pnu); continue; }
            {
                int hint = material_hint(p->material), flags = 0;
                if (tw == 1 && th == 1 && (hint == HS_MAT_FABRIC || hint == HS_MAT_WOOD)) {
                    uint32_t *d = NULL;
                    unsigned char tint[4];
                    memcpy(tint, rgba, 4);
                    if (detail_texture(hint, tint, &d, &tw, &th)) { free(rgba); rgba = d; flags = HS_MESH_TRIPLANAR; }
                    else { tw = th = 1; }
                }
                id = xbox_HostMeshAdd(pnu, (int)nidx, rgba, tw, th, hint | flags);
            }
            free(pnu); free(rgba);
            if (id >= 0) ids[count++] = id;
        }
    }
    cgltf_free(d);
    if (bounds) { bounds[0] = lo[0]; bounds[1] = lo[1]; bounds[2] = lo[2]; bounds[3] = hi[0]; bounds[4] = hi[1]; bounds[5] = hi[2]; }
    fprintf(stderr, "[MCMODEL] %s: %d meshes, size %.2f x %.2f x %.2f m\n", gltf_path, count, hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
    return count;
}
