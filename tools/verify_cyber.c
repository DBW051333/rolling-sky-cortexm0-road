/* Host check of the actual firmware renderers. Build with ROAD_BENCHMARK,
 * road/menu/flyover/player/level/obstacle/walls/glass/appearance.c; no MMIO used.
 * Optional argument is an existing directory for native RGB565 previews. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/menu.h"
#include "../firmware/player.h"
#include "../firmware/flyover.h"
#include "../firmware/level.h"
#include "../firmware/sd_levels.h"
Level sd_level;
unsigned sd_levels_cell(uint32_t row,unsigned field) { (void)row;(void)field;return 0; }
int sd_art_load(uint8_t *target,unsigned capacity) { (void)target;(void)capacity;return 0; }
static uint16_t frame[ROAD_HEIGHT][ROAD_WIDTH];
static unsigned rows,x,count;
static uint32_t hash;
static void begin(unsigned left,unsigned y,unsigned n,void *ctx) {
    (void)ctx; assert(left==0 && y==rows && n==ROAD_WIDTH && (!rows || x==ROAD_WIDTH));
    rows++; x=0;
}
static void span(unsigned n,uint16_t c,void *ctx) {
    (void)ctx; assert(rows && n && x+n<=ROAD_WIDTH);
    count+=n;
    while(n--) { frame[rows-1][x++]=c; hash=(hash^c)*16777619u; }
}
static void reset(void) { rows=x=count=0; hash=2166136261u; }
static void complete(void) { assert(rows==ROAD_HEIGHT && x==ROAD_WIDTH && count==ROAD_WIDTH*ROAD_HEIGHT); }
static void save(const char *dir,const char *kind,unsigned skin,unsigned t) {
    char path[512]; FILE *f;
    if(!dir) return;
    snprintf(path,sizeof(path),"%s/%s-%u-%u.rgb565",dir,kind,skin,t);
    f=fopen(path,"wb"); assert(f); assert(fwrite(frame,2,ROAD_WIDTH*ROAD_HEIGHT,f)==ROAD_WIDTH*ROAD_HEIGHT); fclose(f);
}
static void check_sprite(int lift,int red,int hidden) {
    const uint16_t *palette=player_colors(); unsigned y;
    if(hidden) return;
    for(y=0;y<68;y++) {
        unsigned n,i,pos=BALL_LEFT; const uint8_t *runs=player_row(y,&n);
        int sy=BALL_TOP+(int)y-lift;
        for(i=0;i<n;i++) {
            unsigned end=pos+(runs[i]>>3),index=runs[i]&7u;
            if(index && sy>=20 && sy<320) {
                unsigned px;
                for(px=pos;px<end;px++) assert(frame[sy][px]==(red?0xf800u:palette[index]));
            }
            pos=end;
        }
    }
}
int main(int argc,char **argv) {
    const char *dir=argc>1?argv[1]:0;
    const RoadSink sink={begin,span,0};
    unsigned skin,y,n,i,lane,l,phase,j,total=0;
    uint32_t menu_hash=0;
    unsigned char silhouette[BALL_HEIGHT][BALL_WIDTH];
    MenuState state={0}; state.total=5; state.card=5; state.volume=10;
    state.best=87; state.gems=12; state.percent=50; state.live=1;
    for(skin=0;skin<PLAYER_COUNT;skin++) {
        Road road; uint32_t camera;
        player_select(skin);
        for(y=0;y<68;y++) {
            const uint8_t *runs=player_row(y,&n); unsigned width=0;
            assert(n && n<=BALL_WIDTH);
            for(i=0;i<n;i++) {
                unsigned k,length=runs[i]>>3;
                assert(length && width+length<=BALL_WIDTH);
                for(k=0;k<length;k++) {
                    unsigned opaque=(runs[i]&7u)!=0;
                    if(!skin) silhouette[y][width+k]=(unsigned char)opaque;
                    else assert(silhouette[y][width+k]==opaque);
                }
                width+=length;
            }
            assert(width==68);
        }
        player_row(68,&n); assert(!n);
        player_row(~0u,&n); assert(!n);
        state.focus=(uint8_t)(skin>=4); state.selected=(uint8_t)(skin%5);
        for(i=0;i<4;i++) {
            reset(); menu_draw(&state,i*1024,&sink); complete();
            if(i==1) assert(hash!=menu_hash);
            menu_hash=hash; save(dir,"menu",skin,i);
        }
        /* With a clear start scene, every opaque sprite pixel must survive
         * road/gem composition, including jump, red flash and fall clipping. */
        for(i=0;i<5;i++) {
            int lift,red=0,hidden=0;
            road_init(&road); level_load(&road,&level_one,&camera);
            if(i==1) { road.jump_ms=road.jump_total/2; road.visual_jump_total=road.jump_total+ROAD_VISUAL_LANDING_MS; road.visual_jump_ms=road.visual_jump_total/2; }
            if(i==2) { road.death_kind=GAME_HIT;road.death_ms=70;red=1; }
            if(i==3) { road.death_kind=GAME_FELL;road.death_ms=700; }
            if(i==4) { road.death_kind=GAME_FELL;road.death_ms=800;hidden=1; }
            lift=(int)road_jump_height(&road);
            if(i>=3) lift-=(int)(road.death_ms*road.death_ms>>11);
            if(i==2) lift-=(int)(70*(400-70)>>11);
            reset();road_draw(&road,camera,0,&sink);complete();
            check_sprite(lift,red,hidden);save(dir,"game",skin,i);
        }
        /* Cached and calculated projection must agree across all skins,
         * three levels, five lanes, 37 world phases. */
        for(l=0;l<LEVEL_COUNT;l++) for(phase=0;phase<256;phase+=7) for(lane=0;lane<5;lane++) {
            uint32_t first=0;
            road_init(&road);level_load(&road,levels[l],&camera);
            camera=((phase*levels[l]->row_count/256)*256+phase);
            road.camera_x_q8=(uint16_t)(128+lane*256);
            for(j=0;j<2;j++) {
                reset();
                if(j) road_draw_calculated(&road,camera,0,&sink);else road_draw(&road,camera,0,&sink);
                complete();if(j) assert(hash==first);else first=hash;total++;
            }
        }
    }
    player_select(PLAYER_COUNT);assert(player_selected()==0);
    player_select(4); state.focus=1; state.total=24; state.selected=23;
    state.card=0; state.error=1; state.level_id=4;
    for(i=0;i<32;i++) {
        reset();menu_draw(&state,i*128,&sink);complete();save(dir,"motion",4,i);
    }
    /* Verify the freestanding clock reduction without EABI division. */
    for(i=0;i<FLYOVER_PERIOD;i++) {
        assert(flyover_time(i)==i);
        assert(flyover_time(i+FLYOVER_PERIOD)==i);
    }
    assert(flyover_time(0xffffffffu)==0xffffffffu%FLYOVER_PERIOD);
    /* Sample every scene, transitions and equivalent ticks in the next loop. */
    for(i=0;i<200;i++) {
        uint32_t first;
        reset();menu_draw(&state,i*400,&sink);complete();first=hash;
        save(dir,"tour",4,i);
        reset();menu_draw(&state,i*400+FLYOVER_PERIOD,&sink);complete();assert(hash==first);
    }
    reset();menu_draw(&state,0xffffffffu,&sink);complete();
    reset();menu_draw(&state,0xffffffffu%FLYOVER_PERIOD,&sink);complete();
    { uint32_t last=hash;
      reset();menu_draw(&state,0xffffffffu,&sink);complete();assert(hash==last); }
    puts("PASS flyover: 200 tour frames and exact 80000 ms loop, four detailed scenes, large uptime");
    printf("PASS: %u skins, %u RLE rows with identical sphere silhouettes, %u animated menus (SD/ROM/error), %u sprite-state frames, %u projection frames; %u pixels/frame\n",PLAYER_COUNT,PLAYER_COUNT*BALL_HEIGHT,PLAYER_COUNT*4+32,PLAYER_COUNT*5,total,ROAD_WIDTH*ROAD_HEIGHT);
    return 0;
}
