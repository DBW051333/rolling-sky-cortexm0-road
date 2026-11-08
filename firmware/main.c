#include <stdint.h>
#include "road.h"
#include "level.h"
#include "sd_levels.h"
#include "menu_title.h"
#include "appearance.h"
#include "sound.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define KEY REG(0x40000000)
#define KEY_HELD REG(0x40000004)
/* Slide potentiometer on header P2, converted by the FPGA's internal 12-bit
 * ADC. Channels 1..7 are the seven ADC pins on that socket; the keyboard
 * peripheral exports them at 0x008 + 4*(channel-1). */
#define ADC_LEVEL(ch) REG(0x40000008u+4u*((ch)-1u))
#define CS REG(0x40050000)
#define RS REG(0x40050004)
#define WR REG(0x40050008)
#define RD REG(0x4005000c)
#define RESET REG(0x40050010)
#define BACKLIGHT REG(0x40050014)
#define PIXEL REG(0x40050060)
#define SPAN_COLOR REG(0x40050080)
#define SPAN_COUNT REG(0x40050084)
#define SPAN_STATUS REG(0x40050088)
volatile uint32_t milliseconds,key_events;
volatile uint32_t road_frame_count,road_last_frame_ms;
static Road road;
#ifdef ROAD_BENCHMARK
void road_baseline_init(void);
void road_baseline_frame(uint32_t,int,void (*)(unsigned,unsigned,unsigned,void*),
    void (*)(unsigned,uint16_t,void*),void*);
#define BENCH_MARK REG(0x20000c00)
#endif
void tick_isr(void) { milliseconds++; }
void key_isr(void) { key_events|=KEY; KEY=1; }
void uart_isr(void) { REG(0x40000108)=8; }
static void wait_ms(uint32_t ms) {
#ifdef ROAD_SIM_FAST_INIT
    (void)ms; /* Only LCD power-on delays; render loops/pixel counts stay exact. */
#else
    uint32_t start=milliseconds;
    while((uint32_t)(milliseconds-start)<ms) {}
#endif
}
static void write_lcd(uint16_t value,int is_command) {
    CS=0; RS=is_command?0:1; PIXEL=value; WR=0;
    __asm volatile("nop\nnop\nnop\nnop");
    WR=1; CS=1;
}
static void command(uint16_t v) { write_lcd(v,1); }
static void data(uint16_t v) { write_lcd(v,0); }
static void window(unsigned x,unsigned y,unsigned w,unsigned h) {
    unsigned right=x+w-1,bottom=y+h-1;
    command(0x2a); data(x>>8); data(x&255); data(right>>8); data(right&255);
    command(0x2b); data(y>>8); data(y&255); data(bottom>>8); data(bottom&255);
    command(0x2c);
}
static void wait_span(void) { while(SPAN_STATUS&1u) {} }
static void span(unsigned n,uint16_t color,void *context) {
    (void)context; wait_span(); SPAN_COLOR=color; SPAN_COUNT=n;
}
static void begin_line(unsigned x,unsigned y,unsigned n,void *context) {
    (void)x; (void)n; (void)context;
    if(!y) { wait_span(); window(0,0,ROAD_WIDTH,ROAD_HEIGHT); }
}
static void fill(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color) {
    unsigned count=w*h;
    wait_span(); window(x,y,w,h);
    if(count>65535) { span(38400,color,0); count-=38400; }
    span(count,color,0); wait_span();
}
/* Small, code-native 5x7 alphabet for the board menu; no bitmap screens. */
static void text(unsigned x,unsigned y,const char *s,unsigned scale,uint16_t color) {
    static const char alphabet[]="LEVSTAR0123456789DOMHI";
    static const uint8_t font[][5]={
        {127,64,64,64,64},
        {127,73,73,73,65},
        {31,32,64,32,31},
        {38,73,73,73,50},
        {1,1,127,1,1},
        {126,9,9,9,126},
        {127,9,25,41,70},
        {62,81,73,69,62},
        {0,66,127,64,0},
        {98,81,73,73,70},
        {34,65,73,73,54},
        {24,20,18,127,16},
        {39,69,69,69,57},
        {60,74,73,73,48},
        {1,113,9,5,3},
        {54,73,73,73,54},
        {6,73,73,41,30},
        {127,65,65,34,28},
        {62,65,65,65,62},
        {127,2,12,2,127},
        {127,8,8,8,127},
        {0,65,127,65,0},
    };
    while(*s) {
        unsigned i,col,row;
        for(i=0;alphabet[i] && alphabet[i]!=*s;i++) {}
        if(alphabet[i]) for(col=0;col<5;col++) for(row=0;row<7;row++)
            if(font[i][col]&(1u<<row)) fill(x+col*scale,y+row*scale,scale,scale,color);
        x+=6*scale; s++;
    }
}
#define MENU_SKY 0x347fu
static void menu_number(unsigned x,unsigned y,unsigned value,unsigned scale,uint16_t color) {
    char number[3]={'0','0',0};
    volatile unsigned n=value;
    while(n>=10) { n-=10; number[0]++; }
    number[1]=(char)('0'+n);
    text(x,y,number,scale,color);
}
static void menu_card(unsigned x,unsigned y,unsigned w,unsigned h,int front) {
    fill(x+4,y+6,w,h,0x2357);
    fill(x+3,y,w-6,h,front?0xffff:0xa6bf);
    fill(x,y+3,w,h-6,front?0xffff:0xa6bf);
    fill(x+4,y+4,w-8,h-8,front?0x1b18:0x2c5b);
    fill(x+9,y+9,w-18,3,front?0x5e5f:0x4d5e);
}
static int menu_hills(unsigned index,unsigned card) {
    return (card?sd_levels_id(index):levels[index]->id)==4;
}
/* ---- Slide-potentiometer input -------------------------------------------
 * The ADC pins share their package balls with plain IO, and the board routes
 * seven of those channels to header P2, so which one carries the wiper is not
 * known in advance. The channel that has travelled the most wins, and it only
 * becomes usable once its own range is wide enough to be a hand sweep rather
 * than pin noise. That same range is the calibration: the two ends of the
 * track are whatever the board's reference actually produces, so no absolute
 * voltage has to be trusted. */
