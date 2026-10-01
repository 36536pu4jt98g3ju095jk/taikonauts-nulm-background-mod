#include "nulm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NESTING 32

typedef struct Reader {
    const unsigned char *data;
    size_t size;
} Reader;

static unsigned int rd_u32(const Reader *r, size_t off)
{
    unsigned int v = 0;
    if (off + 4 <= r->size) memcpy(&v, r->data + off, 4);
    return v;
}

static int rd_i32(const Reader *r, size_t off) { return (int)rd_u32(r, off); }

static unsigned int rd_u16(const Reader *r, size_t off)
{
    unsigned short v = 0;
    if (off + 2 <= r->size) memcpy(&v, r->data + off, 2);
    return v;
}

static float rd_f32(const Reader *r, size_t off)
{
    float v = 0.0f;
    if (off + 4 <= r->size) memcpy(&v, r->data + off, 4);
    return v;
}

static void *grow(void *items, int *capacity, int count, size_t item_size)
{
    if (count < *capacity) return items;
    int next = *capacity == 0 ? 8 : *capacity * 2;
    void *grown = realloc(items, (size_t)next * item_size);
    if (grown == NULL) return NULL;
    *capacity = next;
    return grown;
}

/* ---------------------------------------------------------------- parsing */

typedef struct RawLabel {
    int name_id;
    int target;
} RawLabel;

typedef struct RawFrame {
    int tag;
    NulmFrame frame;
    int goto_symbol;
    int action_ids_count;
    int *action_ids;
} RawFrame;

typedef struct ActionDef {
    int stop;
    int goto_symbol;
} ActionDef;

static char *dup_range(const unsigned char *data, size_t length)
{
    char *s = (char *)malloc(length + 1);
    if (s == NULL) return NULL;
    memcpy(s, data, length);
    s[length] = '\0';
    return s;
}

static int parse_symbols(NulmFile *file, const Reader *r, size_t body, size_t body_size)
{
    int count = rd_i32(r, body);
    if (count < 0 || count > 1000000) return 0;
    file->symbols = (char **)calloc((size_t)count + 1, sizeof(char *));
    if (file->symbols == NULL) return 0;
    size_t p = body + 4;
    size_t end = body + body_size;
    for (int i = 0; i < count; ++i) {
        if (p + 4 > end) break;
        size_t length = rd_u32(r, p);
        p += 4;
        size_t raw = p;
        p += ((length + 1 + 3) / 4) * 4;
        if (raw + length > r->size) break;
        size_t len = 0;
        while (len < length && r->data[raw + len] != 0) ++len;
        file->symbols[i] = dup_range(r->data + raw, len);
        file->symbol_count = i + 1;
    }
    return 1;
}

static ActionDef *parse_actions(const Reader *r, size_t body, size_t body_size, int *out_count)
{
    *out_count = 0;
    size_t end = body + body_size;
    if (body + 4 > end) return NULL;
    int count = rd_i32(r, body);
    if (count <= 0 || count > 100000) return NULL;
    ActionDef *defs = (ActionDef *)calloc((size_t)count, sizeof(ActionDef));
    if (defs == NULL) return NULL;
    size_t p = body + 4;
    int parsed = 0;
    for (int i = 0; i < count; ++i) {
        if (p + 4 > end) break;
        int length = rd_i32(r, p);
        p += 4;
        if (length < 0 || p + (size_t)length > end) break;
        size_t record_end = p + (size_t)length;
        defs[i].goto_symbol = -1;
        size_t q = p;
        while (q < record_end) {
            unsigned char code = r->data[q++];
            if (code == 0x00) break;
            if (code == 0x07) { defs[i].stop = 1; continue; }
            if (code >= 0x80) {
                if (q + 2 > record_end) break;
                int data_length = (int)rd_u16(r, q);
                q += 2;
                if (code == 0x8C && data_length == 2 && q + 2 <= record_end)
                    defs[i].goto_symbol = (int)rd_u16(r, q);
                q += (size_t)data_length;
            }
        }
        p = record_end;
        size_t misalign = (p - body) % 4;
        if (misalign != 0) p += 4 - misalign;
        parsed = i + 1;
    }
    *out_count = parsed;
    return defs;
}

