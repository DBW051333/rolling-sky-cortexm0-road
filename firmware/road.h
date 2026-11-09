#ifndef ROAD_H
#define ROAD_H
#include <stdint.h>
#define ROAD_COLS 5
#define ROAD_ROWS 16
#define ROAD_WIDTH 198
#define ROAD_HEIGHT 320
#define ROAD_FIRST_Y 0
#define ROAD_LINES ROAD_HEIGHT
/* Fixed pitched camera: sin=5/13, cos=12/13; focal=384, height=4,
 * principal point=(ROAD_WIDTH/2,56). One world tile is 256 Q8 units.
 * This gives 24 px/tile at top, 97.615 px/tile at bottom,
 * and 13.07 world rows across the screen height. */
#define ROAD_BG 0x0843u
#define ROAD_GRID 0x1926u
#define ROAD_LIGHT 0x357fu
#define ROAD_DARK 0x2456u
#define ROAD_POINT 0xffe0u
#define ROAD_CAMERA_MIN 128
#define ROAD_CAMERA_MAX 1152
#define ROAD_CAMERA_CENTER 640
#define ROAD_BALL_Y 192
#define ROAD_JUMP_MS 1366u   /* 0.75 Q8/ms x 1366 = 1024.5 Q8: the reference
                                four-row pad flight, landing a hair past the
                                take-off row plus four so chains re-trigger */
#define ROAD_SHORT_JUMP_MS 768u
/* Gem and finish-line markers are art-independent until skins catch up.
 * Gems are drawn as a four-tone diamond the size of the local tile, not as a
 * handful of pixels, so they stay readable at the far end of the track.
 * Colours sampled from the reference gameplay: coral-red diamonds. */
#define ROAD_GEM_LIGHT 0xf842u
#define ROAD_GEM_TOP 0xc020u
#define ROAD_GEM_MID 0xd000u
#define ROAD_GEM_LOW 0x8000u
#define ROAD_GEM_EDGE 0x0233u
#define ROAD_FINISH_A 0xffffu
#define ROAD_FINISH_B 0x0000u
/* The finishing gate. The reference ends a level by rolling into a ring
 * portal, not by crossing a painted stripe: a broad orange rim, one thin
 * bright-yellow band inside it, and a cyan disc glowing at the centre. */
#define ROAD_GATE_EDGE  0x8a83u   /* burnt outline on the outside of the rim */
#define ROAD_GATE_RIM   0xb3a5u   /* outer rim, the darker orange */
#define ROAD_GATE_RIM2  0xe509u   /* lit inner rim */
#define ROAD_GATE_BAND  0xf707u   /* the thin yellow ring */
#define ROAD_GATE_CORE  0xdfffu   /* cyan disc, dead centre: the glow */
#define ROAD_GATE_GLOW  0x7fbfu   /* cyan disc, next ring out */
#define ROAD_GATE_MEM_A 0x467eu   /* cyan disc, middle */
#define ROAD_GATE_MEM_B 0x14b9u   /* cyan disc, edge: deep teal */
struct Level;
/* Overlay hooks share the road's own scanline, so a HUD or a gem sprite
 * never becomes a second, visible write to the panel. `rows` is the lowest
 * scanline the overlay does not touch; below it the road streams directly. */
typedef struct {
    unsigned rows;
    void (*line)(uint16_t *pixels,unsigned y,void *context);
    void *context;
} RoadOverlay;
typedef struct {
    uint32_t row;
    int32_t center_x_q8;
    uint32_t center_z_q8;
    uint8_t column,present;
} RoadPoint;
typedef struct {
    uint32_t tags[ROAD_ROWS];
    uint8_t masks[ROAD_ROWS];
    uint16_t depth_q8[ROAD_HEIGHT];
    uint16_t scale_q8[ROAD_HEIGHT];
    int16_t center_y[ROAD_ROWS];
    uint16_t camera_x_q8;
    uint8_t forward_remainder;
    const struct Level *level;
    uint16_t jump_ms,jump_row,jump_total;
    uint16_t death_ms;
    uint8_t death_kind;
    uint32_t gem_bits;   /* collected gems, one bit per gem index */
    uint32_t mech_fired; /* mechanisms that already announced themselves */
    uint16_t gem_cursor,mech_cursor;
    uint16_t sfx;        /* pending SFX_* events, drained by main.c */
    uint8_t gems_got;
    const RoadOverlay *overlay;
} Road;
typedef struct {
    void (*begin_line)(unsigned x,unsigned y,unsigned pixels,void *context);
    void (*span)(unsigned pixels,uint16_t color,void *context);
    void *context;
} RoadSink;
/* Menu and gameplay execute sequentially and share this single scanline. */
uint16_t *road_scanline_buffer(void);
void road_init(Road *road);
void road_pan(Road *road,int direction,uint32_t elapsed_ms);
uint32_t road_forward(Road *road,uint32_t elapsed_ms);
void road_set_cell(Road *road,uint32_t row,unsigned column,int present);
unsigned road_mask(const Road *road,uint32_t row);
int road_query(const Road *road,int32_t x_q8,uint32_t z_q8,RoadPoint *point);
unsigned road_jump_height(const Road *road);
void road_draw(Road *road,uint32_t camera_z_q8,int show_points,const RoadSink *sink);
#ifdef ROAD_BENCHMARK
void road_draw_calculated(Road *road,uint32_t camera_z_q8,int show_points,const RoadSink *sink);
#endif
#endif