#define ADC_CHANNELS 7u
#define ADC_MIN_SPAN 300u   /* about 0.07 VREF; below this it is pin noise */
static uint16_t adc_level[ADC_CHANNELS];
static uint16_t adc_low[ADC_CHANNELS],adc_high[ADC_CHANNELS];
static uint8_t adc_channel,adc_live,adc_primed,adc_manual;
static void adc_poll(void) {
    unsigned i;
    for(i=0;i<ADC_CHANNELS;i++) {
        unsigned raw=(unsigned)ADC_LEVEL(i+1)&0xfffu,v;
        if(!adc_primed) { adc_level[i]=(uint16_t)raw; adc_low[i]=adc_high[i]=(uint16_t)raw; }
        /* One frame of smoothing: the slide moves much faster than the noise
         * it has to hide, so a light filter keeps the pot feeling direct. */
        v=((unsigned)adc_level[i]*3u+raw)>>2;
        adc_level[i]=(uint16_t)v;
        if(v<adc_low[i]) adc_low[i]=(uint16_t)v;
        if(v>adc_high[i]) adc_high[i]=(uint16_t)v;
    }
    adc_primed=1;
}
static unsigned adc_span(unsigned channel) {
    return (unsigned)adc_high[channel]-(unsigned)adc_low[channel];
}
static unsigned adc_position(void);
static void adc_select(void) {
    unsigned i,best=0,best_span=0;
    if(adc_manual) { adc_live=adc_span(adc_channel)>=ADC_MIN_SPAN; return; }
    for(i=0;i<ADC_CHANNELS;i++) {
        unsigned span=adc_span(i);
        if(span>best_span) { best_span=span; best=i; }
    }
    if(best_span<ADC_MIN_SPAN) { adc_live=0; return; }
    /* Switching needs a clear win, so a stray noise channel cannot steal the
     * input mid-game. */
    if(best!=adc_channel &&
       (adc_span(adc_channel)<ADC_MIN_SPAN || best_span>adc_span(adc_channel)*2u))
        adc_channel=(uint8_t)best;
    adc_live=1;
}
/* Menu KEY12 walks the seven channels by hand. The automatic pick is right in
 * the normal case, but this makes a wrong guess fixable without a rebuild:
 * cycle until the readout moves with the slider. */
static void adc_cycle(void) {
    unsigned next=(unsigned)adc_channel+1u;
    adc_manual=1;
    adc_channel=(uint8_t)(next==ADC_CHANNELS?0u:next);  /* no modulo: it links a divide */
    adc_live=adc_span(adc_channel)>=ADC_MIN_SPAN;
}
/* Session bests live in RAM only; the board offers no persistent storage. */
static uint8_t best_pct[24],best_gems[24];
static uint8_t scored;
#define HUD_PCT_GLYPH {5,1,2,4,5}
/* One shared copy of the percent sign: the menu's best-score badge and the
 * result readout both draw it. */
