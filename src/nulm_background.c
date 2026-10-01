#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tnmod_api.h"
#include "nulm.h"
#include "game_clear.h"

#define MOD_NAME "NulmBackground"
#define MAX_ATLASES 16

/* raylib / rlgl constants (stable across raylib 4.x and 5.x). */
#define RL_QUADS 0x0007
#define RL_BLEND_ALPHA 0
#define RL_BLEND_CUSTOM_SEPARATE 7
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_DST_COLOR 0x0306
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define RL_SHADER_UNIFORM_VEC4 3
#define RL_TEXTURE_FILTER_BILINEAR 1

#define SWF_BLEND_MULTIPLY 3
#define SWF_BLEND_SCREEN 4
#define SWF_BLEND_ADD 8
#define SWF_BLEND_SUBTRACT 9

typedef struct RlShader {
    unsigned int id;
    int *locs;
} RlShader;

typedef struct RlImage {
    void *data;
    int width;
    int height;
    int mipmaps;
    int format;
} RlImage;

typedef TNTexture2D (*LoadTextureFn)(const char *);
typedef TNTexture2D (*LoadTextureFromImageFn)(RlImage);
typedef RlImage (*GenImageColorFn)(int, int, TNColor);
typedef void (*UnloadImageFn)(RlImage);
typedef void (*UnloadTextureFn)(TNTexture2D);
typedef void (*SetTextureFilterFn)(TNTexture2D, int);
typedef float (*GetFrameTimeFn)(void);
typedef RlShader (*LoadShaderFromMemoryFn)(const char *, const char *);
typedef int (*GetShaderLocationFn)(RlShader, const char *);
typedef void (*SetShaderValueFn)(RlShader, int, const void *, int);
typedef void (*BeginShaderModeFn)(RlShader);
typedef void (*EndShaderModeFn)(void);
typedef void (*RlBeginFn)(int);
typedef void (*RlEndFn)(void);
typedef void (*RlSetTextureFn)(unsigned int);
typedef void (*RlColor4ubFn)(unsigned char, unsigned char, unsigned char, unsigned char);
typedef void (*RlTexCoord2fFn)(float, float);
typedef void (*RlVertex2fFn)(float, float);
typedef void (*RlSetBlendModeFn)(int);
typedef void (*RlSetBlendFactorsSeparateFn)(int, int, int, int, int, int);
typedef void (*RlVoidFn)(void);

static struct {
    LoadTextureFn load_texture;
    LoadTextureFromImageFn load_texture_from_image;
    GenImageColorFn gen_image_color;
    UnloadImageFn unload_image;
    UnloadTextureFn unload_texture;
    SetTextureFilterFn set_texture_filter;
    GetFrameTimeFn get_frame_time;
    LoadShaderFromMemoryFn load_shader;
    GetShaderLocationFn get_shader_location;
    SetShaderValueFn set_shader_value;
    BeginShaderModeFn begin_shader;
    EndShaderModeFn end_shader;
    RlBeginFn rl_begin;
    RlEndFn rl_end;
    RlSetTextureFn rl_set_texture;
    RlColor4ubFn rl_color4ub;
    RlTexCoord2fFn rl_texcoord2f;
    RlVertex2fFn rl_vertex2f;
    RlSetBlendModeFn rl_set_blend_mode;
    RlSetBlendFactorsSeparateFn rl_set_blend_factors;
    RlVoidFn rl_draw_batch;
    RlVoidFn rl_disable_culling;
    RlVoidFn rl_enable_culling;
} gl;

typedef struct Layer {
    const char *name;
    int enabled;
    NulmFile *file;
    NulmClip *clip;
    TNTexture2D atlases[MAX_ATLASES];
    int atlas_loaded[MAX_ATLASES];
    float x;
    float y;
    char loaded_pack[64];
    int from_skin;
    int config_present;
    char config_pack[64];
    float config_x;
    float config_y;
    int config_root;
} Layer;

