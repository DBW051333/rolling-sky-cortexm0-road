#include "level.h"
#include "obstacle.h"
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
/* ROM fallback gems mirror the SD pack placements; mechanisms stay an SD-only
 * showcase until the fallback needs them. */
static const LevelGem first_gems[]={
    {4,2},{12,2},{25,2},{33,2},{43,2},{57,2},{66,2},{70,2}
};
const Level level_one={first_ground,first_tiles,72,3,7,1,first_gems,0,8,0};
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
static const LevelGem second_gems[]={
    {4,2},{21,2},{37,2},{53,2},{69,2},{77,2}
};
const Level level_two={second_ground,second_tiles,80,4,9,2,second_gems,0,6,0};
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
    road->gem_bits=0; road->gems_got=0;
    road->gem_cursor=0; road->mech_cursor=0;
    *camera=0;
}
unsigned level_step(Road *road,uint32_t *camera,int direction,uint32_t ms) {
    /* One-ms steps sweep both axes, preventing skipped holes on long frames.
     * Ball anchor is screen (120,192), exactly the renderer's ground sample.
     * Ground support is checked when grounded and on the landing step. */
    static const unsigned collision_kinds[]={
        TILE_LOW_BARRIER,TILE_SOLID,TILE_AIR_BLOCK,TILE_GROUND_BLOCK
    };
    uint32_t cached_row=0xffffffffu,volume_rows[3];
    unsigned volume_masks[3][4],pad=0,glass=0,short_pad=0;
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
            unsigned row_index,kind_index;
            cached_row=z>>8;
            pad=level_tiles(road->level,cached_row,TILE_JUMP);
            glass=level_tiles(road->level,cached_row,TILE_FRAGILE);
            short_pad=level_tiles(road->level,cached_row,TILE_SHORT_JUMP);
            for(row_index=0;row_index<3;row_index++) {
                int32_t row=(int32_t)cached_row+(int32_t)row_index-1;
                volume_rows[row_index]=row<0?0xffffffffu:(uint32_t)row;
                for(kind_index=0;kind_index<4;kind_index++)
                    volume_masks[row_index][kind_index]=
                        row<0 || (uint32_t)row>=road->level->row_count?0:
                        level_tiles(road->level,(uint32_t)row,collision_kinds[kind_index]);
            }
        }
        glass_update(road,z);
        /* Gems are picked up by passing through the cell, grounded or airborne,
         * with a wider lateral window than the pads to feel generous. */
        {
            const LevelGem *gems=road->level->gems;
            unsigned total=road->level->gem_count,i;
            unsigned row=z>>8;
            while(road->gem_cursor<total && gems[road->gem_cursor].row<row)
                road->gem_cursor++;
            for(i=road->gem_cursor;i<total && gems[i].row==row;i++) {
                int32_t dx;
                if(road->gem_bits>>i&1u) continue;
                dx=road->camera_x_q8-(int32_t)(gems[i].column*256+128);
                if(dx<0) dx=-dx;
                if(dx<=96) { road->gem_bits|=1u<<i; road->gems_got++; }
            }
        }
        height=level_jump_height_q8(road);
        {
            int32_t x=road->camera_x_q8;
            uint32_t first_row=(z-BALL_COLLISION_RADIUS_Q8)>>8;
            uint32_t last_row=(z+BALL_COLLISION_RADIUS_Q8)>>8;
            unsigned first_column=(unsigned)(x-(int32_t)BALL_COLLISION_RADIUS_Q8)>>8;
            unsigned last_column=(unsigned)(x+(int32_t)BALL_COLLISION_RADIUS_Q8)>>8;
            uint32_t row;
            unsigned column,kind_index,row_index;
            for(row=first_row;row<=last_row;row++) {
                for(row_index=0;row_index<3 && volume_rows[row_index]!=row;row_index++) {}
                if(row_index==3) continue;
                for(column=first_column;column<=last_column;column++) {
                    for(kind_index=0;kind_index<4;kind_index++) {
                        ObstacleVolume volume;
                        if(!(volume_masks[row_index][kind_index]&(1u<<column))) continue;
                        if(obstacle_volume_from_tile(collision_kinds[kind_index],row,column,&volume) &&
                           obstacle_hits_ball(&volume,x,z,height)) return GAME_HIT;
                    }
                }
            }
        }
        /* Mechanisms: sparse rows, so a cursor plus a small forward window
         * keeps the per-step cost proportional to active mechanisms only. */
        {
            const LevelMech *mechs=road->level->mechs;
            unsigned total=road->level->mech_count,i;
            uint32_t base;
            while(road->mech_cursor<total &&
                  (uint32_t)mechs[road->mech_cursor].row*256u+1152u<z)
                road->mech_cursor++;
            for(i=road->mech_cursor;i<total;i++) {
                ObstacleVolume volume;
                base=(uint32_t)mechs[i].row*256u;
                if(base>z+1280u) break; /* sorted: nothing else is near yet */
                if(base+640u<z) continue;
                if(obstacle_mech_volume(&mechs[i],z,&volume) &&
                   obstacle_hits_ball(&volume,road->camera_x_q8,z,height))
                    return GAME_HIT;
            }
        }
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