static const uint8_t hud_pct_rows[5]=HUD_PCT_GLYPH;
/* Live HUD values are latched once per frame and composited into the road's
 * own scanline, so the bar and the counter are written exactly once and stop
 * flickering against the freshly painted road underneath. */
static struct { unsigned done,gems,pct,state,vol; int paused,vol_show; } hud;
static uint16_t *hud_pixels;   /* non-zero while a scanline is being composited */
static unsigned hud_row;
static void hud_digit(unsigned x,unsigned y,unsigned digit);
static void hud_glyph(unsigned x,unsigned y,const uint8_t *rows,uint16_t on,uint16_t off);
static void hud_gem_icon(unsigned x,unsigned y);
static void hud_volume_bars(void);
static unsigned hud_tens(unsigned v);
static uint32_t hud_divide(uint32_t n,uint32_t d);
static void hud_rect(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color) {
    unsigned i;
    if(!hud_pixels) { fill(x,y,w,h,color); return; }
    if(y!=hud_row) return;   /* Only the scanline being composited is touched. */
    for(i=0;i<w && x+i<ROAD_WIDTH;i++) hud_pixels[x+i]=color;
}
static void draw_title(void) {
    unsigned i,x=(240-TITLE_WIDTH)/2;
    wait_span(); window(x,18,TITLE_WIDTH,TITLE_HEIGHT);
    for(i=0;i<sizeof(title_runs);i++)
        span(title_runs[i]>>2,title_palette[title_runs[i]&3u],0);
    wait_span();
    window(x+TITLE_BALL_X,18+TITLE_BALL_Y,TITLE_BALL_SIZE,TITLE_BALL_SIZE);
    for(i=0;i<sizeof(title_ball_runs);i++) {
        unsigned run=title_ball_runs[i],color=run&7u;
        span(run>>3,color?title_ball_palette[color]:MENU_SKY,0);
    }
    wait_span();
    /* Two-tone accent under the fixed, italic logo. */
    fill(66,105,108,2,0x86ff);
    fill(100,105,40,2,0xff29);
}
/* Bring-up readout: which ADC channel won and how far along its own swing the
 * wiper currently sits, as two decimal digits of percent. Grey means the
 * sweep was too narrow to drive the ball yet. Percent rather than the raw
 * sample because the useful question is "does the whole slider travel?", and
 * that is exactly what this number answers: sweep end to end and it should
 * read 00 and 99.
 *
 * The line is the only thing on the menu that changes by itself, so it
 * repaints its own small strip and only when channel, state or position
 * moved: the 2026-10-29 build instead redrew the whole menu every 250 ms,
 * and the full-screen sky fill inside draw_menu flashed on the panel at
 * 4 Hz. */
static void menu_adc_line(int force) {
    static uint8_t seen,last_channel,last_live;
    static uint16_t last_value;
    char line[3];
    unsigned pos=(unsigned)hud_divide(adc_position(),10u);
    uint16_t color=adc_live?(adc_manual?0xffe0u:0x07e0u):0x9cd3u;
    if(pos>99u) pos=99u;
    if(!force && seen && last_channel==adc_channel && last_live==adc_live &&
       last_value==(uint16_t)pos) return;
    seen=1; last_channel=adc_channel; last_live=adc_live; last_value=(uint16_t)pos;
    line[0]='A';
    line[1]=(char)('1'+adc_channel);
    line[2]=0;
    fill(2,281,44,12,MENU_SKY);
    text(4,283,line,1,color);
    menu_number(16,283,pos,1,color);
}
/* ---- Volume -------------------------------------------------------------
 * KEY0 steps down, KEY1 steps up. Both are the matrix keys nothing else uses,
 * and both repeat: the first press lands at once, then every 120 ms while the
 * key is held, so the whole 0..10 range is a one-second sweep instead of ten
 * separate jabs. `vol_stamp` drives the on-screen readout -- the game HUD
 * shows a bar for 1.2 s after a change, the menu keeps a numeric line. */