static const TNModApi *mod_api;
static char game_dir[MAX_PATH];
static Layer upper = {"upper", 0, NULL, NULL, {{0}}, {0}, 0.0f, 0.0f, "", 0, 0, "", 0.0f, 0.0f, -1};
static Layer lower = {"lower", 0, NULL, NULL, {{0}}, {0}, 0.0f, 0.0f, "", 0, 0, "", 0.0f, 0.0f, -1};
static Layer fever = {"fever", 0, NULL, NULL, {{0}}, {0}, 0.0f, 0.0f, "", 0, 0, "", 0.0f, 0.0f, -1};
static Layer dai = {"dai", 0, NULL, NULL, {{0}}, {0}, 0.0f, 0.0f, "", 0, 0, "", 0.0f, 0.0f, -1};
static RlShader color_shader;
static int add_color_location = -1;
static TNTexture2D white_texture;
static int gl_ready;

static const char *const vertex_shader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "uniform mat4 mvp;\n"
    "void main()\n"
    "{\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

/* texel * Mul + Add, the SWF colour transform, in a single pass. */
static const char *const fragment_shader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 addColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 base = texture(texture0, fragTexCoord) * fragColor;\n"
    "    float a = clamp(base.a + addColor.a, 0.0, 1.0);\n"
    "    vec3 rgb = base.rgb + addColor.rgb;\n"
    "    finalColor = vec4(clamp(rgb, 0.0, 1.0), a);\n"
    "}\n";

#define LOAD_PROC(field, name)                                  \
    do {                                                        \
        FARPROC proc_ = mod_api->getOriginalProc(name);         \
        if (proc_ == NULL) {                                    \
            mod_api->log(MOD_NAME, "Missing raylib export " name); \
            return 0;                                           \
        }                                                       \
        memcpy(&gl.field, &proc_, sizeof(gl.field));            \
    } while (0)

static int load_raylib(void)
{
    LOAD_PROC(load_texture, "LoadTexture");
    LOAD_PROC(load_texture_from_image, "LoadTextureFromImage");
    LOAD_PROC(gen_image_color, "GenImageColor");
    LOAD_PROC(unload_image, "UnloadImage");
    LOAD_PROC(unload_texture, "UnloadTexture");
    LOAD_PROC(set_texture_filter, "SetTextureFilter");
    LOAD_PROC(get_frame_time, "GetFrameTime");
    LOAD_PROC(load_shader, "LoadShaderFromMemory");
    LOAD_PROC(get_shader_location, "GetShaderLocation");
    LOAD_PROC(set_shader_value, "SetShaderValue");
    LOAD_PROC(begin_shader, "BeginShaderMode");
    LOAD_PROC(end_shader, "EndShaderMode");
    LOAD_PROC(rl_begin, "rlBegin");
    LOAD_PROC(rl_end, "rlEnd");
    LOAD_PROC(rl_set_texture, "rlSetTexture");
    LOAD_PROC(rl_color4ub, "rlColor4ub");
    LOAD_PROC(rl_texcoord2f, "rlTexCoord2f");
    LOAD_PROC(rl_vertex2f, "rlVertex2f");
    LOAD_PROC(rl_set_blend_mode, "rlSetBlendMode");
    LOAD_PROC(rl_set_blend_factors, "rlSetBlendFactorsSeparate");
    LOAD_PROC(rl_draw_batch, "rlDrawRenderBatchActive");
    LOAD_PROC(rl_disable_culling, "rlDisableBackfaceCulling");
    LOAD_PROC(rl_enable_culling, "rlEnableBackfaceCulling");
    return 1;
}

/* ----------------------------------------------------------------- config */

static char *read_file(const char *path, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0 || size > 64 * 1024 * 1024) { fclose(file); return NULL; }
    char *data = (char *)malloc((size_t)size + 1);
    if (data == NULL) { fclose(file); return NULL; }
    size_t got = fread(data, 1, (size_t)size, file);
    fclose(file);
    data[got] = '\0';
    if (size_out != NULL) *size_out = got;
    return data;
}

/* Copies the object value of "key" ({...}, no nesting) into out. */
static int json_object(const char *text, const char *key, char *out, size_t out_size)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *at = strstr(text, needle);
    if (at == NULL) return 0;
    const char *open = strchr(at, '{');
    if (open == NULL) return 0;
    const char *close = strchr(open, '}');
    if (close == NULL || (size_t)(close - open) + 1 >= out_size) return 0;
    memcpy(out, open, (size_t)(close - open) + 1);
    out[close - open + 1] = '\0';
    return 1;
}

