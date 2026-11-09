/* Exercise the production HUD compositor without entering MMIO firmware main.
 * gcc -O2 -ffunction-sections -fdata-sections tools/verify_hud.c
 *     '-Wl,--gc-sections' -o tmp/verify_hud.exe */
#define HUD_HOST_TEST
#include "../firmware/main.c"
#include <assert.h>
#include <stdio.h>
#define BACKGROUND 0x5aa5u
static uint16_t frame[20][ROAD_WIDTH];
static void render(void) {
    unsigned x,y;
    for(y=0;y<20;y++) {
        for(x=0;x<ROAD_WIDTH;x++)frame[y][x]=BACKGROUND;
        hud_overlay(frame[y],y,0);
    }
}
int main(void) {
    unsigned x,y;
    hud.done=56;hud.gems=17;render();
    for(y=0;y<2;y++)for(x=0;x<ROAD_WIDTH;x++)
        assert(frame[y][x]==(x<56?0xffe0u:0x18e3u));
    /* No rectangular score plate: margins and space below text stay visible. */
    for(y=4;y<20;y++)assert(frame[y][3]==BACKGROUND && frame[y][30]==BACKGROUND);
    for(y=16;y<20;y++)for(x=3;x<31;x++)assert(frame[y][x]==BACKGROUND);
    /* Every lit stroke covers both of its rows, despite transparent gaps. */
    for(y=0;y<5;y++)for(x=0;x<3;x++) {
        unsigned dx,dy;
        for(dy=0;dy<2;dy++)for(dx=0;dx<2;dx++) {
            if(hud_digits[1][y]&(1u<<(2-x)))assert(frame[5+y*2+dy][16+x*2+dx]==0xffffu);
            if(hud_digits[7][y]&(1u<<(2-x)))assert(frame[5+y*2+dy][23+x*2+dx]==0xffffu);
        }
    }
    for(y=4;y<20;y++)for(x=32;x<ROAD_WIDTH;x++)assert(frame[y][x]==BACKGROUND);
    hud.vol=10;hud.vol_show=1;render();
    for(y=8;y<14;y++)for(x=ROAD_WIDTH-44;x<ROAD_WIDTH-5;x++)assert(frame[y][x]==0x07e0u);
    hud.vol_show=0;hud.paused=1;render();
    for(y=8;y<20;y++)for(x=ROAD_WIDTH/2-9;x<ROAD_WIDTH/2-4;x++)assert(frame[y][x]==0xffffu);
    hud.state=GAME_CLEAR;hud.pct=100;hud.paused=0;render();
    for(y=0;y<4;y++)for(x=0;x<ROAD_WIDTH;x++)assert(frame[y][x]==0x07e0u);
    assert(frame[6][ROAD_WIDTH/2-36]==BACKGROUND);
    for(x=ROAD_WIDTH/2-36;x<ROAD_WIDTH/2+36;x++)assert(frame[19][x]==BACKGROUND);
    puts("PASS HUD: transparent score/result margins, outlined two-row digit strokes, volume and pause rectangles");
    return 0;
}