static uint32_t vol_stamp,vol_repeat;
static uint16_t keys_player_frame(uint16_t bits);   /* defined below, in game-frame space */
static int volume_keys(uint32_t now) {
    uint32_t held=keys_player_frame(KEY_HELD);
    unsigned before=sound_volume_get();
    int dir=(held&2u?1:0)-(held&1u?1:0);
    if(!dir) { vol_repeat=0; return 0; }
    if((int32_t)(now-vol_repeat)<0) return 0;
    sound_volume(dir);
    vol_repeat=now+120u;
    if(sound_volume_get()==before) return 0;
    vol_stamp=now;
    return 1;
}
/* Called only when the value actually moved. The menu uses the same painter
 * as the game: hud_rect() writes straight to the panel whenever no scanline
 * is being composited, so one bar serves both screens and there is no second
 * layout to keep in sync. */
static void menu_volume_bar(void) {
    hud.vol=sound_volume_get();
    hud_volume_bars();
}
static void draw_menu(unsigned selected,unsigned card,unsigned error) {
    unsigned total=card?card:LEVEL_COUNT;
    fill(0,0,240,320,MENU_SKY);
    draw_title();
    /* Back cards first. Side labels remain visible beside the front card. */
    if(selected) {
        menu_card(4,130,88,108,0);
        if(menu_hills(selected-1,card)) text(12,170,"HILLS",1,0xc73f);
        else menu_number(12,164,selected,3,0xc73f);
    }
    if(selected+1<total) {
        menu_card(148,130,88,108,0);
        if(menu_hills(selected+1,card)) text(195,170,"HILLS",1,0xc73f);
        else menu_number(192,164,selected+2,3,0xc73f);
    }
    menu_card(54,150,132,132,1);
    if(menu_hills(selected,card)) text(62,198,"HILLS",4,0xffff);
    else menu_number(78,186,selected+1,7,0xffff);
    /* Session bests in the front card corners: progress top-right, gems
     * bottom-center; hidden until the level has been played once. */
    if(best_pct[selected]) {
        unsigned p=best_pct[selected];
        fill(148,154,34,13,0x1b18u);
        hud_digit(153,156,hud_tens(p));
        hud_digit(160,156,p-hud_tens(p)*10u);
        hud_glyph(167,156,hud_pct_rows,0xffe0u,0x1b18u);
    }
    if(best_gems[selected]) {
        unsigned g=best_gems[selected];
        fill(94,262,52,13,0x1b18u);
        hud_gem_icon(99,264);
        hud_digit(110,264,hud_tens(g));
        hud_digit(117,264,g-hud_tens(g)*10u);
    }
    text(card?208:196,299,card?"SD":"ROM",2,error?0xf800u:card?0x07e0u:0xffffu);
    /* Pack revision and mechanism count of the highlighted level. A level
     * whose table is missing reads M00, which separates a stale card file
     * from a rendering fault without a debugger attached. */
    if(card) {
        char revision[3]={'V',(char)('0'+sd_levels_format()),0};
        unsigned mechs=sd_levels_mech_count(selected);
        text(4,299,revision,2,0x07ffu);
        text(30,299,"M",2,0x9cd3u);
        menu_number(42,299,mechs,2,mechs?0x07ffu:0xf800u);
    }
    menu_adc_line(1);

}
static int start_level(unsigned selected,unsigned card,Road *r,uint32_t *camera) {
    if(card) return sd_levels_begin(selected,r,camera);
    level_load(r,levels[selected],camera);
    return 1;
}
/* HUD: a 2-pixel progress bar across the top plus a gem counter. Both are
 * written into the road's scanline instead of over it, so a frame produces a
 * single write per pixel and the overlay holds still. */
static uint32_t hud_divide(uint32_t n,uint32_t d) {
    uint32_t q=0,bit=1;
    if(!d) return 0;
    while(d<=n && !(d&0x80000000u)) { d<<=1; bit<<=1; }
    while(bit) { if(n>=d) { n-=d; q|=bit; } d>>=1; bit>>=1; }
    return q;
}
/* The enclosure mounts the panel upside down, so the raster is flipped in
 * hardware (MADCTL MX|MY). The controls have to be flipped with it: a sweep
 * that would have moved the ball left now has to move it right, otherwise
 * the picture and the hand disagree. Set this to 0 to undo both at once. */
#define INPUT_ROTATED 1
/* The enclosure turned 180 degrees after the keypad was wired, so the key the
 * player sees on the left is the one the matrix calls KEY14. The pair is
 * swapped once, on the way in, rather than at each use: the menu's
 * previous/next used to keep the factory sense while the ball was corrected,
 * which is exactly the "the keypad needs reversing too" report. Action keys
 * (KEY4/8/12) carry no left-right meaning, so they stay put. */