static int json_string(const char *object, const char *key, char *out, size_t out_size)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *at = strstr(object, needle);
    if (at == NULL || (at = strchr(at + strlen(needle), ':')) == NULL ||
        (at = strchr(at, '"')) == NULL) return 0;
    ++at;
    const char *end = strchr(at, '"');
    if (end == NULL || end == at || (size_t)(end - at) >= out_size) return 0;
    memcpy(out, at, (size_t)(end - at));
    out[end - at] = '\0';
    return 1;
}

static float json_number(const char *object, const char *key, float fallback)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *at = strstr(object, needle);
    if (at == NULL || (at = strchr(at + strlen(needle), ':')) == NULL) return fallback;
    return (float)atof(at + 1);
}

static int safe_pack_name(const char *name)
{
    if (name[0] == '\0') return 0;
    for (const char *c = name; *c != '\0'; ++c)
        if (*c == '\\' || *c == '/' || *c == ':' || (c[0] == '.' && c[1] == '.')) return 0;
    return 1;
}

/* ------------------------------------------------------------------- GL */

static int ensure_gl(void)
{
    if (gl_ready) return 1;
    color_shader = gl.load_shader(vertex_shader, fragment_shader);
    if (color_shader.id == 0) {
        mod_api->log(MOD_NAME, "Could not compile the colour transform shader");
        gl_ready = -1;
        return 0;
    }
    add_color_location = gl.get_shader_location(color_shader, "addColor");
    TNColor white = {255, 255, 255, 255};
    RlImage image = gl.gen_image_color(1, 1, white);
    white_texture = gl.load_texture_from_image(image);
    gl.unload_image(image);
    gl_ready = 1;
    return 1;
}

static unsigned char clamp_byte(float v)
{
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (unsigned char)v;
}