static int parse_shape(NulmFile *file, const Reader *r, size_t body, size_t *next)
{
    int character_id = rd_i32(r, body + 0);
    int graphic_total = rd_i32(r, body + 16);
    size_t p = body + 20;
    if (graphic_total < 0 || graphic_total > 4096) return 0;

    NulmShape shape;
    memset(&shape, 0, sizeof(shape));
    shape.character_id = character_id;
    shape.graphics = (NulmGraphic *)calloc((size_t)graphic_total + 1, sizeof(NulmGraphic));
    if (shape.graphics == NULL) return 0;

    for (int g = 0; g < graphic_total; ++g) {
        if (p + 20 > r->size || rd_u32(r, p) != 0xF024) break;
        p += 8;
        NulmGraphic *graphic = &shape.graphics[shape.graphic_count];
        graphic->atlas_id = rd_i32(r, p);
        graphic->fill_type = (int)rd_u16(r, p + 4);
        int vertex_count = (short)rd_u16(r, p + 6);
        int index_count = rd_i32(r, p + 8);
        p += 12;
        if (vertex_count < 0 || index_count < 0 ||
            p + (size_t)vertex_count * 16 + (size_t)index_count * 2 > r->size) break;
        graphic->vertex_count = vertex_count;
        graphic->index_count = index_count;
        graphic->vertices = (float *)malloc((size_t)vertex_count * 4 * sizeof(float) + 4);
        graphic->indices = (unsigned short *)malloc((size_t)index_count * sizeof(unsigned short) + 2);
        if (graphic->vertices == NULL || graphic->indices == NULL) return 0;
        for (int v = 0; v < vertex_count * 4; ++v)
            graphic->vertices[v] = rd_f32(r, p + (size_t)v * 4);
        p += (size_t)vertex_count * 16;
        for (int i = 0; i < index_count; ++i)
            graphic->indices[i] = (unsigned short)rd_u16(r, p + (size_t)i * 2);
        p += (size_t)index_count * 2;
        if (index_count % 2 != 0) p += 2;
        ++shape.graphic_count;
    }

    NulmShape *grown = (NulmShape *)realloc(file->shapes,
                                            ((size_t)file->shape_count + 1) * sizeof(NulmShape));
    if (grown == NULL) return 0;
    file->shapes = grown;
    file->shapes[file->shape_count++] = shape;
    *next = p;
    return 1;
}

static int parse_frame_container(const Reader *r, size_t off, RawFrame *out, size_t *next,
                                 const ActionDef *actions, int action_count, int *mask_count)
{
    unsigned int tag = rd_u32(r, off);
    size_t body = off + 8;
    size_t body_size = (size_t)rd_u32(r, off + 4) * 4;
    int item_count = rd_i32(r, body + 4);
    size_t p = body + body_size;
    int place_capacity = 0, remove_capacity = 0;

    memset(out, 0, sizeof(*out));
    out->tag = (int)tag;
    out->goto_symbol = -1;

    for (int i = 0; i < item_count; ++i) {
        if (p + 8 > r->size) break;
        unsigned int sub = rd_u32(r, p);
        size_t sub_body = p + 8;
        size_t sub_next = sub_body + (size_t)rd_u32(r, p + 4) * 4;
        if (sub == 0x0004) {
            NulmPlace place;
            memset(&place, 0, sizeof(place));
            int place_flag = (int)rd_u16(r, sub_body + 16);
            place.has_character = (place_flag & 1) != 0;
            place.character_id = rd_i32(r, sub_body + 0);
            place.blend_mode = (int)rd_u16(r, sub_body + 18);
            place.depth = (int)rd_u16(r, sub_body + 20);
            place.clip_depth = (int)rd_u16(r, sub_body + 22);
            place.name_id = rd_i32(r, sub_body + 12);
            place.position_id = (int)rd_u16(r, sub_body + 28);
            place.position_flags = (int)rd_u16(r, sub_body + 30);
            place.color_mul_id = rd_i32(r, sub_body + 32);
            place.color_add_id = rd_i32(r, sub_body + 36);
            int has_color_matrix = rd_i32(r, sub_body + 40);
            int has_unknown = rd_i32(r, sub_body + 44);
            p = sub_next;
            if (has_color_matrix != 0) p = p + 8 + (size_t)rd_u32(r, p + 4) * 4;
            if (has_unknown != 0) p = p + 8 + (size_t)rd_u32(r, p + 4) * 4;
            if (place.clip_depth > place.depth && place.has_character) ++*mask_count;
            NulmPlace *grown = (NulmPlace *)grow(out->frame.places, &place_capacity,
                                                 out->frame.place_count, sizeof(NulmPlace));
            if (grown == NULL) return 0;
            out->frame.places = grown;
            out->frame.places[out->frame.place_count++] = place;
        } else if (sub == 0x0005) {
            int depth = (int)rd_u16(r, sub_body + 4);
            int *grown = (int *)grow(out->frame.removes, &remove_capacity,
                                     out->frame.remove_count, sizeof(int));
            if (grown == NULL) return 0;
            out->frame.removes = grown;
            out->frame.removes[out->frame.remove_count++] = depth;
            p = sub_next;
        } else if (sub == 0x000C) {
            int action_id = rd_i32(r, sub_body + 0);
            if (actions != NULL && action_id >= 0 && action_id < action_count) {
                if (actions[action_id].stop) out->frame.stop = 1;
                if (actions[action_id].goto_symbol >= 0)
                    out->goto_symbol = actions[action_id].goto_symbol;
            } else if (action_id == 0) {
                out->frame.stop = 1;
            }
            p = sub_next;
        } else {
            if (sub_next <= p) break;
            p = sub_next;
        }
    }
    *next = p;
    return 1;
}

