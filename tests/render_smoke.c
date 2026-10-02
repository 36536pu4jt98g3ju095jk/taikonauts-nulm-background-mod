/* Loads a raylib DLL, runs the mod against a fake ModLoader and renders the
   configured backgrounds into a 1920x1080 render texture, like the game does.
   usage: render_smoke <raylib.dll> <game-dir> <output.png> [frames] */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tnmod_api.h"

typedef struct RlRenderTexture {
    unsigned int id;
    TNTexture2D texture;
    TNTexture2D depth;
} RlRenderTexture;

typedef struct RlImage {
    void *data;
    int width;
    int height;
    int mipmaps;
    int format;
} RlImage;

static HMODULE raylib;
static char game_directory[MAX_PATH];
static TNFrameCallback begin_callback;
static TNDrawTextureProCallback draw_callback;

static FARPROC __cdecl fake_get_original_proc(const char *name) { return GetProcAddress(raylib, name); }
static const char *__cdecl fake_get_game_directory(void) { return game_directory; }
static void __cdecl fake_log(const char *mod, const char *message) { printf("[%s] %s\n", mod, message); }
static int __cdecl fake_register_begin(TNFrameCallback cb) { begin_callback = cb; return 1; }
static int __cdecl fake_register_end(TNFrameCallback cb) { (void)cb; return 1; }
static int __cdecl fake_register_pro(TNDrawTextureProCallback cb) { draw_callback = cb; return 1; }
static int __cdecl fake_register_rec(TNDrawTextureRecCallback cb) { (void)cb; return 1; }
static int __cdecl fake_register_text(TNDrawTextCallback cb) { (void)cb; return 1; }
static int __cdecl fake_register_text_ex(TNDrawTextExCallback cb) { (void)cb; return 1; }
static const void *__cdecl fake_query(const char *name, unsigned int version) { (void)name; (void)version; return NULL; }

typedef void (*SetConfigFlagsFn)(unsigned int);
typedef void (*InitWindowFn)(int, int, const char *);
typedef RlRenderTexture (*LoadRenderTextureFn)(int, int);
typedef void (*VoidFn)(void);
typedef void (*TextureModeFn)(RlRenderTexture);
typedef void (*ClearBackgroundFn)(TNColor);
typedef RlImage (*LoadImageFromTextureFn)(TNTexture2D);
typedef int (*ExportImageFn)(RlImage, const char *);
typedef void (*DrawRectangleFn)(int, int, int, int, TNColor);
typedef void (*ImageFlipVerticalFn)(RlImage *);
typedef void (*SetTargetFPSFn)(int);