static int apply_blend(int blend_mode)
{
    switch (blend_mode) {
    case SWF_BLEND_MULTIPLY:
        gl.rl_set_blend_factors(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE,
                                GL_FUNC_ADD, GL_FUNC_ADD);
        gl.rl_set_blend_mode(RL_BLEND_CUSTOM_SEPARATE);
        return 1;
    case SWF_BLEND_SCREEN:
        gl.rl_set_blend_factors(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE,
                                GL_FUNC_ADD, GL_FUNC_ADD);
        gl.rl_set_blend_mode(RL_BLEND_CUSTOM_SEPARATE);
        return 1;
    case SWF_BLEND_ADD:
        gl.rl_set_blend_factors(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE,
                                GL_FUNC_ADD, GL_FUNC_ADD);
        gl.rl_set_blend_mode(RL_BLEND_CUSTOM_SEPARATE);
        return 1;
    case SWF_BLEND_SUBTRACT:
        gl.rl_set_blend_factors(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE,
                                GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
        gl.rl_set_blend_mode(RL_BLEND_CUSTOM_SEPARATE);
        return 1;
    default:
        gl.rl_set_blend_mode(RL_BLEND_ALPHA);
        return 0;
    }
}

static void draw_graphic(void *user, const NulmFile *file, const NulmGraphic *graphic,
                         const NulmMat *world, const NulmColor *color, int blend_mode)
{
    const Layer *layer = (const Layer *)user;
    TNTexture2D texture = white_texture;
    float mul_r = 1.0f, mul_g = 1.0f, mul_b = 1.0f, mul_a = 1.0f;

    if (graphic->index_count < 3) return;
    if (graphic->fill_type == 0x41) {
        if (graphic->atlas_id >= 0 && graphic->atlas_id < MAX_ATLASES &&
            layer->atlas_loaded[graphic->atlas_id])
            texture = layer->atlases[graphic->atlas_id];
    } else if (graphic->atlas_id >= 0 && graphic->atlas_id < file->color_count) {
        const NulmColorEntry *c = &file->colors[graphic->atlas_id];
        mul_r = (float)c->r / 255.0f;
        mul_g = (float)c->g / 255.0f;
        mul_b = (float)c->b / 255.0f;
        mul_a = (float)c->a / 255.0f;
    }

    unsigned char r = clamp_byte(mul_r * color->mul_r * 255.0f);
    unsigned char g = clamp_byte(mul_g * color->mul_g * 255.0f);
    unsigned char b = clamp_byte(mul_b * color->mul_b * 255.0f);
    unsigned char a = clamp_byte(mul_a * color->mul_a * 255.0f);
    if (r == 0 && g == 0 && b == 0 && a == 0 && color->add_r <= 0.0f &&
        color->add_g <= 0.0f && color->add_b <= 0.0f && color->add_a <= 0.0f) return;

    int custom_blend = apply_blend(blend_mode);
    float add[4] = {color->add_r, color->add_g, color->add_b, color->add_a};
    gl.begin_shader(color_shader);
    gl.set_shader_value(color_shader, add_color_location, add, RL_SHADER_UNIFORM_VEC4);
    gl.rl_set_texture(texture.id);
    gl.rl_begin(RL_QUADS);
    gl.rl_color4ub(r, g, b, a);
    for (int i = 0; i + 2 < graphic->index_count; i += 3) {
        for (int k = 0; k < 4; ++k) {
            int corner = k < 3 ? k : 2;
            const float *vertex = &graphic->vertices[graphic->indices[i + corner] * 4];
            gl.rl_texcoord2f(vertex[2], vertex[3]);
            gl.rl_vertex2f(vertex[0] * world->a + vertex[1] * world->c + world->tx,
                           vertex[0] * world->b + vertex[1] * world->d + world->ty);
        }
    }
    gl.rl_end();
    gl.rl_set_texture(0);
    gl.end_shader();
    if (custom_blend) gl.rl_set_blend_mode(RL_BLEND_ALPHA);
}

static void draw_layer(Layer *layer)
{
    if (!layer->enabled || layer->clip == NULL || !ensure_gl() || gl_ready < 0) return;
    NulmDrawCallbacks callbacks = {layer, draw_graphic};
    /* NULM triangles are not guaranteed to be counter-clockwise. */
    gl.rl_draw_batch();
    gl.rl_disable_culling();
    nulm_clip_draw(layer->clip, layer->x, layer->y, 1.0f, &callbacks);
    gl.rl_draw_batch();
    gl.rl_enable_culling();
}

/* ---------------------------------------------------------------- loading */

static void log_format(const char *format, const char *a, const char *b)
{
    char message[MAX_PATH * 2 + 160];
    snprintf(message, sizeof(message), format, a, b);
    mod_api->log(MOD_NAME, message);
}

static void free_layer(Layer *layer)
{
    for (int i = 0; i < MAX_ATLASES; ++i)
        if (layer->atlas_loaded[i] && gl.unload_texture != NULL) gl.unload_texture(layer->atlases[i]);
    nulm_clip_free(layer->clip);
    nulm_free(layer->file);
    memset(layer->atlases, 0, sizeof(layer->atlases));
    memset(layer->atlas_loaded, 0, sizeof(layer->atlas_loaded));
    layer->clip = NULL;
    layer->file = NULL;
    layer->enabled = 0;
    layer->loaded_pack[0] = '\0';
}

/* Loads <directory>\<pack>\<pack>.nulm and its atlases <pack>_<n>.png. */
static int load_pack(Layer *layer, const char *directory, const char *pack, float x, float y,
                     int root)
{
    char path[MAX_PATH * 2];
    free_layer(layer);
    layer->x = x;
    layer->y = y;
    snprintf(path, sizeof(path), "%s\\%s\\%s.nulm", directory, pack, pack);
    size_t size = 0;
    char *data = read_file(path, &size);
    if (data == NULL) {
        log_format("Could not read %s%s", path, "");
        return 0;
    }
    char error[128] = "";
    layer->file = nulm_load((const unsigned char *)data, size, error, sizeof(error));
    free(data);
    if (layer->file == NULL) {
        log_format("Could not parse %s: %s", path, error);
        return 0;
    }

    if (root < 0) {
        int best_frames = -1;
        for (int i = 0; i < layer->file->track_count; ++i) {
            const NulmTrack *track = &layer->file->tracks[i];
            if (!track->referenced && track->frame_count > best_frames) {
                best_frames = track->frame_count;
                root = track->character_id;
            }
        }
    }
    layer->clip = nulm_clip_create(layer->file, root);
    if (layer->clip == NULL) {
        log_format("The NULM %s has no usable root track%s", path, "");
        free_layer(layer);
        return 0;
    }

    for (int i = 0; i < MAX_ATLASES; ++i) {
        char png[MAX_PATH * 2];
        snprintf(png, sizeof(png), "%s\\%s\\%s_%d.png", directory, pack, pack, i);
        if (GetFileAttributesA(png) == INVALID_FILE_ATTRIBUTES) continue;
        TNTexture2D texture = gl.load_texture(png);
        if (texture.id == 0) continue;
        gl.set_texture_filter(texture, RL_TEXTURE_FILTER_BILINEAR);
        layer->atlases[i] = texture;
        layer->atlas_loaded[i] = 1;
    }
    nulm_clip_play(layer->clip);
    layer->enabled = 1;
    snprintf(layer->loaded_pack, sizeof(layer->loaded_pack), "%s", pack);

    char message[160];
    snprintf(message, sizeof(message), "Loaded the %s background \"%s\" (root %d)",
             layer->name, pack, root);
    mod_api->log(MOD_NAME, message);
    return 1;
}

/* A layer taken from config.json, packs under mods\nulm-background\packs. */
static int load_layer_from_config(Layer *layer)
{
    if (!layer->config_present) return 0;
    char directory[MAX_PATH * 2];
    snprintf(directory, sizeof(directory), "%s\\mods\\nulm-background\\packs", game_dir);
    int ok = load_pack(layer, directory, layer->config_pack, layer->config_x, layer->config_y,
                       layer->config_root);
    layer->from_skin = 0;
    return ok;
}

static void read_layer_config(Layer *layer, const char *object, float default_y)
{
    layer->config_present = 0;
    if (!json_string(object, "pack", layer->config_pack, sizeof(layer->config_pack))) return;
    if (!safe_pack_name(layer->config_pack)) {
        mod_api->log(MOD_NAME, "Ignored a layer with an invalid pack name");
        return;
    }
    layer->config_x = json_number(object, "x", 0.0f);
    layer->config_y = json_number(object, "y", default_y);
    layer->config_root = (int)json_number(object, "root", -1.0f);
    layer->config_present = 1;
}
/* ---------------------------------------------------------------- state */

#define SCENE_ABSENT_FRAMES 120
#define FEVER_LOOP_FRAME 30

static const char *const fever_scroll_clips[3] = {"fever_mc", "scroll_left_mc", "scroll_left2_mc"};

static int upper_drawn_this_frame;
static int lower_drawn_this_frame;
static int scene_absent_frames = 1000000;
static int clear_state;
static int fever_scroll_pending;
/* How the fever state is decided: the game's own clear decision (default),
   the drawn gauge length, or never. */
enum { CLEAR_FROM_GAME, CLEAR_FROM_GAUGE, CLEAR_OFF };
static int clear_mode = CLEAR_FROM_GAME;
static int game_clear_ok;
/* Gauge length mode: the game draws one tile per filled segment. */
static int gauge_tile_width = 21;
static int gauge_tile_height = 66;
static int gauge_clear_tiles = 40;
#define GAUGE_HYSTERESIS 3
static int gauge_tiles_this_frame;

static void reset_state(void)
{
    game_clear_forget();
    clear_state = 0;
    fever_scroll_pending = 0;
    if (upper.clip != NULL) nulm_clip_play(upper.clip);
    if (lower.clip != NULL) nulm_clip_play(lower.clip);
    if (fever.clip != NULL) {
        nulm_clip_play(fever.clip);
        nulm_clip_goto_label(fever.clip, "init", 1, 0);
    }
    if (dai.clip != NULL) nulm_clip_play(dai.clip);
}

/* The upper and fever backgrounds cross over with the same label pair. */
static void set_clear(int clear)
{
    if (clear == clear_state) return;
    clear_state = clear;
    {
        char message[96];
        snprintf(message, sizeof(message), "Fever %s", clear ? "on" : "off");
        mod_api->log(MOD_NAME, message);
    }
    const char *label = clear ? "normal_fever" : "fever_normal";
    if (upper.clip != NULL) nulm_clip_goto_label(upper.clip, label, 0, 1);
    if (fever.clip != NULL) {
        nulm_clip_goto_label(fever.clip, label, 0, 1);
        fever_scroll_pending = 0;
        if (clear) {
            for (int i = 0; i < 3; ++i) nulm_clip_seek_child(fever.clip, fever_scroll_clips[i], 0, 1);
            fever_scroll_pending = 1;
        }
    }
}

/* ---------------------------------------------------------------- skin lumens */

#define MAX_CANDIDATES 64
#define LAYER_COUNT 4

typedef struct Candidates {
    char names[MAX_CANDIDATES][64];
    int count;
} Candidates;

static char current_skin[64];
static int song_pending;
static int have_lifecycle;
static int frame_counter;
static int last_load_frame = -1000000;

static int has_prefix(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* Pack names follow the arcade data: donbg_* upper, bg_nomal_* lower,
   bg_fever_* fever, bg_dai_* stand. Returns the layer index or -1. */
static int category_of(const char *name)
{
    if (has_prefix(name, "donbg_")) return 0;
    if (has_prefix(name, "bg_nomal_") || has_prefix(name, "bg_normal_")) return 1;
    if (has_prefix(name, "bg_fever_")) return 2;
    if (has_prefix(name, "bg_dai_")) return 3;
    return -1;
}

static void set_skin(const char *start, size_t length)
{
    if (length == 0 || length >= sizeof(current_skin)) return;
    char skin[64];
    memcpy(skin, start, length);
    skin[length] = '\0';
    if (!safe_pack_name(skin)) return;
    snprintf(current_skin, sizeof(current_skin), "%s", skin);
}

/* Skin file names the game loads look like Skins\<skin>\Image\... */
static void note_skin_from_path(const char *file_name)
{
    for (const char *at = file_name; *at != '\0'; ++at) {
        if ((at == file_name || at[-1] == '\\' || at[-1] == '/') &&
            _strnicmp(at, "Skins", 5) == 0 && (at[5] == '\\' || at[5] == '/')) {
            const char *start = at + 6;
            while (*start == '\\' || *start == '/') ++start;
            const char *end = start;
            while (*end != '\0' && *end != '\\' && *end != '/') ++end;
            set_skin(start, (size_t)(end - start));
            return;
        }
    }
}

/* GameConfig.json holds "skinPath": "Skins//<skin>". */
static void read_skin_from_config(void)
{
    char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "%s\\Config\\GameConfig.json", game_dir);
    char *text = read_file(path, NULL);
    if (text == NULL) return;
    char value[128];
    if (json_string(text, "skinPath", value, sizeof(value))) {
        const char *last = value;
        for (const char *c = value; *c != '\0'; ++c)
            if ((*c == '\\' || *c == '/') && c[1] != '\0') last = c + 1;
        size_t length = strlen(last);
        while (length > 0 && (last[length - 1] == '\\' || last[length - 1] == '/')) --length;
        set_skin(last, length);
    }
    free(text);
}

static void scan_lumens(const char *lumens_dir, Candidates *candidates)
{
    char pattern[MAX_PATH * 4];
    WIN32_FIND_DATAA entry;
    snprintf(pattern, sizeof(pattern), "%s\\*", lumens_dir);
    HANDLE handle = FindFirstFileA(pattern, &entry);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (entry.cFileName[0] == '.' || strlen(entry.cFileName) >= 64) continue;
        int category = category_of(entry.cFileName);
        if (category < 0 || candidates[category].count >= MAX_CANDIDATES) continue;
        char nulm[MAX_PATH * 4];
        snprintf(nulm, sizeof(nulm), "%s\\%s\\%s.nulm", lumens_dir, entry.cFileName,
                 entry.cFileName);
        if (GetFileAttributesA(nulm) == INVALID_FILE_ATTRIBUTES) continue;
        snprintf(candidates[category].names[candidates[category].count++], 64, "%s",
                 entry.cFileName);
    } while (FindNextFileA(handle, &entry));
    FindClose(handle);
}