/* Raw frame index (counting 0xF105 frames) -> index among playable frames. */
static int resolve_compiled_index(const int *raw_to_compiled, int raw_count, int raw_target)
{
    if (raw_count <= 0) return 0;
    if (raw_target >= raw_count) raw_target = raw_count - 1;
    for (int raw = raw_target; raw >= 0; --raw)
        if (raw_to_compiled[raw] >= 0) return raw_to_compiled[raw];
    return 0;
}

static int parse_track(NulmFile *file, const Reader *r, size_t off, size_t *next,
                       const ActionDef *actions, int action_count)
{
    size_t body = off + 8;
    size_t body_size = (size_t)rd_u32(r, off + 4) * 4;
    int sprite_id = rd_i32(r, body + 0);
    int label_total = rd_i32(r, body + 12);
    int frame_total = rd_i32(r, body + 16);
    int key_total = rd_i32(r, body + 20);
    size_t p = body + body_size;
    int frame_target = frame_total + key_total;
    int guard = 0;
    int max_iterations = (label_total + frame_total + key_total) * 4 + 16;
    int frames_parsed = 0, labels_parsed = 0;

    if (label_total < 0 || frame_total < 0 || key_total < 0 || frame_target > 1000000)
        return 0;

    RawFrame *raw_frames = (RawFrame *)calloc((size_t)frame_target + 1, sizeof(RawFrame));
    RawLabel *raw_labels = (RawLabel *)calloc((size_t)label_total + 1, sizeof(RawLabel));
    if (raw_frames == NULL || raw_labels == NULL) return 0;

    while ((frames_parsed < frame_target || labels_parsed < label_total) &&
           guard < max_iterations) {
        ++guard;
        if (p + 8 > r->size) break;
        unsigned int type = rd_u32(r, p);
        if (type == 0x0001 || type == 0xF105) {
            if (frames_parsed >= frame_target) break;
            if (!parse_frame_container(r, p, &raw_frames[frames_parsed], &p, actions,
                                       action_count, &file->mask_count)) return 0;
            ++frames_parsed;
        } else if (type == 0x002B) {
            if (labels_parsed >= label_total) break;
            raw_labels[labels_parsed].name_id = rd_i32(r, p + 8 + 0);
            raw_labels[labels_parsed].target = rd_i32(r, p + 8 + 4);
            p = p + 8 + (size_t)rd_u32(r, p + 4) * 4;
            ++labels_parsed;
        } else {
            size_t unknown_next = p + 8 + (size_t)rd_u32(r, p + 4) * 4;
            if (unknown_next <= p) break;
            p = unknown_next;
        }
    }

    NulmTrack track;
    memset(&track, 0, sizeof(track));
    track.character_id = sprite_id;
    track.frames = (NulmFrame *)calloc((size_t)frames_parsed + 1, sizeof(NulmFrame));
    int *raw_to_compiled = (int *)malloc(((size_t)frames_parsed + 1) * sizeof(int));
    if (track.frames == NULL || raw_to_compiled == NULL) return 0;

    for (int i = 0; i < frames_parsed; ++i) {
        if (raw_frames[i].tag == 0xF105) {
            raw_to_compiled[i] = -1;
            free(raw_frames[i].frame.places);
            free(raw_frames[i].frame.removes);
            continue;
        }
        raw_to_compiled[i] = track.frame_count;
        NulmFrame frame = raw_frames[i].frame;
        if (raw_frames[i].goto_symbol >= 0 && raw_frames[i].goto_symbol < file->symbol_count)
            frame.goto_label = file->symbols[raw_frames[i].goto_symbol];
        track.frames[track.frame_count++] = frame;
    }

    track.labels = (NulmLabel *)calloc((size_t)labels_parsed + 1, sizeof(NulmLabel));
    if (track.labels == NULL) return 0;
    for (int i = 0; i < labels_parsed; ++i) {
        int id = raw_labels[i].name_id;
        if (id < 0 || id >= file->symbol_count || file->symbols[id] == NULL ||
            file->symbols[id][0] == '\0') continue;
        track.labels[track.label_count].name = file->symbols[id];
        track.labels[track.label_count].frame =
            resolve_compiled_index(raw_to_compiled, frames_parsed, raw_labels[i].target);
        ++track.label_count;
    }

    free(raw_to_compiled);
    free(raw_frames);
    free(raw_labels);

    NulmTrack *grown = (NulmTrack *)realloc(file->tracks,
                                            ((size_t)file->track_count + 1) * sizeof(NulmTrack));
    if (grown == NULL) return 0;
    file->tracks = grown;
    file->tracks[file->track_count++] = track;
    *next = p;
    return 1;
}