static uint16_t keys_player_frame(uint16_t bits) {
#if INPUT_ROTATED
    uint16_t swapped=bits&(uint16_t)~((1u<<6)|(1u<<14));
    if(bits&(1u<<6)) swapped|=1u<<14;
    if(bits&(1u<<14)) swapped|=1u<<6;
    return swapped;
#else
    return bits;
#endif
}
/* Slide position over the channel's own observed swing, 0..1000. */
static unsigned adc_position(void) {
    unsigned span=adc_span(adc_channel),pos;
    if(span<ADC_MIN_SPAN) return 500u;
    pos=(unsigned)hud_divide(
        (uint32_t)((unsigned)adc_level[adc_channel]-(unsigned)adc_low[adc_channel])*1000u,span);
    return pos>1000u?1000u:pos;
}
/* Steering auto-calibrates to the pot's real swing: min/max of the selected
 * channel are tracked continuously and mapped onto the whole road, so the
 * full travel is usable whatever the hardware delivers (the EG4S20
 * converter's range compresses at both rails -- the board's own manual rates
 * it 0.33..2.97 V -- so a raw full-scale assumption undershoots, which the
 * 2026-11-01 build got wrong: a full sweep then moved the ball two lanes).
 * Sweep the slider once after power-up; until the span is wide enough the
 * readout stays grey and the keys drive.
 *
 * The outer 1.2 percent of the travel snaps to the end stop. Without it the
 * lanes at either edge need the wiper pushed into its mechanical limit, so
 * the reachable band felt narrower than the road and the last centimetre of
 * the track did nothing -- reported as "the usable part of the slider is too
 * small". The slew keeps the 1 ms collision stepping seeing the whole
 * crossing; 3 Q8/ms tracks a fast sweep instead of trailing it. */
static void adc_steer(Road *r,uint32_t ms) {
    unsigned pos=adc_position();
    int32_t target,delta,step,travel=ROAD_CAMERA_MAX-ROAD_CAMERA_MIN;
    if(!adc_live || adc_span(adc_channel)<ADC_MIN_SPAN) return;
#if INPUT_ROTATED
    pos=1000u-pos;
#endif
    if(pos<=12u) target=(int32_t)ROAD_CAMERA_MIN;
    else if(pos>=988u) target=(int32_t)ROAD_CAMERA_MAX;
    else target=(int32_t)ROAD_CAMERA_MIN+
        (int32_t)hud_divide((uint32_t)(pos-12u)*(uint32_t)travel,976u);
    if(target>(int32_t)ROAD_CAMERA_MAX) target=(int32_t)ROAD_CAMERA_MAX;
    delta=target-(int32_t)r->camera_x_q8;
    step=(int32_t)(ms*3u);          /* 11.7 cells/s: keeps up with a hand sweep */
    if(delta>step) delta=step;
    if(delta<-step) delta=-step;
    /* Three Q8 units is one hundredth of a cell, about a third of a pixel at
     * the nearest row: below that the move is invisible but the wiper's own
     * quantisation keeps flipping its sign, and a camera that rocks by a pixel
     * every frame reads as a shudder across the whole picture. */
    if(delta>=-3 && delta<=3) return;
    r->camera_x_q8=(uint16_t)((int32_t)r->camera_x_q8+delta);
}
static const uint8_t hud_digits[10][5]={
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
};
/* Comparison ladders: -Oz rewrites subtract loops back into EABI division
 * calls, and this image links without those helpers. */
static unsigned hud_tens(unsigned v) {
    if(v>=90) return 9; if(v>=80) return 8; if(v>=70) return 7; if(v>=60) return 6;
    if(v>=50) return 5; if(v>=40) return 4; if(v>=30) return 3; if(v>=20) return 2;
    return v>=10?1u:0u;
}
static void hud_glyph(unsigned x,unsigned y,const uint8_t *rows,uint16_t on,uint16_t off) {
    unsigned row;
    for(row=0;row<5;row++) {
        unsigned bits=rows[row],col;
        for(col=0;col<3;col++)
            hud_rect(x+col*2,y+row*2,2,2,(bits>>(2-col))&1u?on:off);
    }
}
static void hud_digit(unsigned x,unsigned y,unsigned digit) {
    hud_glyph(x,y,hud_digits[digit],0xffffu,0x0000u);
}
static void hud_number(unsigned x,unsigned y,unsigned v) {
    unsigned tens=hud_tens(v);
    hud_digit(x,y,tens);
    hud_digit(x+7,y,v-tens*10u);
}
static void hud_gem_icon(unsigned x,unsigned y) {
    static const uint8_t widths[5]={1,3,5,3,1};
    unsigned row;
    for(row=0;row<5;row++)
        hud_rect(x+(5-widths[row])/2,y+row*2,widths[row],2,0x07ffu);
}
static void hud_counter(unsigned gems) {
    hud_rect(3,4,28,16,0x0000u);
    hud_gem_icon(6,5);
    hud_number(16,5,gems);
}
static unsigned hud_pct(const Road *r,uint32_t camera) {
    uint32_t z=camera+r->depth_q8[ROAD_BALL_Y];
    uint32_t total=(uint32_t)r->level->row_count*256u;
    unsigned pct=total?(unsigned)hud_divide(z*100u,total):0u;
    return pct>100?100u:pct;
}
/* Death/clear readout: gems reached and percentage, the feedback the
 * original shows on its result screen. */