/* Picks one pack per layer from the selected skin's Lumens folder for the song
   that is about to start. A layer the skin has no pack for falls back to
   config.json. */
static void select_packs_for_song(void)
{
    static Candidates candidates[LAYER_COUNT];
    Layer *layers[LAYER_COUNT] = {&upper, &lower, &fever, &dai};
    const float default_y[LAYER_COUNT] = {0.0f, 540.0f, 540.0f, 540.0f};
    char lumens_dir[MAX_PATH * 2];

    memset(candidates, 0, sizeof(candidates));
    if (current_skin[0] == '\0') read_skin_from_config();
    if (current_skin[0] != '\0') {
        snprintf(lumens_dir, sizeof(lumens_dir), "%s\\Skins\\%s\\Lumens", game_dir, current_skin);
        scan_lumens(lumens_dir, candidates);
    }

    for (int i = 0; i < LAYER_COUNT; ++i) {
        Layer *layer = layers[i];
        int count = candidates[i].count;
        if (count > 0) {
            int pick = rand() % count;
            if (count > 1 && strcmp(candidates[i].names[pick], layer->loaded_pack) == 0)
                pick = (pick + 1 + rand() % (count - 1)) % count;
            if (load_pack(layer, lumens_dir, candidates[i].names[pick], 0.0f, default_y[i], -1)) {
                layer->from_skin = 1;
                continue;
            }
        }
        if (layer->from_skin || !layer->enabled) {
            if (!load_layer_from_config(layer)) free_layer(layer);
        }
    }
}
/* ---------------------------------------------------------------- callbacks */

