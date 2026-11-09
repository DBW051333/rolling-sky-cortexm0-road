#include "menu.h"
#include "player.h"
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
static void city(uint32_t ticks) {
    unsigned i,x,t=ticks>>6;
    uint16_t base=row<70?0x0804u:row<142?0x1008u:0x0804u;
    for(x=0;x<ROAD_WIDTH;x++) pixels[x]=base;
    /* Slow skyline; a faster shop layer establishes parallax. All geometry
     * is pixel aligned, periodic and independent of menu selections. */
    if(row>=46 && row<142) for(i=0;i<9;i++) {
        unsigned world=i+(t>>6);
        int bx=(int)(i*32)-(int)((t>>1)&31u),top=52+(int)((world*13)&31u);
        if((int)row<top-6) continue;
        rect(bx,top,28,142-top,0x18cdu);
        rect(bx+3,top-6,2,6,0x29b1u);
        rect(bx+2,top,24,1,0x2933u);
        {
            unsigned wy;
            for(wy=0;wy<5;wy++) {
                int window_y=top+6+(int)wy*10;
                if((int)row<window_y || (int)row>=window_y+4) continue;
                for(x=0;x<3;x++)
                rect(bx+4+(int)x*8,top+6+(int)wy*10,3,4,((world+x+wy)&3)?0x3294u:0x6831u);
            }
        }
    }
    if(row>=96 && row<180) for(i=0;i<5;i++) {
        unsigned world=i+(t>>6);
        int bx=(int)(i*64)-(int)(t&63u),top=96+(int)((world*11)&15u);
        uint16_t neon=(world&1)?PINK:CYAN;
        if(row<146) {
        rect(bx,top,60,146-top,0x1049u);
        rect(bx,top,60,2,0x3811u);
        rect(bx+4,top+6,24,13,0x200du);
        rect(bx+4,top+6,24,1,neon);
        rect(bx+4,top+18,24,1,neon);
        for(x=0;x<4;x++) {
            rect(bx+7+(int)x*5,top+9,2,7,neon);
            rect(bx+6+(int)x*5,top+12,4,1,neon);
        }
        rect(bx+36,top+4,17,29,0x222fu);
        rect(bx+38,top+6,13,25,0x08b3u);
        rect(bx+43,top+6,2,25,0x1050u);
        rect(bx+3,143,54,2,neon);
        }
        /* Broken neon reflections in the wet street. */
        for(x=0;x<5;x++)
            rect(bx+8-(int)x*2,150+(int)x*7,24+(int)x*3,1,((x+t)&1)?0x2010u:0x0190u);
    }
    rect(0,146,ROAD_WIDTH,2,0x4976u);
    /* Hover taxi with a cyan headlight, sweeping past the shop fronts. */
    if(row>=130 && row<=143) { int car=(int)((t*3)&511u)-80;
      rect(car+7,130,22,5,0x692fu); rect(car,135,38,8,0xb177u);
      rect(car+10,131,16,4,0x04d9u); rect(car+33,136,6,3,CYAN);
      rect(car+4,143,30,1,PINK);
    }
    if(row>=48 && row<179) for(i=0;i<12;i++) {
        int rx=(int)((i*43+t)&255u),ry=(int)((i*29+t*5)&127u)+48;
        rect(rx,ry,1,4,0x3215u);
    }
    /* Road-edge lane lights continue scrolling below the cards. */
    if(row>=154) for(i=0;i<7;i++) {
        int y=154+(int)i*28+(int)((t*2)&15u);
        rect(2,y,4,10,0x0354u); rect(ROAD_WIDTH-6,y,4,10,0x780fu);
    }
}
void menu_draw(const MenuState *s,uint32_t ticks,const RoadSink *sink) {
    unsigned x;
    const uint16_t *palette=player_colors();
    pixels=road_scanline_buffer();
    for(row=0;row<320;row++) {
        city(ticks);
        label(12,9,"NEON DISTRICT",0,CYAN);
        label(ROAD_WIDTH-48,9,"ONLINE",0,PINK);
        label(19,28,"ROLLING",1,0x6013u);
        label(17,26,"ROLLING",1,WHITE);
        label(17,46,"SKY",1,CYAN);
        label(66,52,"NIGHT RUN",0,PINK);
        rect(17,65,50,2,CYAN); rect(69,65,24,2,PINK);
        panel(153,43,s->focus?0x3993u:CYAN,!s->focus);
        label(22,161,s->focus?"LEVEL":"LEVEL <    >",0,s->focus?MUTED:CYAN);
        number(ROAD_WIDTH-38,164,s->selected+1,1,WHITE);
        label(98,165,s->level_id==4?"HILLS":s->level_id==5?"MASSIF":"",0,MUTED);
        label(22,180,"BEST",0,MUTED); number(52,180,s->best,0,WHITE);
        label(76,180,"GEMS",0,MUTED); number(106,180,s->gems,0,WHITE);
        number(ROAD_WIDTH-38,180,s->total,0,MUTED);
        panel(205,77,s->focus?PINK:0x3993u,s->focus);
        label(22,215,s->focus?"SKIN <    >":"SKIN",0,s->focus?PINK:MUTED);
        label(22,235,player_name(),0,WHITE);
        number(22,251,player_selected()+1,0,CYAN);
        label(40,251,"OF",0,MUTED); number(58,251,PLAYER_COUNT,0,MUTED);
        label(22,269,"NEON ORBS",0,PINK);
        /* Actual game sprite at its native size, with a subtle scan pedestal. */
        rect(ROAD_WIDTH-84,276,68,2,0x3a39u);
        if(row>=209 && row<277) {
            unsigned n,i,pos=ROAD_WIDTH-84; const uint8_t *runs=player_row(row-209,&n);
            for(i=0;i<n;i++) {
                unsigned len=runs[i]>>3,index=runs[i]&7u;
                if(index) rect((int)pos,(int)row,(int)len,1,palette[index]);
                pos+=len;
            }
        }
        label(12,285,"2 SELECT",0,CYAN);
        label(ROAD_WIDTH-70,285,s->focus?"8 APPLY":"8 PLAY",0,PINK);
        label(12,298,"A",0,MUTED); number(18,298,s->adc+1,0,MUTED);
        number(36,298,s->percent,0,s->live?(s->manual?0xffe0u:CYAN):MUTED);
        label(72,298,"VOL",0,MUTED); number(96,298,s->volume,0,CYAN);
        if(s->card) {
            label(12,309,"V",0,MUTED); number(18,309,s->revision,0,CYAN);
            label(48,309,"M",0,MUTED); number(54,309,s->mechs,0,CYAN);
        }
        label(ROAD_WIDTH-36,309,s->card?"SD":"ROM",0,s->error?0xf800u:s->card?CYAN:WHITE);
        sink->begin_line(0,row,ROAD_WIDTH,sink->context);
        for(x=0;x<ROAD_WIDTH;) {
            unsigned end=x+1; uint16_t color=pixels[x];
            while(end<ROAD_WIDTH && pixels[end]==color) end++;
            sink->span(end-x,color,sink->context); x=end;
        }
    }
}
