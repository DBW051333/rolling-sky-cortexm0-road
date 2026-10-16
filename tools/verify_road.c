/* Desktop verification only. No images or browser preview are generated. */
#include <stdio.h>
#include <assert.h>
#include "../firmware/road.h"
static uint16_t image[ROAD_WIDTH*ROAD_HEIGHT];
static unsigned cursor,remaining,lines,pixels;
static void line(unsigned x,unsigned y,unsigned n,void *context) {
    (void)context;
    assert(!remaining && x==0 && y==lines && n==240);
    cursor=y*240; remaining=n; lines++;
}
static void span(unsigned n,uint16_t color,void *context) {
    (void)context; assert(n && n<=remaining);
    remaining-=n; pixels+=n;
    while(n--) { assert(cursor<76800); image[cursor++]=color; }
}
static unsigned render(Road *r,unsigned camera,int cached) {
    const RoadSink sink={line,span,0};
    unsigned i,hash=2166136261u;
    lines=0; remaining=0; pixels=0;
    if(cached) road_draw(r,camera,1,&sink);
    else road_draw_calculated(r,camera,1,&sink);
    assert(lines==320 && pixels==76800 && !remaining);
    for(i=0;i<76800;i++) hash=(hash^image[i])*16777619u;
    return hash;
}
int main(void) {
    Road r; RoadPoint p;
    unsigned y,phase,old_hash,new_hash;
    road_init(&r);
    {
        unsigned elapsed,travel=0;
        for(elapsed=0;elapsed<1000;elapsed++) travel+=road_forward(&r,1);
        assert(travel==625 && !r.forward_remainder);
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
    assert(image[179*240+109]==0xffda); // Upper-left highlight.
    assert(image[192*240+120]==0xf521); // Gold body.
    assert(image[207*240+135]==0x6980); // Lower-right shade.
    assert(image[316*240+137]==0x2456); // Ground below ball remains visible.
    road_set_cell(&r,5,2,0); new_hash=render(&r,33,1); assert(old_hash!=new_hash);
    {
        int depth=1408-33;
        int py=56+384*(48*256-5*depth)/(12*depth+20*256);
        assert(image[py*240+120]==ROAD_BG);
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
    render(&r,0,1); assert(image[319*240]==ROAD_BG);
    for(y=0;y<100;y++) road_pan(&r,1,33);
    assert(r.camera_x_q8==ROAD_CAMERA_MAX);
    render(&r,0,1); assert(image[319*240+239]==ROAD_BG); // Old road erased after pan.
    road_pan(&r,0,100); assert(r.camera_x_q8==ROAD_CAMERA_MAX);
    printf("PASS: all 256 phases x five camera positions, clipping, coverage, holes/restoration, both pan limits\n");
    printf("PASS: forward speed 5/8 Q8 per ms; fixed sphere highlight/body/shade, no ground shadow\n");
    printf("Road state %u B; 76800 pixels/frame; top %.2f tiles wide, bottom %.2f; vertical %.2f world tiles\n",
        (unsigned)sizeof(Road),240.0*256/r.scale_q8[0],240.0*256/r.scale_q8[319],(r.depth_q8[0]-r.depth_q8[319])/256.0);
    return 0;
}