static void __cdecl on_load_texture(const TNLoadTextureEvent *event)
{
    if (event == NULL || event->fileName == NULL) return;
    note_skin_from_path(event->fileName);
    if (strstr(event->fileName, "EnsoGame") != NULL || strstr(event->fileName, "DanGame") != NULL) {
        /* the first skin texture of a song starts a new loading burst */
        if (frame_counter - last_load_frame > 30) song_pending = 1;
        last_load_frame = frame_counter;
    }
}

static void __cdecl on_begin_frame(void)
{
    ++frame_counter;
    if (song_pending) {
        song_pending = 0;
        select_packs_for_song();
        reset_state();
    } else if (lower_drawn_this_frame) {
        if (scene_absent_frames >= SCENE_ABSENT_FRAMES) {
            if (!have_lifecycle) select_packs_for_song();
            reset_state();
        }
    }
    if (lower_drawn_this_frame) {
        scene_absent_frames = 0;
    } else if (scene_absent_frames < 1000000) {
        ++scene_absent_frames;
    }
    lower_drawn_this_frame = 0;
    upper_drawn_this_frame = 0;

    if (clear_mode == CLEAR_FROM_GAME) {
        /* the game evaluates IsGaugeClear itself; keep its latest answer */
        int state = game_clear_latest();
        if (state >= 0) set_clear(state);
    } else if (clear_mode == CLEAR_FROM_GAUGE && gauge_tiles_this_frame > 0) {
        /* Once cleared it is kept until the gauge visibly drops below the
           border; frames without tiles (a full gauge is one sprite) change
           nothing. */
        if (gauge_tiles_this_frame >= gauge_clear_tiles) set_clear(1);
        else if (gauge_tiles_this_frame <= gauge_clear_tiles - GAUGE_HYSTERESIS) set_clear(0);
    }
    gauge_tiles_this_frame = 0;
    if (scene_absent_frames >= SCENE_ABSENT_FRAMES) return;
    float dt = gl.get_frame_time();
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    if (upper.clip != NULL) nulm_clip_update(upper.clip, dt, 1);
    if (lower.clip != NULL) nulm_clip_update(lower.clip, dt, 1);
    if (fever.clip != NULL) {
        nulm_clip_update(fever.clip, dt, 1);
        if (fever_scroll_pending && nulm_clip_current_frame(fever.clip) >= FEVER_LOOP_FRAME) {
            for (int i = 0; i < 3; ++i) nulm_clip_play_child(fever.clip, fever_scroll_clips[i]);
            fever_scroll_pending = 0;
        }
    }
    if (dai.clip != NULL) nulm_clip_update(dai.clip, dt, 1);
}

