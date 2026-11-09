/* Host regression for independent collision and visual jump clocks. */
#include <assert.h>
#include <stdio.h>
#include "../firmware/level.h"
#include "../firmware/sd_levels.h"

Level sd_level;
unsigned sd_levels_cell(uint32_t row,unsigned field) { (void)row;(void)field;return 0; }
int sd_art_load(uint8_t *target,unsigned capacity) { (void)target;(void)capacity;return 0; }

static void single(unsigned kind,unsigned duration) {
    TileEvent pad={8,4,kind};
    Level level={0};
    Road r;
    uint32_t camera,start;
    unsigned t,peak=0,previous=0,coefficient=kind==TILE_SHORT_JUMP?12u:7u;
    level.row_count=64; level.tiles=&pad; level.tile_count=1;
    road_init(&r); level_load(&r,&level,&camera);
    camera=start=8*256-r.depth_q8[ROAD_BALL_Y];
    assert(level_step(&r,&camera,0,0)==GAME_RUNNING);
    assert(r.jump_ms==duration && road_jump_height(&r)==0);
    for(t=1;t<=duration+ROAD_VISUAL_LANDING_MS;t++) {
        unsigned height;
        assert(level_step(&r,&camera,0,1)==GAME_RUNNING);
        assert(camera-start==t*3/4);
        assert(r.jump_ms==(t<duration?duration-t:0));
        assert(level_jump_height_q8(&r)==(t<duration?((duration-t)*t*coefficient)>>12:0));
        height=road_jump_height(&r);
        if(t<=duration/2) {
            unsigned expected=(((duration-t)*t*coefficient)>>12)*3>>5;
            assert(height==expected); /* unchanged ascent and apex */
        } else assert(height<=previous);
        if(height>peak) peak=height;
        if(t==duration) assert(height>0 && !r.jump_ms);
        previous=height;
    }
    assert(peak==((duration/2*(duration-duration/2)*coefficient)>>12)*3/32);
    assert(road_jump_height(&r)==0 && !r.visual_jump_ms);
    if(kind==TILE_JUMP) assert(((camera+r.depth_q8[ROAD_BALL_Y])&255)==128);
    level_load(&r,&level,&camera);
    assert(!r.visual_jump_ms && !r.visual_next_total);
}

static void chain(void) {
    const TileEvent pads[]={{8,4,TILE_JUMP},{12,4,TILE_JUMP},{16,4,TILE_JUMP}};
    Level level={0}; Road r; uint32_t camera;
    unsigned t,triggers=0,last_row=0xffff;
    level.row_count=64; level.tiles=pads; level.tile_count=3;
    road_init(&r); level_load(&r,&level,&camera);
    camera=8*256-r.depth_q8[ROAD_BALL_Y];
    for(t=0;t<=3*ROAD_JUMP_MS+ROAD_VISUAL_LANDING_MS;t++) {
        unsigned before=r.visual_jump_ms,height=road_jump_height(&r);
        assert(level_step(&r,&camera,0,t?1:0)==GAME_RUNNING);
        if(r.jump_row!=last_row) {
            last_row=r.jump_row; triggers++;
            if(before) {
                unsigned after=road_jump_height(&r);
                assert(r.visual_jump_ms==before-1);
                assert(after<=height && height-after<=1); /* no snap on retrigger */
            }
        }
    }
    assert(triggers==3 && !r.jump_ms && !r.visual_jump_ms && !r.visual_next_total);
}

int main(void) {
    single(TILE_JUMP,ROAD_JUMP_MS);
    single(TILE_SHORT_JUMP,ROAD_SHORT_JUMP_MS);
    chain();
    puts("PASS jump: original collision timing, unchanged ascent/apex, extended monotonic descent, full-jump tile-centre landing, three chained pads without reset snap or growing delay, reset");
    return 0;
}