NulmFile *nulm_load(const unsigned char *data, size_t size, char *error, size_t error_size)
{
    Reader reader = {data, size};
    ActionDef *actions = NULL;
    int action_count = 0;
    if (size < 0x40 || data[0] != 'L' || data[1] != 'M' || data[2] != 'B' || data[3] != 0) {
        if (error != NULL) snprintf(error, error_size, "not an LMB/.nulm file");
        return NULL;
    }
    NulmFile *file = (NulmFile *)calloc(1, sizeof(NulmFile));
    if (file == NULL) return NULL;
    file->fps = 30.0f;

    size_t off = 0x40;
    while (off + 8 <= size) {
        unsigned int tag = rd_u32(&reader, off);
        size_t body = off + 8;
        size_t body_size = (size_t)rd_u32(&reader, off + 4) * 4;
        if (body + body_size > size && tag != 0xF022 && tag != 0x0027) break;
        size_t next = body + body_size;

        if (tag == 0xF001) {
            if (!parse_symbols(file, &reader, body, body_size)) goto fail;
        } else if (tag == 0xF002) {
            int count = rd_i32(&reader, body);
            if (count < 0 || count > 1000000) goto fail;
            file->colors = (NulmColorEntry *)calloc((size_t)count + 1, sizeof(NulmColorEntry));
            if (file->colors == NULL) goto fail;
            for (int i = 0; i < count; ++i) {
                size_t b = body + 4 + (size_t)i * 8;
                file->colors[i].r = (int)rd_u16(&reader, b);
                file->colors[i].g = (int)rd_u16(&reader, b + 2);
                file->colors[i].b = (int)rd_u16(&reader, b + 4);
                file->colors[i].a = (int)rd_u16(&reader, b + 6);
            }
            file->color_count = count;
        } else if (tag == 0xF00C) {
            float fps = rd_f32(&reader, body + 28);
            file->fps = fps > 0.0f ? fps : 30.0f;
            file->width = rd_f32(&reader, body + 32);
            file->height = rd_f32(&reader, body + 36);
        } else if (tag == 0xF003) {
            int count = rd_i32(&reader, body);
            if (count < 0 || count > 1000000) goto fail;
            file->transforms = (NulmMat *)calloc((size_t)count + 1, sizeof(NulmMat));
            if (file->transforms == NULL) goto fail;
            for (int i = 0; i < count; ++i) {
                size_t b = body + 4 + (size_t)i * 24;
                file->transforms[i].a = rd_f32(&reader, b);
                file->transforms[i].b = rd_f32(&reader, b + 4);
                file->transforms[i].c = rd_f32(&reader, b + 8);
                file->transforms[i].d = rd_f32(&reader, b + 12);
                file->transforms[i].tx = rd_f32(&reader, b + 16);
                file->transforms[i].ty = rd_f32(&reader, b + 20);
            }
            file->transform_count = count;
        } else if (tag == 0xF103) {
            int count = rd_i32(&reader, body);
            if (count < 0 || count > 1000000) goto fail;
            file->positions = (float *)calloc((size_t)count * 2 + 2, sizeof(float));
            if (file->positions == NULL) goto fail;
            for (int i = 0; i < count; ++i) {
                size_t b = body + 4 + (size_t)i * 8;
                file->positions[i * 2] = rd_f32(&reader, b);
                file->positions[i * 2 + 1] = rd_f32(&reader, b + 4);
            }
            file->position_count = count;
        } else if (tag == 0xF005) {
            free(actions);
            actions = parse_actions(&reader, body, body_size, &action_count);
        } else if (tag == 0xF022) {
            if (!parse_shape(file, &reader, body, &next)) goto fail;
        } else if (tag == 0x0027) {
            if (!parse_track(file, &reader, off, &next, actions, action_count)) goto fail;
        }
        if (next <= off) break;
        off = next;
    }

    /* A track placed by another track is a child; the rest are root candidates. */
    for (int t = 0; t < file->track_count; ++t) {
        const NulmTrack *track = &file->tracks[t];
        for (int f = 0; f < track->frame_count; ++f) {
            for (int p = 0; p < track->frames[f].place_count; ++p) {
                const NulmPlace *place = &track->frames[f].places[p];
                if (!place->has_character) continue;
                for (int c = 0; c < file->track_count; ++c)
                    if (file->tracks[c].character_id == place->character_id)
                        file->tracks[c].referenced = 1;
            }
        }
    }

    free(actions);
    return file;

fail:
    free(actions);
    if (error != NULL) snprintf(error, error_size, "malformed or truncated NULM data");
    nulm_free(file);
    return NULL;
}

