#include "player.h"
#include "ball_sprite.h"
#include "player_pixels.h"
static uint8_t selected;
static uint8_t runs[12];
static const uint16_t palettes[7][8]={
    {0,0x016a,0x0232,0x039a,0x05be,0x07df,0xf91f,0xefff},
    {0,0x2009,0x4011,0x701b,0xa81f,0xd29f,0xed5f,0x07ff},
    {0,0x580b,0x980d,0xd00e,0xf811,0xfb36,0xfd1a,0xff3d},
    {0,0x08e8,0x124c,0x24b6,0x87fb,0xff19,0xeffe,0x07f4},
    {0,0x100a,0x19d5,0x349f,0x9e7f,0xff19,0xefff,0x07ff},
    {0,0x2828,0x8a4c,0xf4b0,0xff16,0xff19,0xffbc,0xfb8f},
    {0,0x180b,0x40f1,0x827c,0xd5bf,0xff19,0xf7ff,0x07ff}
};
void player_select(unsigned skin) { selected=(uint8_t)(skin<PLAYER_COUNT?skin:0); }
unsigned player_selected(void) { return selected; }
const char *player_name(void) {
    static const char names[8][12]={"GOLD CORE","ION CYAN","VOID ORB","PINK NOVA",
        "GPT MUSE","DEEPSEEK","CLAUDE","GLM NEKO"};
    return names[selected];
}
const uint16_t *player_colors(void) { return selected?palettes[selected-1]:ball_palette; }
const uint8_t *player_row(unsigned y,unsigned *count) {
    unsigned x;
    if(y>=68) { *count=0; return runs; }
    if(selected<4) {
        const uint8_t *source=ball_runs+ball_row_offset[y];
        *count=ball_row_offset[y+1]-ball_row_offset[y];
        if(!selected) return source;
        /* Neon latitude bands retain the source sphere's shaded silhouette.
         * The original sphere has at most twelve runs per row. */
        for(x=0;x<*count;x++) {
            unsigned run=source[x],ink=run&7u;
            int band=selected==1?(y==16 || y==17 || y==50 || y==51):
                selected==2?(y>=28 && y<=31):((y&15u)<2);
            if(ink>1 && band) run=(run&248u)|(selected==1?6u:7u);
            runs[x]=(uint8_t)run;
        }
        return runs;
    }
    y>>=2; x=chibi_offsets[selected-4][y];
    *count=chibi_offsets[selected-4][y+1]-x;
    return chibi_runs[selected-4]+x;
}
