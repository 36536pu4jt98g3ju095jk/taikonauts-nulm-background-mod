#include <stdio.h>
#include <stdlib.h>
#include "../src/nulm.h"

/* Prints the structure of a .nulm so a root track can be chosen. */
int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: inspect file.nulm\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) return 2;
    fclose(f);
    char error[128];
    NulmFile *n = nulm_load(data, (size_t)size, error, sizeof error);
    if (!n) { fprintf(stderr, "load failed: %s\n", error); return 1; }
    printf("%s fps=%.1f size=%.0fx%.0f shapes=%d tracks=%d masks=%d\n", argv[1], n->fps,
           n->width, n->height, n->shape_count, n->track_count, n->mask_count);
    int max_atlas = -1;
    for (int s = 0; s < n->shape_count; ++s)
        for (int g = 0; g < n->shapes[s].graphic_count; ++g)
            if (n->shapes[s].graphics[g].fill_type == 0x41 && n->shapes[s].graphics[g].atlas_id > max_atlas)
                max_atlas = n->shapes[s].graphics[g].atlas_id;
    printf("  atlases used: 0..%d\n", max_atlas);
    for (int t = 0; t < n->track_count; ++t) {
        const NulmTrack *k = &n->tracks[t];
        if (k->referenced) continue;
        printf("  root %d frames=%d labels:", k->character_id, k->frame_count);
        for (int l = 0; l < k->label_count; ++l) printf(" %s@%d", k->labels[l].name, k->labels[l].frame);
        printf("\n");
    }
    nulm_free(n);
    free(data);
    return 0;
}