void nulm_free(NulmFile *file)
{
    if (file == NULL) return;
    for (int i = 0; i < file->symbol_count; ++i) free(file->symbols[i]);
    free(file->symbols);
    free(file->colors);
    free(file->transforms);
    free(file->positions);
    for (int s = 0; s < file->shape_count; ++s) {
        for (int g = 0; g < file->shapes[s].graphic_count; ++g) {
            free(file->shapes[s].graphics[g].vertices);
            free(file->shapes[s].graphics[g].indices);
        }
        free(file->shapes[s].graphics);
    }
    free(file->shapes);
    for (int t = 0; t < file->track_count; ++t) {
        for (int f = 0; f < file->tracks[t].frame_count; ++f) {
            free(file->tracks[t].frames[f].places);
            free(file->tracks[t].frames[f].removes);
        }
        free(file->tracks[t].frames);
        free(file->tracks[t].labels);
    }
    free(file->tracks);
    free(file);
}

const NulmTrack *nulm_find_track(const NulmFile *file, int character_id)
{
    for (int i = 0; i < file->track_count; ++i)
        if (file->tracks[i].character_id == character_id) return &file->tracks[i];
    return NULL;
}

static const NulmShape *find_shape(const NulmFile *file, int character_id)
{
    for (int i = 0; i < file->shape_count; ++i)
        if (file->shapes[i].character_id == character_id) return &file->shapes[i];
    return NULL;
}

/* ---------------------------------------------------------------- playback */

typedef struct DisplayEntry {
    int depth;
    int character_id;
    NulmMat matrix;
    NulmColor color;
    int blend_mode;
    int clip_depth;
    const char *name;
    NulmClip *child;
} DisplayEntry;

struct NulmClip {
    const NulmFile *file;
    const NulmTrack *track;
    int current_frame;
    double timer;
    int stopped;
    DisplayEntry *entries;
    int entry_count;
    int entry_capacity;
};

static const NulmMat identity_matrix = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
static const NulmColor identity_color = {1, 1, 1, 1, 0, 0, 0, 0};

static NulmMat mat_compose(NulmMat first, NulmMat second)
{
    NulmMat m;
    m.a = first.a * second.a + first.b * second.c;
    m.b = first.a * second.b + first.b * second.d;
    m.c = first.c * second.a + first.d * second.c;
    m.d = first.c * second.b + first.d * second.d;
    m.tx = first.tx * second.a + first.ty * second.c + second.tx;
    m.ty = first.tx * second.b + first.ty * second.d + second.ty;
    return m;
}

static NulmColor color_compose(NulmColor child, NulmColor parent)
{
    NulmColor c;
    c.mul_r = child.mul_r * parent.mul_r;
    c.mul_g = child.mul_g * parent.mul_g;
    c.mul_b = child.mul_b * parent.mul_b;
    c.mul_a = child.mul_a * parent.mul_a;
    c.add_r = child.add_r * parent.mul_r + parent.add_r;
    c.add_g = child.add_g * parent.mul_g + parent.add_g;
    c.add_b = child.add_b * parent.mul_b + parent.add_b;
    c.add_a = child.add_a * parent.mul_a + parent.add_a;
    return c;
}

static NulmClip *clip_new(const NulmFile *file, const NulmTrack *track)
{
    NulmClip *clip = (NulmClip *)calloc(1, sizeof(NulmClip));
    if (clip == NULL) return NULL;
    clip->file = file;
    clip->track = track;
    return clip;
}