static void hud_result(void) {
    unsigned value=hud.pct,x;
    hud_rect(84,6,72,14,0x0000u);
    hud_gem_icon(88,8);
    hud_number(97,8,hud.gems);
    x=118;
    if(value>=100) { hud_digit(x,8,1); hud_digit(x+7,8,0); hud_digit(x+14,8,0); x+=21; }
    else { hud_number(x,8,value); x+=14; }
    hud_glyph(x,8,hud_pct_rows,0xffe0u,0x0000u);
}
/* Ten-step master-volume bar, drawn in the HUD band but at the opposite end
 * from the gem counter. It only exists for 1.2 s after a change: every frame
 * repaints the whole road underneath, so letting the scanline stop drawing it
 * is all the erasing that is needed. Colour follows the level -- red for the
 * bottom three steps, amber to six, green above. The track is opaque black
 * rather than a grey tint: the scene's sun or moon sits right there, and a
 * see-through track disappears into the disc. */
static void hud_volume_bars(void) {
    unsigned w=hud.vol*4u;
    hud_rect(196,8,39,6,0x0000u);
    if(w) hud_rect(196,8,w-1,6,hud.vol>6u?0x07e0u:hud.vol>3u?0xffe0u:0xf800u);
}
/* One scanline of HUD: called by the road renderer while it still owns the
 * line, so the caller must have latched `hud` for this frame already. */
