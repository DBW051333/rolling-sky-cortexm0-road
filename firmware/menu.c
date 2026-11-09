#include "menu.h"
#include "player.h"
#include "flyover.h"
#include "menu_title.h"
#define CYAN 0x07ffu
#define PINK 0xf91fu
#define WHITE 0xe7ffu
#define MUTED 0x6bb2u
#define PANEL 0x0865u
static uint16_t *pixels;
static unsigned row;
/* Column bitmaps, A..Z then 0..9. Drawn into the shared scanline. */
static const uint8_t font[36][5]={
 {126,9,9,9,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {7,8,112,8,7},{97,81,73,69,67},
 {62,81,73,69,62},{0,66,127,64,0},{98,81,73,73,70},{34,65,73,73,54},
 {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
 {54,73,73,73,54},{6,73,73,41,30}
};
static __attribute__((noinline)) void rect(int x,int y,int w,int h,uint16_t c) {
    int end=x+w;
    if((int)row<y || (int)row>=y+h) return;
    if(x<0) x=0;
    if(end>ROAD_WIDTH) end=ROAD_WIDTH;
    while(x<end) pixels[x++]=c;
}
static __attribute__((noinline)) void label(int x,unsigned y,const char *s,unsigned large,uint16_t c) {
    unsigned dy,col;
    if(row<y || row>=y+(7u<<large)) return;
    dy=(row-y)>>large;
    while(*s) {
        unsigned ch=(unsigned char)*s++,index=36;
        if(ch>='A' && ch<='Z') index=ch-'A';
        if(ch>='0' && ch<='9') index=ch-'0'+26;
        for(col=0;col<5;col++) {
            static const uint8_t arrows[2][5]={{8,20,34,65,0},{0,65,34,20,8}};
            unsigned bits=index<36?font[index][col]:ch=='-'?8u:ch=='<'?arrows[0][col]:ch=='>'?arrows[1][col]:0u;
            if(bits&(1u<<dy)) rect(x+(int)(col<<large),(int)row,1<<large,1,c);
        }
        x+=6<<large;
    }
}
static void number(int x,unsigned y,unsigned n,unsigned large,uint16_t c) {
    char s[4]; unsigned tens=0;
    if(row<y || row>=y+(7u<<large)) return;
    if(n>99) n=99;
    /* Volatile keeps -Oz from synthesizing a freestanding EABI divide. */
    { volatile unsigned v=n; while(v>=10) { v-=10; tens++; } n=v; }
    s[0]=(char)('0'+tens); s[1]=(char)('0'+n); s[2]=0;
    label(x,y,s,large,c);
}
static void panel(unsigned top,unsigned height,uint16_t edge,int active) {
    rect(10,(int)top,ROAD_WIDTH-20,(int)height,PANEL);
    rect(10,(int)top,ROAD_WIDTH-20,1,edge);
    rect(10,(int)top,2,(int)height,edge);
    rect(10,(int)(top+height-1),ROAD_WIDTH-20,1,active?edge:0x210cu);
    rect(ROAD_WIDTH-12,(int)top,2,(int)height,0x210cu);
    rect(10,(int)top,5,3,0x0002u);
    rect(ROAD_WIDTH-15,(int)(top+height-3),5,3,0x0002u);
}
static unsigned title_bit(int x,int y) {
    if(x<0 || x>=174 || y<0 || y>=30) return 0;
    return (title_mask[y*22+(x>>3)]>>(x&7))&1u;
}
static void title(void) {
    int x,y=(int)row-27;
    static const uint16_t face_color[8]={0xffffu,0xe7ffu,0xbfffu,0x9effu,0x5e7fu,0x34fau,0x2bd5u,0x2373u};
    if(y<0 || y>=37) return;
    for(x=0;x<181;x++) {
        uint16_t color=0;
        /* Seven offset slices make a solid extrusion, not a drop shadow. */
        if(title_bit(x-6,y-6) || title_bit(x-4,y-4) || title_bit(x-2,y-2)) color=0x116bu;
        if(title_bit(x-1,y-1)) color=0x36bbu;
        if(title_bit(x,y)) {
            color=face_color[(unsigned)y>>2];
            if(!title_bit(x-1,y) || !title_bit(x,y-1)) color=0xffffu;
            else if(!title_bit(x+1,y) || !title_bit(x,y+1)) color=0x1393u;
        }
        if(color) pixels[9+x]=color;
    }
}
void menu_draw(const MenuState *s,uint32_t ticks,const RoadSink *sink) {
    unsigned x,time=flyover_time(ticks),stage=time<20000?0:time<40000?1:time<60000?2:3;
    const uint16_t *palette=player_colors();
    pixels=road_scanline_buffer();
    flyover_prepare(time);
    for(row=0;row<320;row++) {
        static const char locations[4][16]={"DEEP SPACE","RIVER MOUNTAINS","METROPOLIS","FOREST CANOPY"};
        flyover_line(pixels,row);
        label(12,9,"WORLD TOUR",0,WHITE);
        label(ROAD_WIDTH-36,9,s->card?"SD":"ROM",0,s->error?0xf800u:s->card?CYAN:WHITE);
        title();
        label(12,76,locations[stage],0,WHITE);
        rect(12,88,64,1,0x298du);
        rect(12,88,1+(int)((time*52u)>>16),1,0x97ffu);
        label(12,199,"A",0,MUTED);number(18,199,s->adc+1,0,MUTED);
        number(36,199,s->percent,0,s->live?(s->manual?0xffe0u:CYAN):MUTED);
        label(72,199,"VOL",0,MUTED);number(96,199,s->volume,0,CYAN);
        if(s->card) {
            label(122,199,"V",0,MUTED);number(128,199,s->revision,0,CYAN);
            label(150,199,"M",0,MUTED);number(156,199,s->mechs,0,CYAN);
        }
        panel(211,34,s->focus?0x3993u:CYAN,!s->focus);
        label(22,218,s->focus?"LEVEL":"LEVEL <    >",0,s->focus?MUTED:CYAN);
        number(ROAD_WIDTH-38,218,s->selected+1,1,WHITE);
        label(22,233,"BEST",0,MUTED); number(52,233,s->best,0,WHITE);
        label(76,233,"GEMS",0,MUTED); number(106,233,s->gems,0,WHITE);
        number(ROAD_WIDTH-38,233,s->total,0,MUTED);
        panel(250,69,s->focus?PINK:0x3993u,s->focus);
        label(22,257,s->focus?"SKIN <    >":"SKIN",0,s->focus?PINK:MUTED);
        label(22,274,player_name(),0,WHITE);
        number(22,288,player_selected()+1,0,CYAN);
        label(40,288,"OF",0,MUTED); number(58,288,PLAYER_COUNT,0,MUTED);
        label(22,306,"2 SELECT",0,CYAN);
        label(76,306,s->focus?"8 SET":"8 GO",0,PINK);
        if(row>=251 && row<319) {
            unsigned n,i,pos=ROAD_WIDTH-84; const uint8_t *runs=player_row(row-251,&n);
            for(i=0;i<n;i++) {
                unsigned len=runs[i]>>3,index=runs[i]&7u;
                if(index) rect((int)pos,(int)row,(int)len,1,palette[index]);
                pos+=len;
            }
        }
        sink->begin_line(0,row,ROAD_WIDTH,sink->context);
        for(x=0;x<ROAD_WIDTH;) {
            unsigned end=x+1; uint16_t color=pixels[x];
            while(end<ROAD_WIDTH && pixels[end]==color) end++;
            sink->span(end-x,color,sink->context); x=end;
        }
    }
}
