#ifndef PLAYER_H
#define PLAYER_H
#include <stdint.h>
#include "road.h"
#define PLAYER_COUNT 12u
#define BALL_LEFT ((ROAD_WIDTH-62)/2)
#define BALL_TOP 350
#define BALL_WIDTH 62
#define BALL_HEIGHT 68
/* Gameplay is about 94% of one floor tile wide; preview uses source size.
 * Cumulative 15/8 run scaling avoids gaps from rounding each run separately. */
#define GAME_BALL_WIDTH (BALL_WIDTH*15/8)
#define GAME_BALL_HEIGHT 128
#define GAME_BALL_LEFT ((ROAD_WIDTH-GAME_BALL_WIDTH)/2)
#define GAME_BALL_TOP (ROAD_BALL_Y-GAME_BALL_HEIGHT/2)
void player_select(unsigned skin);
unsigned player_selected(void);
const char *player_name(void);
const uint16_t *player_colors(void);
/* One 62-pixel RLE row, run length in bits 7:3, palette index in 2:0.
 * Decorated spheres share a scratch row; every skin keeps the same alpha
 * silhouette and collision footprint. */
const uint8_t *player_row(unsigned y,unsigned *count);
#endif