static void *symbol(const char *name)
{
    FARPROC proc = GetProcAddress(raylib, name);
    void *out = NULL;
    memcpy(&out, &proc, sizeof(out));
    return out;
}
int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: render_smoke raylib.dll game-dir out.png [frames]\n"); return 2; }
    int frames = argc > 4 ? atoi(argv[4]) : 120;
    raylib = LoadLibraryA(argv[1]);
    if (raylib == NULL) { fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    snprintf(game_directory, sizeof(game_directory), "%s", argv[2]);

    SetConfigFlagsFn SetConfigFlags = (SetConfigFlagsFn)symbol("SetConfigFlags");
    InitWindowFn InitWindow = (InitWindowFn)symbol("InitWindow");
    LoadRenderTextureFn LoadRenderTexture = (LoadRenderTextureFn)symbol("LoadRenderTexture");
    VoidFn BeginDrawing = (VoidFn)symbol("BeginDrawing");
    VoidFn EndDrawing = (VoidFn)symbol("EndDrawing");
    TextureModeFn BeginTextureMode = (TextureModeFn)symbol("BeginTextureMode");
    VoidFn EndTextureMode = (VoidFn)symbol("EndTextureMode");
    ClearBackgroundFn ClearBackground = (ClearBackgroundFn)symbol("ClearBackground");
    LoadImageFromTextureFn LoadImageFromTexture = (LoadImageFromTextureFn)symbol("LoadImageFromTexture");
    ExportImageFn ExportImage = (ExportImageFn)symbol("ExportImage");
    VoidFn CloseWindow = (VoidFn)symbol("CloseWindow");
    DrawRectangleFn DrawRectangle = (DrawRectangleFn)symbol("DrawRectangle");
    ImageFlipVerticalFn ImageFlipVertical = (ImageFlipVerticalFn)symbol("ImageFlipVertical");
    SetTargetFPSFn SetTargetFPS = (SetTargetFPSFn)symbol("SetTargetFPS");
    const char *tile_plan = getenv("SMOKE_TILES");
    if (!InitWindow || !LoadRenderTexture || !ExportImage) { fprintf(stderr, "missing raylib exports\n"); return 2; }

    SetConfigFlags(0x80 /* FLAG_WINDOW_HIDDEN */);
    InitWindow(1280, 720, "render_smoke");
    SetTargetFPS(60);

    TNModApi api;
    memset(&api, 0, sizeof(api));
    api.version = TNMOD_API_VERSION;
    api.size = sizeof(api);
    api.getOriginalProc = fake_get_original_proc;
    api.getGameDirectory = fake_get_game_directory;
    api.log = fake_log;
    api.registerBeginFrame = fake_register_begin;
    api.registerEndFrame = fake_register_end;
    api.registerDrawText = fake_register_text;
    api.registerDrawTextEx = fake_register_text_ex;
    api.registerDrawTexturePro = fake_register_pro;
    api.registerDrawTextureRec = fake_register_rec;
    api.queryExtension = fake_query;

    char dll_path[MAX_PATH * 2];
    snprintf(dll_path, sizeof(dll_path), "%s", argc > 5 ? argv[5] : "mod\\nulm_background.dll");
    HMODULE mod = LoadLibraryA(dll_path);
    if (mod == NULL) { fprintf(stderr, "cannot load %s\n", dll_path); return 2; }
    int (__cdecl *init)(const TNModApi *) = NULL;
    {
        FARPROC proc = GetProcAddress(mod, "TaikoNautsModInit");
        memcpy(&init, &proc, sizeof(init));
    }
    if (init == NULL || !init(&api) || draw_callback == NULL) { fprintf(stderr, "mod init failed\n"); return 1; }

    RlRenderTexture target = LoadRenderTexture(1920, 1080);
    TNColor black = {0, 0, 0, 255};
    for (int frame = 0; frame < frames; ++frame) {
        BeginDrawing();
        if (begin_callback != NULL) begin_callback();
        BeginTextureMode(target);
        ClearBackground(black);
        TNDrawTextureProEvent lower;
        memset(&lower, 0, sizeof(lower));
        lower.texture.id = 9999;
        lower.texture.width = 1920;
        lower.texture.height = 540;
        lower.destination.x = 960.0f;
        lower.destination.y = 810.0f;
        lower.destination.width = 1920.0f;
        lower.destination.height = 540.0f;
        lower.origin.x = 960.0f;
        lower.origin.y = 270.0f;
        if (tile_plan != NULL) {
            /* SMOKE_TILES="60:45,150:30": from frame N the gauge shows M tiles */
            int tiles = 0;
            for (const char *p = tile_plan; *p != '\0';) {
                int at = atoi(p);
                const char *colon = strchr(p, ':');
                if (colon == NULL) break;
                if (frame >= at) tiles = atoi(colon + 1);
                p = strchr(colon, ',');
                if (p == NULL) break;
                ++p;
            }
            for (int i = 0; i < tiles; ++i) {
                TNDrawTextureProEvent tile;
                memset(&tile, 0, sizeof(tile));
                tile.texture.id = 9998;
                tile.texture.width = 126;
                tile.texture.height = 66;
                tile.source.width = 21;
                tile.source.height = 66;
                tile.destination.width = 21;
                tile.destination.height = 66;
                draw_callback(&tile);
            }
        }        draw_callback(&lower);
        TNColor red = {255, 0, 0, 255};
        if (getenv("SMOKE_SKIN_UPPER") != NULL) {
            /* the skin's own upper background: pieces drawn after the lower background */
            DrawRectangle(100, 50, 300, 150, red);
        }
        {
            /* the lane panel, the first wide draw below the upper band */
            TNDrawTextureProEvent lane;
            memset(&lane, 0, sizeof(lane));
            lane.texture.id = 9997;
            lane.texture.width = 1426;
            lane.texture.height = 264;
            lane.destination.x = 497.0f;
            lane.destination.y = 276.0f;
            lane.destination.width = 1426.0f;
            lane.destination.height = 264.0f;
            draw_callback(&lane);
        }
        DrawRectangle(10, 10, 60, 60, red);
        EndTextureMode();
        EndDrawing();
    }
    RlImage image = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&image);
    printf("export %s -> %d\n", argv[3], ExportImage(image, argv[3]));
    CloseWindow();
    return 0;
}
