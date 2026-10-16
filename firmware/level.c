#include "level.h"
#include "sd_levels.h"
#include "glass.h"
/* Full ground is implicit between runs. Opening rows give time to orient. */
static const GroundRun first_ground[]={
    {28,3,27}, {44,3,19}, {60,3,17}
};
static const TileEvent first_tiles[]={
    {8,31,TILE_FRAGILE},
    {19,4,TILE_JUMP}, {21,31,TILE_LOW_BARRIER},
    {35,2,TILE_JUMP}, {37,31,TILE_LOW_BARRIER},
    {51,16,TILE_JUMP}, {53,31,TILE_LOW_BARRIER}
};
const Level level_one={first_ground,first_tiles,72,3,7,1};
/* Same object rules, different lanes and rhythm. Each jump pad has its own
 * wall two rows ahead, with solid landing space before the next gap. */
static const GroundRun second_ground[]={
    {24,3,7}, {40,3,28}, {56,3,14}, {72,3,17}
};
static const TileEvent second_tiles[]={
    {8,31,TILE_FRAGILE},
    {15,16,TILE_JUMP}, {17,31,TILE_LOW_BARRIER},
    {31,1,TILE_JUMP}, {33,31,TILE_LOW_BARRIER},
    {47,8,TILE_JUMP}, {49,31,TILE_LOW_BARRIER},
    {63,2,TILE_JUMP}, {65,31,TILE_LOW_BARRIER}
};
static const Level level_two={second_ground,second_tiles,80,4,9,2};
#include "hills_level.h"
const Level *const levels[LEVEL_COUNT]={&level_one,&level_two,&hills_level};
/* Logical vertical position in fixed Q8 units, independent of artwork. */
unsigned level_jump_height_q8(const Road *road) {
    unsigned total=road->jump_total,remaining=road->jump_ms;
    unsigned coefficient=total==ROAD_SHORT_JUMP_MS?6u:3u;
    return (remaining*(total-remaining)*coefficient)>>11;
}
unsigned road_jump_height(const Road *road) {
    return (level_jump_height_q8(road)*3u)>>5;
}
unsigned level_tiles(const Level *level,uint32_t row,unsigned kind) {
    unsigned i,mask=0;
    if(level==&sd_level) return sd_levels_cell(row,kind);
    for(i=0;i<level->tile_count;i++) {
        const TileEvent *tile=&level->tiles[i];
        if(tile->row>row) break;
        if(tile->row==row && tile->kind==kind) mask|=tile->columns;
    }
    return mask;
}
unsigned level_ground(const Level *level,uint32_t row) {
    unsigned i;
    if(row>=level->row_count) return 0;
    if(level==&sd_level) return sd_levels_cell(row,0);
    for(i=0;i<level->ground_count;i++) {
        const GroundRun *run=&level->ground[i];
        if(row<run->first_row) break;
        if(row>=run->first_row && row-run->first_row<run->row_count)
            return run->ground_mask;
    }
    return 31;
}
void level_load(Road *road,const Level *level,uint32_t *camera) {
    unsigned i;
    road->level=level;
    glass_reset();
    for(i=0;i<ROAD_ROWS;i++) road->tags[i]=0xffffffffu;
    road->camera_x_q8=ROAD_CAMERA_CENTER;
    road->forward_remainder=0;
    road->jump_ms=0; road->jump_row=0xffffu; road->jump_total=ROAD_JUMP_MS;
    road->death_ms=0; road->death_kind=0;
    *camera=0;
}
unsigned level_step(Road *road,uint32_t *camera,int direction,uint32_t ms) {
    /* One-ms steps sweep both axes, preventing skipped holes on long frames.
     * Ball anchor is screen (120,192), exactly the renderer's ground sample.
     * Ground support is checked when grounded and on the landing step. */
    unsigned cached_row=0xffffffffu,wall=0,pad=0,glass=0,tall=0,air_block=0,short_pad=0,ground_block=0;
    if(ms>200) ms=200;
    do {
        uint32_t z;
        unsigned height;
        if(ms) {
            if(direction) road_pan(road,direction,1);
            *camera+=road_forward(road,1);
            if(road->jump_ms) road->jump_ms--;
            ms--;
        }
        z=*camera+road->depth_q8[ROAD_BALL_Y];
        if((z>>8)>=road->level->row_count) return GAME_CLEAR;
        if((z>>8)!=cached_row) {
            cached_row=z>>8;
            wall=level_tiles(road->level,cached_row,TILE_LOW_BARRIER);
            pad=level_tiles(road->level,cached_row,TILE_JUMP);
            glass=level_tiles(road->level,cached_row,TILE_FRAGILE);
            tall=level_tiles(road->level,cached_row,TILE_SOLID);
            air_block=level_tiles(road->level,cached_row,TILE_AIR_BLOCK);
            short_pad=level_tiles(road->level,cached_row,TILE_SHORT_JUMP);
            ground_block=level_tiles(road->level,cached_row,TILE_GROUND_BLOCK);
        }
        glass_update(road,z);
        height=level_jump_height_q8(road);
        if(((air_block&(1u<<(road->camera_x_q8>>8))) && height>=COLLISION_AIR_BOTTOM_Q8 && height<COLLISION_AIR_TOP_Q8) ||
           ((ground_block&(1u<<(road->camera_x_q8>>8))) && height<COLLISION_GROUND_TOP_Q8)) return GAME_HIT;
        if(tall&(1u<<(road->camera_x_q8>>8))) return GAME_HIT;
        if((wall&(1u<<(road->camera_x_q8>>8)))
           && (z&255u)<64 && height<COLLISION_GROUND_TOP_Q8)
            return GAME_HIT;
        if(!road->jump_ms) {
            unsigned column=1u<<(road->camera_x_q8>>8);
            if(!(road_mask(road,z>>8)&column)) return GAME_FELL;
            if(glass&column) glass_touch(road,z>>8,road->camera_x_q8>>8,z);
            /* Trigger at pad center; jump clears the next row's low wall. */
            if(road->jump_row!=(z>>8) && (z&255u)>=128 &&
               ((pad|short_pad)&column)) {
                road->jump_total=(short_pad&column)?ROAD_SHORT_JUMP_MS:ROAD_JUMP_MS;
                road->jump_ms=road->jump_total;
                road->jump_row=(uint16_t)(z>>8);
            }
        }
    } while(ms);
    return GAME_RUNNING;
}