static void clip_clear_entries(NulmClip *clip)
{
    for (int i = 0; i < clip->entry_count; ++i) nulm_clip_free(clip->entries[i].child);
    clip->entry_count = 0;
}

void nulm_clip_free(NulmClip *clip)
{
    if (clip == NULL) return;
    clip_clear_entries(clip);
    free(clip->entries);
    free(clip);
}

static int entry_index(const NulmClip *clip, int depth)
{
    for (int i = 0; i < clip->entry_count; ++i)
        if (clip->entries[i].depth == depth) return i;
    return -1;
}

static DisplayEntry *entry_insert(NulmClip *clip, int depth)
{
    DisplayEntry *grown = (DisplayEntry *)grow(clip->entries, &clip->entry_capacity,
                                               clip->entry_count, sizeof(DisplayEntry));
    if (grown == NULL) return NULL;
    clip->entries = grown;
    int at = clip->entry_count;
    while (at > 0 && clip->entries[at - 1].depth > depth) {
        clip->entries[at] = clip->entries[at - 1];
        --at;
    }
    memset(&clip->entries[at], 0, sizeof(DisplayEntry));
    clip->entries[at].depth = depth;
    clip->entries[at].matrix = identity_matrix;
    clip->entries[at].color = identity_color;
    ++clip->entry_count;
    return &clip->entries[at];
}

static void entry_remove(NulmClip *clip, int depth)
{
    int at = entry_index(clip, depth);
    if (at < 0) return;
    nulm_clip_free(clip->entries[at].child);
    for (int i = at; i + 1 < clip->entry_count; ++i) clip->entries[i] = clip->entries[i + 1];
    --clip->entry_count;
}

static int resolve_matrix(const NulmFile *file, const NulmPlace *place, NulmMat *out)
{
    int pid = place->position_id;
    int flags = place->position_flags;
    if (pid == 0xFFFF && flags == 0xFFFF) return 0;
    if (flags == 0x8000) {
        *out = identity_matrix;
        if (pid >= 0 && pid < file->position_count) {
            out->tx = file->positions[pid * 2];
            out->ty = file->positions[pid * 2 + 1];
        }
        return 1;
    }
    if (flags == 0x0000) {
        *out = (pid >= 0 && pid < file->transform_count) ? file->transforms[pid] : identity_matrix;
        return 1;
    }
    if (pid >= 0 && pid < file->position_count) {
        *out = identity_matrix;
        out->tx = file->positions[pid * 2];
        out->ty = file->positions[pid * 2 + 1];
    } else if (pid >= 0 && pid < file->transform_count) {
        *out = file->transforms[pid];
    } else {
        *out = identity_matrix;
    }
    return 1;
}

static int resolve_color(const NulmFile *file, const NulmPlace *place, NulmColor *out)
{
    if (place->color_mul_id < 0 && place->color_add_id < 0) return 0;
    *out = identity_color;
    if (place->color_mul_id >= 0 && place->color_mul_id < file->color_count) {
        const NulmColorEntry *c = &file->colors[place->color_mul_id];
        out->mul_r = (float)(short)c->r / 256.0f;
        out->mul_g = (float)(short)c->g / 256.0f;
        out->mul_b = (float)(short)c->b / 256.0f;
        out->mul_a = (float)(short)c->a / 256.0f;
    }
    if (place->color_add_id >= 0 && place->color_add_id < file->color_count) {
        const NulmColorEntry *c = &file->colors[place->color_add_id];
        out->add_r = (float)(short)c->r / 256.0f;
        out->add_g = (float)(short)c->g / 256.0f;
        out->add_b = (float)(short)c->b / 256.0f;
        out->add_a = (float)(short)c->a / 256.0f;
    }
    return 1;
}


static NulmClip *clip_make_child(const NulmClip *parent, int character_id)
{
    const NulmTrack *track = nulm_find_track(parent->file, character_id);
    if (track == NULL) return NULL;
    return clip_new(parent->file, track);
}

static void clip_apply_frame(NulmClip *clip, int index, int nesting);

