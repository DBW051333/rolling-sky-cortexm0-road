#ifndef PLAYER_H
#define PLAYER_H
#include <stdint.h>
#include "road.h"
#define PLAYER_COUNT 12u
#define BALL_LEFT ((ROAD_WIDTH-68)/2)
#define BALL_TOP 158
#define BALL_WIDTH 68
#define BALL_HEIGHT 68
void player_select(unsigned skin);
unsigned player_selected(void);
const char *player_name(void);
const uint16_t *player_colors(void);
/* One 68-pixel RLE row, run length in bits 7:3, palette index in 2:0.
 * Decorated spheres share a scratch row; every skin keeps the same alpha
 * silhouette and collision footprint. */
const uint8_t *player_row(unsigned y,unsigned *count);
#endif