/* The game renders the scene into an offscreen target and draws each
   skin background as one 1920-wide texture: about 540 tall for the lower
   background, about 276 tall for the upper one. The matching draw is replaced
   at the same point in the frame, so every later draw stays on top. */
static void __cdecl on_draw_texture_pro(TNDrawTextureProEvent *event)
{
    const TNTexture2D *texture = &event->texture;
    if ((int)event->source.width == gauge_tile_width &&
        (int)event->source.height == gauge_tile_height &&
        (int)event->destination.height == gauge_tile_height)
        ++gauge_tiles_this_frame;
    if (texture->width < 1900 || texture->width > 1940) return;
    float top = event->destination.y - event->origin.y;

    if (lower.enabled && texture->height >= 500 && texture->height <= 560 && top >= 270.0f) {
        if (upper.enabled && !upper_drawn_this_frame) {
            draw_layer(&upper);
            upper_drawn_this_frame = 1;
        }
        draw_layer(&lower);
        draw_layer(&fever);
        draw_layer(&dai);
        lower_drawn_this_frame = 1;
        event->skipOriginal = 1;
    } else if (upper.enabled && texture->height >= 250 && texture->height <= 300 &&
               top < 100.0f) {
        if (!upper_drawn_this_frame) draw_layer(&upper);
        upper_drawn_this_frame = 1;
        event->skipOriginal = 1;
    }
}