static void clip_apply_place(NulmClip *clip, const NulmPlace *place, int nesting)
{
    int at = entry_index(clip, place->depth);
    DisplayEntry *entry = at >= 0 ? &clip->entries[at] : entry_insert(clip, place->depth);
    if (entry == NULL) return;

    if (place->has_character) {
        int id = place->character_id;
        const NulmTrack *child_track = nulm_find_track(clip->file, id);
        int same = entry->character_id == id && (entry->child != NULL || child_track == NULL) &&
                   (at >= 0);
        if (!same) {
            nulm_clip_free(entry->child);
            entry->child = NULL;
            entry->character_id = id;
            if (child_track != NULL && nesting < MAX_NESTING) {
                entry->child = clip_make_child(clip, id);
                if (entry->child != NULL && entry->child->track->frame_count > 0)
                    clip_apply_frame(entry->child, 0, nesting + 1);
            }
        }
    }

    NulmMat matrix;
    if (resolve_matrix(clip->file, place, &matrix)) entry->matrix = matrix;
    NulmColor color;
    if (resolve_color(clip->file, place, &color)) entry->color = color;
    if (place->has_character) {
        entry->blend_mode = place->blend_mode;
        entry->clip_depth = place->clip_depth;
        entry->name = NULL;
        if (place->name_id >= 0 && place->name_id < clip->file->symbol_count &&
            clip->file->symbols[place->name_id] != NULL &&
            clip->file->symbols[place->name_id][0] != '\0')
            entry->name = clip->file->symbols[place->name_id];
    }
}

static void clip_apply_frame(NulmClip *clip, int index, int nesting)
{
    const NulmFrame *frame = &clip->track->frames[index];
    for (int i = 0; i < frame->remove_count; ++i) entry_remove(clip, frame->removes[i]);
    for (int i = 0; i < frame->place_count; ++i) clip_apply_place(clip, &frame->places[i], nesting);
    if (frame->stop) clip->stopped = 1;
}

static void clip_seek_preserving(NulmClip *clip, int frame_index, int preserve)
{
    if (clip->track->frame_count == 0) return;
    if (frame_index < 0) frame_index = 0;
    if (frame_index >= clip->track->frame_count) frame_index = clip->track->frame_count - 1;

    DisplayEntry *previous = NULL;
    int previous_count = 0;
    if (preserve && clip->entry_count > 0) {
        previous = (DisplayEntry *)malloc((size_t)clip->entry_count * sizeof(DisplayEntry));
        if (previous != NULL) {
            memcpy(previous, clip->entries, (size_t)clip->entry_count * sizeof(DisplayEntry));
            previous_count = clip->entry_count;
            /* ownership of the children moves to the saved copy */
            clip->entry_count = 0;
        }
    }
    clip_clear_entries(clip);
    clip->stopped = 0;
    for (int i = 0; i <= frame_index; ++i) clip_apply_frame(clip, i, 1);

    for (int i = 0; i < previous_count; ++i) {
        int at = entry_index(clip, previous[i].depth);
        if (at >= 0 && previous[i].child != NULL && clip->entries[at].child != NULL &&
            clip->entries[at].character_id == previous[i].character_id) {
            nulm_clip_free(clip->entries[at].child);
            clip->entries[at].child = previous[i].child;
            previous[i].child = NULL;
        }
    }
    for (int i = 0; i < previous_count; ++i) nulm_clip_free(previous[i].child);
    free(previous);

    clip->stopped = clip->track->frames[frame_index].stop;
    clip->current_frame = frame_index;
    clip->timer = 0.0;
}

static void clip_seek(NulmClip *clip, int frame_index)
{
    clip_seek_preserving(clip, frame_index, 0);
}
NulmClip *nulm_clip_create(const NulmFile *file, int track_id)
{
    const NulmTrack *track = nulm_find_track(file, track_id);
    if (track == NULL) return NULL;
    NulmClip *clip = clip_new(file, track);
    if (clip == NULL) return NULL;
    if (track->frame_count > 0) clip_apply_frame(clip, 0, 1);
    return clip;
}

void nulm_clip_play(NulmClip *clip)
{
    if (clip == NULL) return;
    clip_seek(clip, 0);
    clip->stopped = 0;
}

static int frame_places_depth(const NulmFrame *frame, int depth)
{
    for (int i = 0; i < frame->place_count; ++i)
        if (frame->places[i].depth == depth) return 1;
    return 0;
}

static int find_label(const NulmTrack *track, const char *name)
{
    for (int i = 0; i < track->label_count; ++i)
        if (strcmp(track->labels[i].name, name) == 0) return track->labels[i].frame;
    return -1;
}

