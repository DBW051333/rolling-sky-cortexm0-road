/* Desktop verification only. No images or browser preview are generated. */
#include <stdio.h>
#include <assert.h>
#include "../firmware/road.h"
#include "../firmware/appearance.h"
#include "../firmware/sd_levels.h"
/* ROM-only host verification: no SD controller or MMIO is exercised. */
Level sd_level;
unsigned sd_levels_cell(uint32_t row,unsigned field) { (void)row;(void)field;return 0; }
int sd_art_load(uint8_t *target,unsigned capacity) { (void)target;(void)capacity;return 0; }
static uint16_t image[ROAD_WIDTH*ROAD_HEIGHT];
static unsigned cursor,remaining,lines,pixels;
static void line(unsigned x,unsigned y,unsigned n,void *context) {
    (void)context;
    assert(!remaining && x==0 && y==lines && n==ROAD_WIDTH);
    cursor=y*ROAD_WIDTH; remaining=n; lines++;
}
static void span(unsigned n,uint16_t color,void *context) {
    (void)context; assert(n && n<=remaining);
    remaining-=n; pixels+=n;
    while(n--) { assert(cursor<ROAD_WIDTH*ROAD_HEIGHT); image[cursor++]=color; }
}
static unsigned render(Road *r,unsigned camera,int cached) {
    const RoadSink sink={line,span,0};
    unsigned i,hash=2166136261u;
    lines=0; remaining=0; pixels=0;
    if(cached) road_draw(r,camera,1,&sink);
    else road_draw_calculated(r,camera,1,&sink);
    assert(lines==ROAD_HEIGHT && pixels==ROAD_WIDTH*ROAD_HEIGHT && !remaining);
    for(i=0;i<ROAD_WIDTH*ROAD_HEIGHT;i++) hash=(hash^image[i])*16777619u;
    return hash;
}
int main(void) {
    Road r={0}; RoadPoint p;
    unsigned y,phase,old_hash,new_hash;
    road_init(&r);
    {
        unsigned elapsed,travel=0;
        for(elapsed=0;elapsed<1000;elapsed++) travel+=road_forward(&r,1);
        assert(travel==750 && !r.forward_remainder);
    }
    for(y=0;y<320;y++) {
        assert(r.depth_q8[y]==(19552-20*y)*256/(1248+12*y));
        assert(r.scale_q8[y]==(1248+12*y)*256/52);
        if(y) assert(r.depth_q8[y]<=r.depth_q8[y-1]);
    }
    assert(r.scale_q8[0]==24*256);
    assert(r.scale_q8[319]>97*256 && r.scale_q8[319]<98*256);
    assert(r.depth_q8[0]-r.depth_q8[319]>13*256);
    assert(r.depth_q8[0]-r.depth_q8[319]<15*256);
    assert(!road_query(&r,-1,0,&p) && !road_query(&r,1280,0,&p));
    assert(road_query(&r,640,1408,&p) && p.column==2 && p.row==5);
    old_hash=render(&r,33,1);
    assert(image[179*ROAD_WIDTH+ROAD_WIDTH/2-11]==0xffda); // Upper-left highlight.
    assert(image[192*ROAD_WIDTH+ROAD_WIDTH/2]==0xf521); // Gold body.
    assert(image[207*ROAD_WIDTH+ROAD_WIDTH/2+15]==0x6980); // Lower-right shade.
    assert(image[316*ROAD_WIDTH+ROAD_WIDTH/2+17]==appearance_for(0,0)->light); // Ground below ball remains visible.
    road_set_cell(&r,5,2,0); new_hash=render(&r,33,1); assert(old_hash!=new_hash);
    {
        int depth=1408-33;
        int py=56+384*(48*256-5*depth)/(12*depth+20*256);
        assert(image[py*ROAD_WIDTH+ROAD_WIDTH/2]==0x19a4u);
    }
    road_set_cell(&r,5,2,1); assert(render(&r,33,1)==old_hash);
    for(phase=0;phase<256;phase++) {
        int position;
        road_set_cell(&r,5,2,(phase&1)!=0);
        assert(((phase+r.depth_q8[0])>>8)-((phase+r.depth_q8[319])>>8)<ROAD_ROWS);
        for(position=128;position<=1152;position+=256) {
            r.camera_x_q8=(uint16_t)position;
            assert(render(&r,phase,0)==render(&r,phase,1));
        }
    }
    road_init(&r);
    for(y=0;y<100;y++) road_pan(&r,-1,33);
    assert(r.camera_x_q8==ROAD_CAMERA_MIN);
    render(&r,0,1); assert(image[319*ROAD_WIDTH]==0x19a4u);
    for(y=0;y<100;y++) road_pan(&r,1,33);
    assert(r.camera_x_q8==ROAD_CAMERA_MAX);
    render(&r,0,1); assert(image[319*ROAD_WIDTH+ROAD_WIDTH-1]==0x19a4u); // Old road erased after pan.
    road_pan(&r,0,100); assert(r.camera_x_q8==ROAD_CAMERA_MAX);
    printf("PASS: all 256 phases x five camera positions, clipping, coverage, holes/restoration, both pan limits\n");
    printf("PASS: forward speed 3/4 Q8 per ms; fixed sphere highlight/body/shade, no ground shadow\n");
    printf("Road state %u B; 63360 pixels/frame; top %.2f tiles wide, bottom %.2f; vertical %.2f world tiles\n",
        (unsigned)sizeof(Road),(double)ROAD_WIDTH*256/r.scale_q8[0],(double)ROAD_WIDTH*256/r.scale_q8[319],(r.depth_q8[0]-r.depth_q8[319])/256.0);
    return 0;
}