static void hud_overlay(uint16_t *pixels,unsigned y,void *context) {
    (void)context;
    hud_pixels=pixels; hud_row=y;
    if(y<4) {
        if(hud.state) hud_rect(0,y,ROAD_WIDTH,1,hud.state==GAME_CLEAR?0x07e0u:0xf800u);
        else if(y<2) {
            hud_rect(0,y,hud.done,1,0xffe0u);
            if(hud.done<ROAD_WIDTH) hud_rect(hud.done,y,ROAD_WIDTH-hud.done,1,0x18e3u);
        }
    }
    if(y>=4 && y<20) {
        hud_counter(hud.gems);
        if(hud.state) hud_result();
        else if(hud.paused) {
            hud_rect(111,8,5,12,0xffffu);
            hud_rect(124,8,5,12,0xffffu);
        }
        if(hud.vol_show) hud_volume_bars();
    }
    hud_pixels=0;
}
static const RoadOverlay hud_layer={20,hud_overlay,0};
static void hud_latch(const Road *r,uint32_t camera,unsigned state,int paused) {
    uint32_t z=camera+r->depth_q8[ROAD_BALL_Y];
    uint32_t total=(uint32_t)r->level->row_count*256u;
    unsigned done=total?(unsigned)hud_divide(z*ROAD_WIDTH,total):0u;
    hud.done=done>ROAD_WIDTH?ROAD_WIDTH:done;
    hud.gems=r->gems_got;
    hud.state=state;
    hud.paused=paused;
    hud.vol=sound_volume_get();
    hud.vol_show=(uint32_t)(milliseconds-vol_stamp)<1200u;
    hud.pct=state?hud_pct(r,camera):0u;
}
static void lcd_init(void) {
    static const uint8_t config[]={
        0xcf,3,0,0xc1,0x30, 0xed,4,0x64,3,0x12,0x81,
        0xe8,3,0x85,0x10,0x7a, 0xcb,5,0x39,0x2c,0,0x34,2,
        0xf7,1,0x20, 0xea,2,0,0, 0xc0,1,0x1b, 0xc1,1,1,
        /* 0x08 portrait is 90 degrees CCW from the previous 0x68 landscape.
         * MADCTL now sets MX and MY as well (0xc8), which flips both address
         * axes: the whole raster comes out rotated 180 degrees for the
         * enclosure, at no cost and with no change to any drawing code. */
        0xc5,2,0x30,0x30, 0xc7,1,0xb7, 0x36,1,0xc8, 0x3a,1,0x55,
        0xb1,2,0,0x1a, 0xb6,2,0x0a,0xa2, 0xf2,1,0, 0x26,1,1,
        0xe0,15,0x0f,0x2a,0x28,8,0x0e,8,0x54,0xa9,0x43,0x0a,0x0f,0,0,0,0,
        0xe1,15,0,0x15,0x17,7,0x11,6,0x2b,0x56,0x3c,5,0x10,0x0f,0x3f,0x3f,0x0f
    };
    unsigned i=0,n;
    CS=1; RS=1; WR=1; RD=1; BACKLIGHT=0;
    RESET=0; wait_ms(20); RESET=1; wait_ms(120);
    command(1); wait_ms(150);
    while(i<sizeof(config)) { command(config[i++]); n=config[i++]; while(n--) data(config[i++]); }
    command(0x11); wait_ms(120); command(0x29); wait_ms(20);
    window(0,0,ROAD_WIDTH,ROAD_HEIGHT);
#ifndef ROAD_PROFILE
    span(38400,ROAD_BG,0); span(38400,ROAD_BG,0); wait_span();
#endif
    BACKLIGHT=1;
}
int main(void) {
    /* Startup branches directly here; explicitly initialize mutable state. */
    uint32_t camera=0,last_time,frame_start,keys;
    int paused=0,points=0,vol_changed=0;
    unsigned state=GAME_RUNNING,selected=0,card_count=0,sd_error=0;
    int menu=1,menu_dirty=1;
    uint32_t menu_stamp=0;
    const RoadSink sink={begin_line,span,0};
    milliseconds=0; key_events=0; road_frame_count=0; road_last_frame_ms=0; art_sd_loaded=0;
    road_init(&road);
    road.overlay=&hud_layer;
    level_load(&road,&level_one,&camera);
    REG(0xe000e014)=31999; REG(0xe000e018)=0; REG(0xe000e010)=7;
    REG(0xe000e100)=1;
    lcd_init(); sound_init(); last_time=milliseconds;
#ifdef ROAD_PROFILE
    {
        unsigned scene;
        for(scene=0;scene<2;scene++) {
            level_load(&road,&level_one,&camera);
            if(scene) camera=21*256-road.depth_q8[ROAD_BALL_Y]-256;
            REG(0x20001800)=scene*4+1;
            (void)level_step(&road,&camera,0,33);
            REG(0x20001800)=scene*4+2;
            road_draw(&road,camera,points,&sink); wait_span();
            REG(0x20001800)=scene*4+4;
        }
        for(;;) {}
    }
#endif
#ifdef ROAD_BENCHMARK
    road_baseline_init();
    BENCH_MARK=1;
    road_baseline_frame(33,1,begin_line,span,0); wait_span();
    BENCH_MARK=2;
    road_draw_calculated(&road,33,1,&sink); wait_span();
    BENCH_MARK=3;
    road_draw(&road,33,1,&sink); wait_span();
    BENCH_MARK=4;
    for(;;) {}
#endif
#if !defined(ROAD_PROFILE) && !defined(ROAD_BENCHMARK)
    card_count=sd_levels_mount(); appearance_load(card_count!=0);
#endif
    for(;;) {
        frame_start=milliseconds;
        __asm volatile("cpsid i" ::: "memory");
        keys=keys_player_frame(key_events); key_events=0;
        __asm volatile("cpsie i" ::: "memory");
        adc_poll(); adc_select();
        /* Volume is a small self-contained strip, so it repaints itself
         * rather than marking the menu dirty: a full repaint per repeat step
         * would flash the whole sky four times a second. */
        vol_changed=volume_keys(milliseconds);
        if(menu) {
            if(vol_changed) menu_volume_bar();
            if(keys&(1u<<4)) {
                card_count=sd_levels_mount(); appearance_load(card_count!=0); sd_error=!card_count;
                selected=0; menu_dirty=1;
            }
            if(keys&(1u<<12)) { adc_cycle(); menu_dirty=1; }
            if((keys&(1u<<6)) && !(keys&(1u<<14)) && selected) {
                selected--; menu_dirty=1;
            }
            if((keys&(1u<<14)) && !(keys&(1u<<6)) && selected+1<(card_count?card_count:LEVEL_COUNT)) {
                selected++; menu_dirty=1;
            }
            if(keys&(1u<<8)) {
                if(!start_level(selected,card_count,&road,&camera)) {
                    card_count=0; sd_error=1; selected=0; menu_dirty=1;
                    continue;
                }
                state=GAME_RUNNING; paused=0; menu=0; scored=0;
                last_time=frame_start=milliseconds; keys=0;
            } else {
                /* Full repaint only on a real state change; the ADC line is
                 * the sole self-updating element and repaints its own strip
                 * (throttled to 10 Hz) instead of dragging the whole menu
                 * through a sky fill every 250 ms. */
                if(menu_dirty) {
                    draw_menu(selected,card_count,sd_error);
                    menu_dirty=0; menu_stamp=milliseconds;
                } else if((uint32_t)(milliseconds-menu_stamp)>=100u) {
                    menu_stamp=milliseconds;
                    menu_adc_line(0);
                }
                /* The menu has no music at all: the sequencer is told to
                 * stand down while the level picker is up, so the beat only
                 * exists while the road is moving. */
                sound_frame((unsigned)(milliseconds-last_time),0);
                last_time=milliseconds;
                while((uint32_t)(milliseconds-frame_start)<10) {}
                continue;
            }
        } else if(keys&(1u<<12)) {
            menu=1; menu_dirty=1;
            continue;
        }
        if(keys&(1u<<8)) paused=!paused;
        if(keys&(1u<<4)) {
            if(!start_level(selected,road.level==&sd_level,&road,&camera)) {
                menu=1; menu_dirty=1; card_count=0; sd_error=1; selected=0;
                continue;
            }
            state=GAME_RUNNING; paused=0; scored=0; last_time=frame_start;
        }
        {
            unsigned held=keys_player_frame(KEY_HELD);
            int direction=(held&(1u<<14)?1:0)-(held&(1u<<6)?1:0);
            /* The slider steers only while neither arrow key is held, so the
             * keys stay usable as a fallback and never fight the pot. */
            if(!paused && state==GAME_RUNNING && !(held&((1u<<14)|(1u<<6))))
                adc_steer(&road,frame_start-last_time);
            if(!paused && state==GAME_RUNNING)
                state=level_step(&road,&camera,direction,frame_start-last_time);
            if(state==GAME_FELL || state==GAME_HIT) {
                if(!road.death_kind) { road.death_kind=(uint8_t)state; sound_sfx(SFX_DEATH); }
                else {
                    unsigned t=road.death_ms+frame_start-last_time;
                    road.death_ms=(uint16_t)(t>800?800:t);
                }
            }
        }
        /* Drain the events the simulation raised this frame. A bit word
         * rather than a queue: level_step may run a hundred 1 ms steps, so
         * three gems in one frame are one ding, which is the right call. */
        if(road.sfx) {
            unsigned sfx=road.sfx;
            road.sfx=0;
            if(sfx&SFX_GEM) sound_sfx(SFX_GEM);
            if(sfx&SFX_JUMP) sound_sfx(SFX_JUMP);
            if(sfx&SFX_HAMMER) sound_sfx(SFX_HAMMER);
            if(sfx&SFX_LASER) sound_sfx(SFX_LASER);
        }
        sound_frame(frame_start-last_time,!paused && state==GAME_RUNNING);
        last_time=frame_start;
        if(road.level==&sd_level && !sd_levels_prepare(&road,camera)) {
            menu=1; menu_dirty=1; card_count=0; sd_error=1; selected=0;
            continue;
        }
        /* Latch the HUD for this frame, then let one road draw carry both the
         * scene and the overlay. Nothing is painted twice. */
        hud_latch(&road,camera,state,paused);
        if(state!=GAME_RUNNING && !scored) {
            unsigned pct=hud.pct;
            if(pct>99u) pct=99u;
            scored=1;
            if(state==GAME_CLEAR) sound_sfx(SFX_CLEAR);
            if(selected<24u) {
                if(pct>best_pct[selected]) best_pct[selected]=(uint8_t)pct;
                if(road.gems_got>best_gems[selected]) best_gems[selected]=road.gems_got;
            }
        }
        road_draw(&road,camera,points,&sink); wait_span();
        road_last_frame_ms=milliseconds-frame_start; road_frame_count++;
        while((uint32_t)(milliseconds-frame_start)<33) {}
    }
}
