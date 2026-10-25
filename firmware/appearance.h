#ifndef APPEARANCE_H
#define APPEARANCE_H
#include <stdint.h>
struct Level;
typedef struct {
    uint16_t background,light,dark,grid,pad;
    uint16_t fragile,fragile_edge,fragile_fall_light,fragile_fall_dark,barrier_front,barrier_side,barrier_top,solid_front,solid_side,solid_roof,solid_inset,trunk_front,trunk_side,trunk_top,leaf_front,leaf_side,leaf_top;
    uint16_t short_pad,air_front,air_side,air_top;
    uint8_t solid_model,backdrop,air_model,ground_model;
} Appearance;
#define ART_CACHE_ADDRESS 0x00006200u
#define ART_CACHE_BYTES 7680u
typedef struct {
    int16_t x,z,width,depth,bottom,top;
    uint8_t front,side,roof,flags;
} ArtBox;
typedef struct { uint16_t count,reserved; ArtBox boxes[4]; } ArtModel;
typedef struct {
    const uint16_t *palette,*offsets;
    const uint8_t *runs;
    unsigned height,bottom;
} ArtBackground;
extern volatile unsigned art_sd_loaded;
void appearance_load(int card);
const ArtModel *appearance_model(unsigned index);
const ArtBackground *appearance_background(void);
const Appearance *appearance_for(const struct Level *level,unsigned row);
#endif