void nulm_clip_update(NulmClip *clip, double seconds, int loop)
{
    if (clip == NULL) return;
    float fps = clip->file->fps;
    if (!clip->stopped && clip->track->frame_count > 1 && fps > 0.0f) {
        double duration = 1.0 / fps;
        clip->timer += seconds;
        int guard = 0;
        while (clip->timer >= duration && !clip->stopped && guard < 10000) {
            clip->timer -= duration;
            int next = clip->current_frame + 1;
            int wrapped = 0;
            if (next >= clip->track->frame_count) {
                if (!loop) {
                    clip->stopped = 1;
                    clip->timer = 0.0;
                    break;
                }
                next = 0;
                wrapped = 1;
            }
            if (wrapped) {
                for (int i = clip->entry_count - 1; i >= 0; --i)
                    if (!frame_places_depth(&clip->track->frames[0], clip->entries[i].depth))
                        entry_remove(clip, clip->entries[i].depth);
            }
            clip->current_frame = next;
            clip_apply_frame(clip, next, 1);

            const NulmFrame *frame = &clip->track->frames[next];
            if (frame->goto_label != NULL) {
                int target = find_label(clip->track, frame->goto_label);
                if (target >= 0) {
                    int stop_after = frame->stop;
                    double remaining = clip->timer;
                    clip_seek(clip, target);
                    clip->timer = remaining;
                    if (stop_after) clip->stopped = 1;
                    if (clip->stopped) break;
                }
            }
            ++guard;
        }
    }
    for (int i = 0; i < clip->entry_count; ++i)
        nulm_clip_update(clip->entries[i].child, seconds, loop);
}

int nulm_clip_goto_label(NulmClip *clip, const char *label, int stop, int preserve)
{
    if (clip == NULL || label == NULL) return 0;
    int target = find_label(clip->track, label);
    if (target >= 0) {
        clip_seek_preserving(clip, target, preserve);
        clip->stopped = stop;
        return 1;
    }
    int any = 0;
    for (int i = 0; i < clip->entry_count; ++i)
        any |= nulm_clip_goto_label(clip->entries[i].child, label, stop, preserve);
    return any;
}

static NulmClip *find_child(NulmClip *clip, const char *name)
{
    for (int i = 0; i < clip->entry_count; ++i) {
        NulmClip *child = clip->entries[i].child;
        if (child == NULL) continue;
        if (clip->entries[i].name != NULL && strcmp(clip->entries[i].name, name) == 0) return child;
        NulmClip *found = find_child(child, name);
        if (found != NULL) return found;
    }
    return NULL;
}

int nulm_clip_seek_child(NulmClip *clip, const char *name, int frame, int stop)
{
    NulmClip *child = clip == NULL ? NULL : find_child(clip, name);
    if (child == NULL) return 0;
    clip_seek(child, frame);
    if (stop) child->stopped = 1;
    return 1;
}

int nulm_clip_play_child(NulmClip *clip, const char *name)
{
    NulmClip *child = clip == NULL ? NULL : find_child(clip, name);
    if (child == NULL) return 0;
    nulm_clip_play(child);
    return 1;
}

int nulm_clip_current_frame(const NulmClip *clip)
{
    return clip == NULL ? 0 : clip->current_frame;
}

static void clip_draw(const NulmClip *clip, NulmMat parent_world, NulmColor parent_color,
                      int parent_blend, const NulmDrawCallbacks *callbacks, int nesting)
{
    for (int i = 0; i < clip->entry_count; ++i) {
        const DisplayEntry *entry = &clip->entries[i];
        NulmMat world = mat_compose(entry->matrix, parent_world);
        NulmColor color = color_compose(entry->color, parent_color);
        int blend = entry->blend_mode > 2 ? entry->blend_mode : parent_blend;

        /* Clip masks are not reproduced: the mask shape itself is not drawn. */
        if (entry->clip_depth > entry->depth) continue;

        if (entry->child != NULL) {
            if (nesting < MAX_NESTING)
                clip_draw(entry->child, world, color, blend, callbacks, nesting + 1);
        } else {
            const NulmShape *shape = find_shape(clip->file, entry->character_id);
            if (shape == NULL) continue;
            for (int g = 0; g < shape->graphic_count; ++g)
                callbacks->draw_graphic(callbacks->user, clip->file, &shape->graphics[g],
                                        &world, &color, blend);
        }
    }
}

void nulm_clip_draw(const NulmClip *clip, float x, float y, float scale,
                    const NulmDrawCallbacks *callbacks)
{
    if (clip == NULL || callbacks == NULL || callbacks->draw_graphic == NULL) return;
    NulmMat stage = {scale, 0.0f, 0.0f, scale, x, y};
    clip_draw(clip, stage, identity_color, 0, callbacks, 0);
}
