#ifndef TNMOD_API_H
#define TNMOD_API_H

#include <windows.h>
#include <stddef.h>

#define TNMOD_API_VERSION 1u
#define TNMOD_EXPORT __declspec(dllexport)

typedef struct TNVector2 {
    float x;
    float y;
} TNVector2;

typedef struct TNRectangle {
    float x;
    float y;
    float width;
    float height;
} TNRectangle;

typedef struct TNColor {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} TNColor;

typedef struct TNTexture2D {
    unsigned int id;
    int width;
    int height;
    int mipmaps;
    int format;
} TNTexture2D;

typedef struct TNFont {
    int baseSize;
    int glyphCount;
    int glyphPadding;
    TNTexture2D texture;
    void *recs;
    void *glyphs;
} TNFont;

typedef struct TNDrawTextEvent {
    const char *text;
    int x;
    int y;
    int fontSize;
    TNColor tint;
    int skipOriginal;
} TNDrawTextEvent;

typedef struct TNDrawTextExEvent {
    TNFont font;
    const char *text;
    TNVector2 position;
    TNVector2 origin;
    float rotation;
    float fontSize;
    float spacing;
    TNColor tint;
    int isPro;
    int skipOriginal;
} TNDrawTextExEvent;

typedef struct TNDrawTextureProEvent {
    TNTexture2D texture;
    TNRectangle source;
    TNRectangle destination;
    TNVector2 origin;
    float rotation;
    TNColor tint;
    int skipOriginal;
} TNDrawTextureProEvent;

typedef struct TNDrawTextureRecEvent {
    TNTexture2D texture;
    TNRectangle source;
    TNVector2 position;
    TNColor tint;
    int skipOriginal;
} TNDrawTextureRecEvent;

enum TNTextureDrawKind {
    TN_TEXTURE_DRAW_BASIC = 0,
    TN_TEXTURE_DRAW_VECTOR = 1,
    TN_TEXTURE_DRAW_EXTENDED = 2
};

typedef struct TNDrawTextureEvent {
    TNTexture2D texture;
    TNVector2 position;
    float rotation;
    float scale;
    TNColor tint;
    int kind;
    int skipOriginal;
} TNDrawTextureEvent;

typedef struct TNKeyPressedEvent {
    int key;
    int originalResult;
    int result;
    const void *callerAddress;
    int queryType;
} TNKeyPressedEvent;

enum TNKeyQueryType {
    TN_KEY_QUERY_PRESSED = 0,
    TN_KEY_QUERY_PRESSED_REPEAT = 1,
    TN_KEY_QUERY_DOWN = 2,
    TN_KEY_QUERY_RELEASED = 3,
    TN_KEY_QUERY_GET_PRESSED = 4
};

typedef struct TNWindowShouldCloseEvent {
    int originalResult;
    int result;
} TNWindowShouldCloseEvent;

typedef struct TNLoadTextureEvent {
    const char *fileName;
    TNTexture2D texture;
} TNLoadTextureEvent;

typedef struct TNUnloadTextureEvent {
    TNTexture2D texture;
} TNUnloadTextureEvent;

typedef void (__cdecl *TNFrameCallback)(void);
typedef void (__cdecl *TNDrawTextCallback)(TNDrawTextEvent *event);
typedef void (__cdecl *TNDrawTextExCallback)(TNDrawTextExEvent *event);
typedef void (__cdecl *TNDrawTextureProCallback)(TNDrawTextureProEvent *event);
typedef void (__cdecl *TNDrawTextureRecCallback)(TNDrawTextureRecEvent *event);
typedef void (__cdecl *TNDrawTextureCallback)(TNDrawTextureEvent *event);
typedef void (__cdecl *TNKeyPressedCallback)(TNKeyPressedEvent *event);
typedef void (__cdecl *TNWindowShouldCloseCallback)(TNWindowShouldCloseEvent *event);
typedef void (__cdecl *TNLoadTextureCallback)(const TNLoadTextureEvent *event);
typedef void (__cdecl *TNUnloadTextureCallback)(const TNUnloadTextureEvent *event);

#define TNMOD_INPUT_API_NAME "tnmod.input"
#define TNMOD_INPUT_API_VERSION 1u
#define TNMOD_TEXTURE_LIFECYCLE_API_NAME "tnmod.texture-lifecycle"
#define TNMOD_TEXTURE_LIFECYCLE_API_VERSION 1u
#define TNMOD_TEXTURE_DRAW_API_NAME "tnmod.texture-draw"
#define TNMOD_TEXTURE_DRAW_API_VERSION 1u

typedef struct TNModInputApiV1 {
    unsigned int version;
    size_t size;
    int (__cdecl *registerKeyQuery)(TNKeyPressedCallback callback);
    int (__cdecl *registerWindowShouldClose)(TNWindowShouldCloseCallback callback);
} TNModInputApiV1;

typedef struct TNModTextureLifecycleApiV1 {
    unsigned int version;
    size_t size;
    int (__cdecl *registerLoadTexture)(TNLoadTextureCallback callback);
    int (__cdecl *registerUnloadTexture)(TNUnloadTextureCallback callback);
} TNModTextureLifecycleApiV1;

typedef struct TNModTextureDrawApiV1 {
    unsigned int version;
    size_t size;
    int (__cdecl *registerDrawTexture)(TNDrawTextureCallback callback);
} TNModTextureDrawApiV1;

typedef struct TNModApi {
    unsigned int version;
    size_t size;
    FARPROC (__cdecl *getOriginalProc)(const char *name);
    const char *(__cdecl *getGameDirectory)(void);
    void (__cdecl *log)(const char *modName, const char *message);
    int (__cdecl *registerBeginFrame)(TNFrameCallback callback);
    int (__cdecl *registerEndFrame)(TNFrameCallback callback);
    int (__cdecl *registerDrawText)(TNDrawTextCallback callback);
    int (__cdecl *registerDrawTextEx)(TNDrawTextExCallback callback);
    int (__cdecl *registerDrawTexturePro)(TNDrawTextureProCallback callback);
    int (__cdecl *registerDrawTextureRec)(TNDrawTextureRecCallback callback);
    const void *(__cdecl *queryExtension)(const char *name, unsigned int version);
} TNModApi;

typedef int (__cdecl *TNModInitFn)(const TNModApi *api);
typedef void (__cdecl *TNModShutdownFn)(void);

#endif
