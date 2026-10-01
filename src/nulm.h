#ifndef NULM_H
#define NULM_H

#include <stddef.h>

/* Self-contained NULM (LMB) reader and timeline player. It has no rendering
   dependency: the host receives each visible mesh through a callback. */

typedef struct NulmMat {
    float a, b, c, d, tx, ty;
} NulmMat;

typedef struct NulmColor {
    float mul_r, mul_g, mul_b, mul_a;
    float add_r, add_g, add_b, add_a;
} NulmColor;

typedef struct NulmGraphic {
    int atlas_id;
    int fill_type;
    int vertex_count;
    float *vertices; /* x, y, u, v per vertex */
    int index_count;
    unsigned short *indices;
} NulmGraphic;

typedef struct NulmShape {
    int character_id;
    int graphic_count;
    NulmGraphic *graphics;
} NulmShape;

typedef struct NulmPlace {
    int has_character;
    int character_id;
    int blend_mode;
    int depth;
    int clip_depth;
    int name_id;
    int position_id;
    int position_flags;
    int color_mul_id;
    int color_add_id;
} NulmPlace;

typedef struct NulmFrame {
    NulmPlace *places;
    int place_count;
    int *removes;
    int remove_count;
    int stop;
    const char *goto_label;
} NulmFrame;

typedef struct NulmLabel {
    const char *name;
    int frame;
} NulmLabel;

typedef struct NulmTrack {
    int character_id;
    NulmFrame *frames;
    int frame_count;
    NulmLabel *labels;
    int label_count;
    int referenced; /* placed by another track, so not a root candidate */
} NulmTrack;

typedef struct NulmColorEntry {
    int r, g, b, a;
} NulmColorEntry;

typedef struct NulmFile {
    char **symbols;
    int symbol_count;
    NulmColorEntry *colors;
    int color_count;
    NulmMat *transforms;
    int transform_count;
    float *positions; /* x, y pairs */
    int position_count;
    NulmShape *shapes;
    int shape_count;
    NulmTrack *tracks;
    int track_count;
    float fps;
    float width;
    float height;
    int mask_count; /* places that define a clip mask (not rendered) */
} NulmFile;

typedef struct NulmClip NulmClip;

typedef struct NulmDrawCallbacks {
    void *user;
    void (*draw_graphic)(void *user, const NulmFile *file, const NulmGraphic *graphic,
                         const NulmMat *world, const NulmColor *color, int blend_mode);
} NulmDrawCallbacks;

NulmFile *nulm_load(const unsigned char *data, size_t size, char *error, size_t error_size);
void nulm_free(NulmFile *file);
const NulmTrack *nulm_find_track(const NulmFile *file, int character_id);

/* Returns a new root clip for the given track, or NULL when it does not exist. */
NulmClip *nulm_clip_create(const NulmFile *file, int track_id);
void nulm_clip_free(NulmClip *clip);
void nulm_clip_play(NulmClip *clip);
void nulm_clip_update(NulmClip *clip, double seconds, int loop);
void nulm_clip_draw(const NulmClip *clip, float x, float y, float scale,
                    const NulmDrawCallbacks *callbacks);
/* Jumps to a label on the clip or, when it has none, on its descendants.
   preserve keeps the playback position of children that stay at the same depth. */
int nulm_clip_goto_label(NulmClip *clip, const char *label, int stop, int preserve);
/* Child clips are found recursively by their placement name. */
int nulm_clip_seek_child(NulmClip *clip, const char *name, int frame, int stop);
int nulm_clip_play_child(NulmClip *clip, const char *name);
int nulm_clip_current_frame(const NulmClip *clip);

#endif
