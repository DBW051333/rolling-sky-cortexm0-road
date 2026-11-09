#include "player.h"
#include "ball_sprite.h"
#include "player_skins.h"
static uint8_t selected;
static uint8_t runs[BALL_WIDTH];
void player_select(unsigned skin) { selected=(uint8_t)(skin<PLAYER_COUNT?skin:0); }
unsigned player_selected(void) { return selected; }
const char *player_name(void) {
    return skin_names[selected];
}
const uint16_t *player_colors(void) { return selected?skin_palettes[selected-1]:ball_palette; }
static unsigned accent(unsigned x,unsigned y) {
    unsigned diagonal=x+y,tilt=x+(y>>1);
    switch(selected) {
    case 1: return y==16 || y==17 || y==50 || y==51;
    case 2: return tilt>=47 && tilt<51;
    case 3: return (diagonal&15u)<2;
    case 4: return x>=27+((y>>3)&1u)*8 && x<31+((y>>3)&1u)*8;
    case 5: return (diagonal&31u)<2 || ((x+68-y)&31u)<2;
    case 6: return (x&15u)<2 || (y&15u)<2;
    case 7: return ((x*3+y)&31u)<4 || ((x+y*3)&63u)<2;
    case 8: return (tilt>=38 && tilt<41) || (tilt>=61 && tilt<64);
    case 9: return (diagonal>=64 && diagonal<=69) || (x+68-y>=64 && x+68-y<=69);
    case 10: return ((x&15u)==4 && (y&15u)<12) || ((y&15u)==11 && (x&15u)>=4);
    default: return ((x+(y>>2)*3)&15u)<3;
    }
}
const uint8_t *player_row(unsigned y,unsigned *count) {
    unsigned x=0,i,out=0;
    const uint8_t *source;
    if(y>=68) { *count=0; return runs; }
    source=ball_runs+ball_row_offset[y];
    *count=ball_row_offset[y+1]-ball_row_offset[y];
    if(!selected) return source;
    for(i=0;i<*count;i++) {
        unsigned length=source[i]>>3,shade=source[i]&7u;
        while(length--) {
            unsigned ink=shade;
            if(shade>1 && accent(x,y)) ink=shade<3?6u:7u;
            if(out && (runs[out-1]&7u)==ink && (runs[out-1]>>3)<31)
                runs[out-1]+=8;
            else runs[out++]=(uint8_t)(8u|ink);
            x++;
        }
    }
    *count=out;
    return runs;
}