TNMOD_EXPORT int __cdecl TaikoNautsModInit(const TNModApi *api)
{
    if (api == NULL || api->version != TNMOD_API_VERSION || api->size < sizeof(TNModApi) ||
        api->getOriginalProc == NULL || api->getGameDirectory == NULL || api->log == NULL ||
        api->registerBeginFrame == NULL || api->registerDrawTexturePro == NULL) return 0;
    mod_api = api;
    snprintf(game_dir, sizeof(game_dir), "%s", api->getGameDirectory());
    if (!load_raylib()) return 0;
    srand((unsigned int)(GetTickCount() ^ (GetCurrentProcessId() << 8)));

    /* config.json is optional: backgrounds can come from the skin's Lumens
       folder alone. */
    char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "%s\\mods\\nulm-background\\config.json", game_dir);
    char *text = read_file(path, NULL);
    if (text != NULL) {
        char object[512];
        char mode[16];
        if (json_string(text, "clearDetection", mode, sizeof(mode))) {
            if (strcmp(mode, "gauge") == 0) clear_mode = CLEAR_FROM_GAUGE;
            else if (strcmp(mode, "off") == 0) clear_mode = CLEAR_OFF;
        }
        if (json_object(text, "gauge", object, sizeof(object))) {
            gauge_tile_width = (int)json_number(object, "tileWidth", (float)gauge_tile_width);
            gauge_tile_height = (int)json_number(object, "tileHeight", (float)gauge_tile_height);
            gauge_clear_tiles = (int)json_number(object, "clearTiles", (float)gauge_clear_tiles);
        }
        if (json_object(text, "upper", object, sizeof(object))) read_layer_config(&upper, object, 0.0f);
        if (json_object(text, "lower", object, sizeof(object))) read_layer_config(&lower, object, 540.0f);
        if (json_object(text, "fever", object, sizeof(object))) read_layer_config(&fever, object, 540.0f);
        if (json_object(text, "dai", object, sizeof(object))) read_layer_config(&dai, object, 540.0f);
        free(text);
        load_layer_from_config(&upper);
        load_layer_from_config(&lower);
        load_layer_from_config(&fever);
        load_layer_from_config(&dai);
    } else {
        api->log(MOD_NAME, "No config.json; backgrounds come from the skin's Lumens folder");
    }
    read_skin_from_config();

    if (clear_mode == CLEAR_FROM_GAME) {
        char message[256];
        game_clear_ok = game_clear_install(message, sizeof(message));
        api->log(MOD_NAME, message);
        if (!game_clear_ok) clear_mode = CLEAR_OFF;
    }
    reset_state();

    const TNModTextureLifecycleApiV1 *lifecycle = (const TNModTextureLifecycleApiV1 *)
        (api->queryExtension != NULL ? api->queryExtension(TNMOD_TEXTURE_LIFECYCLE_API_NAME,
                                                           TNMOD_TEXTURE_LIFECYCLE_API_VERSION)
                                     : NULL);
    if (lifecycle != NULL && lifecycle->size >= sizeof(TNModTextureLifecycleApiV1) &&
        lifecycle->registerLoadTexture != NULL)
        have_lifecycle = lifecycle->registerLoadTexture(on_load_texture);
    if (!have_lifecycle)
        api->log(MOD_NAME, "Texture lifecycle extension unavailable; packs change when a song's first frame is drawn");

    if (!api->registerBeginFrame(on_begin_frame) ||
        !api->registerDrawTexturePro(on_draw_texture_pro)) return 0;
    api->log(MOD_NAME, "Initialized");
    return 1;
}
TNMOD_EXPORT void __cdecl TaikoNautsModShutdown(void)
{
    game_clear_remove();
    free_layer(&upper);
    free_layer(&lower);
    free_layer(&fever);
    free_layer(&dai);
    if (mod_api != NULL) mod_api->log(MOD_NAME, "Shutdown");
}