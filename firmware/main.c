#include <stdint.h>
#include "road.h"
#include "level.h"
#include "sd_levels.h"
#include "menu_title.h"
#include "appearance.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define KEY REG(0x40000000)
#define KEY_HELD REG(0x40000004)
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
    text(card?208:196,299,card?"SD":"ROM",2,error?0xf800u:card?0x07e0u:0xffffu);
}
static int start_level(unsigned selected,unsigned card,Road *r,uint32_t *camera) {
    if(card) return sd_levels_begin(selected,r,camera);
    level_load(r,levels[selected],camera);
    return 1;
}
static void lcd_init(void) {
    static const uint8_t config[]={
        0xcf,3,0,0xc1,0x30, 0xed,4,0x64,3,0x12,0x81,
        0xe8,3,0x85,0x10,0x7a, 0xcb,5,0x39,0x2c,0,0x34,2,
        0xf7,1,0x20, 0xea,2,0,0, 0xc0,1,0x1b, 0xc1,1,1,
        /* 0x08 portrait is 90 degrees CCW from the previous 0x68 landscape. */
        0xc5,2,0x30,0x30, 0xc7,1,0xb7, 0x36,1,0x08, 0x3a,1,0x55,
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
    int paused=0,points=0;
    unsigned state=GAME_RUNNING,selected=0,card_count=0,sd_error=0;
    int menu=1,menu_dirty=1;
    const RoadSink sink={begin_line,span,0};
    milliseconds=0; key_events=0; road_frame_count=0; road_last_frame_ms=0; art_sd_loaded=0;
    road_init(&road);
    level_load(&road,&level_one,&camera);
    REG(0xe000e014)=31999; REG(0xe000e018)=0; REG(0xe000e010)=7;
    REG(0xe000e100)=1;
    lcd_init(); last_time=milliseconds;
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
        keys=key_events; key_events=0;
        __asm volatile("cpsie i" ::: "memory");
        if(menu) {
            if(keys&(1u<<4)) {
                card_count=sd_levels_mount(); appearance_load(card_count!=0); sd_error=!card_count;
                selected=0; menu_dirty=1;
            }
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
                state=GAME_RUNNING; paused=0; menu=0;
                last_time=frame_start=milliseconds; keys=0;
            } else {
                if(menu_dirty) {
                    draw_menu(selected,card_count,sd_error); menu_dirty=0;
                }
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
            state=GAME_RUNNING; paused=0; last_time=frame_start;
        }
        {
            unsigned held=KEY_HELD;
            int direction=(held&(1u<<14)?1:0)-(held&(1u<<6)?1:0);
            if(!paused && state==GAME_RUNNING)
                state=level_step(&road,&camera,direction,frame_start-last_time);
            if(state==GAME_FELL || state==GAME_HIT) {
                if(!road.death_kind) road.death_kind=(uint8_t)state;
                else {
                    unsigned t=road.death_ms+frame_start-last_time;
                    road.death_ms=(uint16_t)(t>800?800:t);
                }
            }
        }
        last_time=frame_start;
        if(road.level==&sd_level && !sd_levels_prepare(&road,camera)) {
            menu=1; menu_dirty=1; card_count=0; sd_error=1; selected=0;
            continue;
        }
        road_draw(&road,camera,points,&sink); wait_span();
        if(state!=GAME_RUNNING) {
            window(0,0,ROAD_WIDTH,4);
            span(ROAD_WIDTH*4,state==GAME_CLEAR?0x07e0:0xf800,0);
            wait_span();
        }
        road_last_frame_ms=milliseconds-frame_start; road_frame_count++;
        while((uint32_t)(milliseconds-frame_start)<33) {}
    }
}
