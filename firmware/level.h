#ifndef LEVEL_H
#define LEVEL_H
#include "road.h"
#include "obstacle.h"
/* All world rows are absolute. Bit 0 is the leftmost tile; 1 means solid.
 * Separate future object tracks can share these same world coordinates. */
typedef struct {
    uint16_t first_row;
    uint8_t row_count, ground_mask;
} GroundRun;
#define COLLISION_GROUND_TOP_Q8 680u
#define COLLISION_AIR_BOTTOM_Q8 600u
#define COLLISION_AIR_TOP_Q8 1024u

enum { TILE_JUMP=1, TILE_LOW_BARRIER=2, TILE_FRAGILE=3, TILE_SOLID=4, TILE_AIR_BLOCK=5, TILE_SHORT_JUMP=6, TILE_GROUND_BLOCK=7 };
typedef struct {
    uint16_t row;
    uint8_t columns,kind;
} TileEvent;
/* Collectibles and dynamic mechanisms are sparse, sorted by row, and live in
 * side tables instead of per-row masks. Gems may float over gaps; mechanisms
 * keep reusing the shared ObstacleVolume collision with a time-varying pose. */
typedef struct {
    uint16_t row;
    uint8_t column;
} LevelGem;
enum { MECH_LASER=1, MECH_MOVER=2, MECH_HAMMER=3 };
typedef struct LevelMech {
    uint16_t row;
    uint8_t column,kind;
    uint8_t period_shift; /* period = 256 << period_shift Q8 units of travel */
    uint16_t phase,param; /* laser: beam reach toward the player; mover: amplitude */
} LevelMech;
typedef struct Level {
    const GroundRun *ground;
    const TileEvent *tiles;
    uint16_t row_count;
    uint8_t ground_count,tile_count;
    uint16_t id;
    const LevelGem *gems;
    const LevelMech *mechs;
    uint8_t gem_count,mech_count;
} Level;
enum { GAME_RUNNING, GAME_FELL, GAME_CLEAR, GAME_HIT };
extern const Level level_one;
#define LEVEL_COUNT 3
extern const Level *const levels[LEVEL_COUNT];
unsigned level_ground(const Level *level,uint32_t row);
unsigned level_tiles(const Level *level,uint32_t row,unsigned kind);
unsigned level_jump_height_q8(const Road *road);
void level_load(Road *road,const Level *level,uint32_t *camera);
unsigned level_step(Road *road,uint32_t *camera,int direction,uint32_t ms);
#endif
