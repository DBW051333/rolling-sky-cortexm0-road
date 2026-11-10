#include <stdint.h>
#include "road.h"
#include "level.h"
#include "sd_levels.h"
#include "menu.h"
#include "player.h"
#include "appearance.h"
#include "sound.h"
#include "frame_clock.h"
#ifdef MENU_VIDEO
#include "menu_video.h"
#endif
#define REG(a) (*(volatile uint32_t *)(a))
#define KEY REG(0x40000000)
#define KEY_HELD REG(0x40000004)
/* Slide potentiometer on header P2, converted by the FPGA's internal 12-bit
 * ADC. Channels 1..7 are the seven ADC pins on that socket; the keyboard
 * peripheral exports them at 0x008 + 4*(channel-1). */
#ifndef ADC_LEVEL
#define ADC_LEVEL(ch) REG(0x40000008u+4u*((ch)-1u))
#endif
#define VIDEO_X REG(0x40050000)
#define VIDEO_Y REG(0x40050004)
#define VIDEO_BEGIN REG(0x40050008)
#define SPAN_COLOR REG(0x40050080)
#define SPAN_COUNT REG(0x40050084)
#define SPAN_STATUS REG(0x40050088)
#define VIDEO_FLUSH REG(0x40050090)
#define VIDEO_PIXELS REG(0x400500a0)
#define VIDEO_TARGET REG(0x400500b0)
#define VIDEO_CLOCK REG(0x400500b4)
#define VIDEO_STALL REG(0x400500b8)
#define VIDEO_PRESSURE REG(0x400500bc)
#define VIDEO_PRESENT REG(0x400500f0)
#define VIDEO_DISPLAY_STATUS REG(0x400500f4)
volatile uint32_t milliseconds,key_events;
volatile uint32_t road_frame_count,road_last_frame_ms;
/* Cycle counts use the 32 MHz free-running video clock counter. Unsigned
 * differences remain valid across its approximately 134-second wrap. */
volatile struct {
    uint32_t draw_cycles,drain_cycles,present_cycles,stall_cycles,pressure_cycles;
    uint32_t logic_ms,frame_ms,fps_x10,submitted_frames,displayed_frames,scan_misses;
} game_video_stats;
static unsigned game_back;
static uint32_t game_draw_start,game_stall_start,game_pressure_start;
static uint32_t game_fps_stamp,game_fps_frames,game_present_base;
static uint32_t hud_divide(uint32_t n,uint32_t d);
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
/* Both menu and gameplay use the same 30 Hz budget. Slow frames continue
 * immediately; elapsed-time simulation remains independent of rendering. */
static void video_finish(void) {
    while(SPAN_STATUS&1u) {}
    VIDEO_FLUSH=1;
    while(!(SPAN_STATUS&8u)) {}
}
static void finish_frame(uint32_t start,unsigned *phase) {
    video_finish();
    unsigned budget=frame_period_ms(phase);
    road_last_frame_ms=milliseconds-start; road_frame_count++;
    while((uint32_t)(milliseconds-start)<budget) {}
}
static void game_video_begin(void) {
    uint32_t status=VIDEO_DISPLAY_STATUS;
    unsigned front=(status>>1)&3u;
    unsigned queued=status&1u?(status>>5)&3u:front;
    game_back=0;
    while(game_back==front || game_back==queued)game_back++;
    while(SPAN_STATUS&1u) {}
    VIDEO_TARGET=game_back==2u?4u:game_back;
}
static void game_video_present(void) {
    while(VIDEO_DISPLAY_STATUS&1u) {}
    VIDEO_PRESENT=game_back;
}
static void game_render_begin(uint32_t start) {
    game_video_begin();
    game_video_stats.logic_ms=milliseconds-start;
    game_draw_start=VIDEO_CLOCK;
    game_stall_start=VIDEO_STALL;game_pressure_start=VIDEO_PRESSURE;
}
static void game_render_end(uint32_t start) {
    uint32_t stamp=VIDEO_CLOCK,now,elapsed;
    game_video_stats.draw_cycles=stamp-game_draw_start;
    video_finish();
    game_video_stats.drain_cycles=VIDEO_CLOCK-stamp;
    game_video_stats.stall_cycles=VIDEO_STALL-game_stall_start;
    game_video_stats.pressure_cycles=VIDEO_PRESSURE-game_pressure_start;
    stamp=VIDEO_CLOCK;game_video_present();
    game_video_stats.present_cycles=VIDEO_CLOCK-stamp;
    now=milliseconds;elapsed=now-game_fps_stamp;
    game_video_stats.frame_ms=now-start;
    game_video_stats.submitted_frames++;
    game_video_stats.displayed_frames=REG(0x400500f8)-game_present_base;
    if(elapsed>=1000u) {
        uint32_t displayed=REG(0x400500f8);
        game_video_stats.fps_x10=hud_divide((displayed-game_fps_frames)*10000u,elapsed);
        game_fps_stamp=now;game_fps_frames=displayed;
    }
    game_video_stats.scan_misses=REG(0x400500fc);
}
static void window(unsigned x,unsigned y,unsigned w,unsigned h) {
    VIDEO_X=((x+w-1u)<<16)|x;
    VIDEO_Y=((y+h-1u)<<16)|y;
    VIDEO_BEGIN=1;
}
static void wait_span(void) { while(SPAN_STATUS&1u) {} }
static void span(unsigned n,uint16_t color,void *context) {
    (void)context; wait_span();
    if(n<=65535u)REG(0x40050070)=((uint32_t)color<<16)|n;
    else { SPAN_COLOR=color;SPAN_COUNT=n; }
}
static void sprite_prepare(int left,int top,unsigned red,void *context) {
    (void)context;wait_span();
    REG(0x40050074)=((uint32_t)(uint16_t)top<<16)|(uint16_t)left;
    REG(0x40050078)=red;
}
static void sprite_span(unsigned n,uint16_t color,void *context) {
    (void)context;wait_span();REG(0x4005006c)=((uint32_t)color<<16)|n;
}
static void sprite_load(void) {
    const uint16_t *palette=player_colors();
    unsigned y,i,count;
    wait_span();REG(0x4005009c)=0;
    for(i=0;i<8;i++) { wait_span();REG(0x400500ac)=palette[i]; }
    for(y=0;y<BALL_HEIGHT;y++) {
        const uint8_t *runs=player_row(y,&count);
        wait_span();REG(0x40050094)=y*64u;
        for(i=0;i<count;i++) {
            unsigned length=runs[i]>>3,ink=runs[i]&7u;
            while(length--) { wait_span();REG(0x40050098)=ink; }
        }
    }
}
static void tile_materials(const uint32_t *materials,void *context) {
    unsigned i;(void)context;wait_span();REG(0x40050044)=0;
    for(i=0;i<ROAD_COLS;i++) { wait_span();REG(0x40050048)=materials[i]; }
}
static void tile_row(unsigned scale,unsigned camera,unsigned flags,void *context) {
    (void)context;wait_span();REG(0x40050060)=(scale<<16)|camera;
    REG(0x40050064)=flags;
}
/* Shared scanlines are four-byte aligned and contain 360 pixels. */
static void stream_pixels(const uint16_t *pixels,unsigned count,void *context) {
    typedef uint32_t PixelPair __attribute__((may_alias));
    const PixelPair *words=(const PixelPair *)pixels;
    unsigned pairs=count>>1;
    (void)context;
    while(pairs) {
        unsigned batch=pairs>8?8:pairs,n=batch;
        wait_span();
        do { VIDEO_PIXELS=*words++; } while(--n);
        pairs-=batch;
    }
}
static void begin_line(unsigned x,unsigned y,unsigned n,void *context) {
    (void)x; (void)n; (void)context;
    if(!y) { wait_span(); window(0,0,ROAD_WIDTH,ROAD_HEIGHT); }
}
static void fill(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color) {
    unsigned count=w*h;
    wait_span(); window(x,y,w,h);
    span(count,color,0); wait_span();
}
/* ---- Slide-potentiometer input -------------------------------------------
 * The ADC pins share their package balls with plain IO, and the board routes
 * seven of those channels to header P2, so which one carries the wiper is not
 * known in advance. The channel that has travelled the most wins, and it only
 * becomes usable once its own range is wide enough to be a hand sweep rather
 * than pin noise. Sweep both mechanical stops in the menu, then KEY3 commits
 * the measured range. Never steer with a still-growing calibration: the
 * leading sample of a first sweep would always map to the road edge. */
#define ADC_CHANNELS 7u
#define ADC_MIN_SPAN 300u   /* about 0.07 VREF; below this it is pin noise */
static uint16_t adc_level[ADC_CHANNELS];
static uint16_t adc_low[ADC_CHANNELS],adc_high[ADC_CHANNELS];
static uint8_t adc_channel,adc_live,adc_primed,adc_manual,adc_calibrated;
static void adc_poll(void) {
    unsigned i;
    for(i=0;i<ADC_CHANNELS;i++) {
        unsigned raw=(unsigned)ADC_LEVEL(i+1)&0xfffu,v;
        if(!adc_primed) { adc_level[i]=(uint16_t)raw; adc_low[i]=adc_high[i]=(uint16_t)raw; }
        /* One frame of smoothing: the slide moves much faster than the noise
         * it has to hide, so a light filter keeps the pot feeling direct. */
        v=((unsigned)adc_level[i]*3u+raw)>>2;
        adc_level[i]=(uint16_t)v;
        if(!adc_calibrated) {
            if(v<adc_low[i]) adc_low[i]=(uint16_t)v;
            if(v>adc_high[i]) adc_high[i]=(uint16_t)v;
        }
    }
    adc_primed=1;
}
static unsigned adc_span(unsigned channel) {
    return (unsigned)adc_high[channel]-(unsigned)adc_low[channel];
}
static unsigned adc_position(void);
static void adc_select(void) {
    unsigned i,best=0,best_span=0;
    if(adc_calibrated) return;
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
    adc_live=adc_calibrated=adc_primed=0;
}
/* A second KEY3 after calibration starts a fresh sweep. Until committed,
 * keyboard steering remains available and the slider cannot move the ball. */
static void adc_calibrate(void) {
    if(adc_calibrated) adc_live=adc_calibrated=adc_primed=0;
    else if(adc_live) adc_calibrated=1;
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
static void hud_glyph(unsigned x,unsigned y,const uint8_t *rows,uint16_t on);
static void hud_gem_icon(unsigned x,unsigned y);
static void hud_volume_bars(void);
static unsigned hud_tens(unsigned v);
static void hud_rect(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color) {
    unsigned i;
    if(!hud_pixels) { fill(x,y,w,h,color); return; }
    /* Every covered row must be composited, including the second row of a
     * 2x glyph and the full opaque counter backing. */
    if(hud_row<y || hud_row-y>=h) return;
    for(i=0;i<w && x+i<ROAD_WIDTH;i++) hud_pixels[x+i]=color;
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
/* Retain the established enclosure/keypad orientation. HDMI rotation is
 * handled separately by the scanout scaler. Set to 0 for factory controls. */
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
/* Slide position over the frozen endpoint range, 0..1000. Clamp before
 * subtraction: noise beyond an endpoint must not wrap to the opposite side. */
static unsigned adc_position(void) {
    unsigned span=adc_span(adc_channel),v=adc_level[adc_channel];
    if(span<ADC_MIN_SPAN) return 500u;
    if(v<=adc_low[adc_channel]) return 0;
    if(v>=adc_high[adc_channel]) return 1000;
    return (unsigned)hud_divide((v-adc_low[adc_channel])*1000u,span);
}
/* Map only the committed physical travel, retaining 1.2% endpoint snap,
 * input orientation, noise deadband and the existing slew/collision path. */
static void adc_steer(Road *r,uint32_t ms) {
    unsigned pos=adc_position();
    int32_t target,delta,step,travel=ROAD_CAMERA_MAX-ROAD_CAMERA_MIN;
    if(!adc_calibrated || !adc_live) return;
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
static void hud_glyph(unsigned x,unsigned y,const uint8_t *rows,uint16_t on) {
    unsigned pass,row;
    /* Paint only the strokes, with a one-pixel rim for bright scenery.
     * Outline all strokes before their ink, so neighbours cannot erase ink. */
    for(pass=0;pass<2;pass++) for(row=0;row<5;row++) {
        unsigned bits=rows[row],col;
        for(col=0;col<3;col++) if(bits&(1u<<(2-col)))
            hud_rect(x+col*2-(pass?0u:1u),y+row*2-(pass?0u:1u),
                     pass?2u:4u,pass?2u:4u,pass?on:0x0000u);
    }
}
static void hud_digit(unsigned x,unsigned y,unsigned digit) {
    hud_glyph(x,y,hud_digits[digit],0xffffu);
}
static void hud_number(unsigned x,unsigned y,unsigned v) {
    unsigned tens=hud_tens(v);
    hud_digit(x,y,tens);
    hud_digit(x+7,y,v-tens*10u);
}
static void hud_gem_icon(unsigned x,unsigned y) {
    static const uint8_t widths[5]={1,3,5,3,1};
    unsigned pass,row;
    for(pass=0;pass<2;pass++) for(row=0;row<5;row++)
        hud_rect(x+(5-widths[row])/2-(pass?0u:1u),y+row*2-(pass?0u:1u),
                 widths[row]+(pass?0u:2u),pass?2u:4u,pass?0x07ffu:0x0000u);
}
static void hud_counter(unsigned gems) {
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
    hud_gem_icon(ROAD_WIDTH/2-32,8);
    hud_number(ROAD_WIDTH/2-23,8,hud.gems);
    x=ROAD_WIDTH/2-2;
    if(value>=100) { hud_digit(x,8,1); hud_digit(x+7,8,0); hud_digit(x+14,8,0); x+=21; }
    else { hud_number(x,8,value); x+=14; }
    hud_glyph(x,8,hud_pct_rows,0xffe0u);
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
    hud_rect(ROAD_WIDTH-44,8,39,6,0x0000u);
    if(w) hud_rect(ROAD_WIDTH-44,8,w-1,6,hud.vol>6u?0x07e0u:hud.vol>3u?0xffe0u:0xf800u);
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
            hud_rect(ROAD_WIDTH/2-9,8,5,12,0xffffu);
            hud_rect(ROAD_WIDTH/2+4,8,5,12,0xffffu);
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
static void video_init(void) {
    // Hardware clears the framebuffer once; no panel power-on protocol.
    while(!(SPAN_STATUS&4u)) {}
}
/* Keep menu-only locals out of the gameplay stack. Pack the two boolean
 * flags above the five-bit card count so the call needs no stack arguments. */
static uint8_t menu_cached_level,menu_cached_mechs;
#ifdef MENU_VIDEO
static uint8_t ui_previous[sizeof(MenuState)],ui_valid,ui_mode,ui_stage,ui_bar;
static uint32_t ui_stamp;
#endif
static __attribute__((noinline)) void menu_paint(const RoadSink *sink,unsigned selected,
                                                unsigned flags,uint32_t ticks) {
    unsigned card=flags&31u;
    MenuState m;
    m.selected=(uint8_t)selected; m.total=(uint8_t)(card?card:LEVEL_COUNT);
    m.focus=(uint8_t)((flags>>6)&1u); m.card=(uint8_t)card;
    m.error=(uint8_t)((flags>>5)&1u);
    m.best=best_pct[selected]; m.gems=best_gems[selected];
    m.adc=adc_channel; m.percent=(uint8_t)hud_divide(adc_position(),10u);
    m.live=adc_calibrated; m.manual=adc_manual; m.volume=(uint8_t)sound_volume_get();
    if(menu_cached_level!=selected) {
        menu_cached_mechs=(uint8_t)(card?sd_levels_mech_count(selected):0);
        menu_cached_level=(uint8_t)selected;
    }
    m.mechs=menu_cached_mechs; m.revision=(uint8_t)sd_levels_format();
    m.level_id=(uint8_t)(card?sd_levels_id(selected):levels[selected]->id);
#ifdef MENU_VIDEO
    {
        unsigned mode=(unsigned)menu_video_active(),stage,bar,i,dirty=!ui_valid;
        const uint8_t *bytes=(const uint8_t *)&m;
        volatile uint8_t *saved=ui_previous;
        ticks=mode?menu_video_time():0;
        stage=ticks<20000?0u:ticks<40000?1u:ticks<60000?2u:3u;
        bar=(ticks*52u)>>16;
        if(mode!=ui_mode || stage!=ui_stage || bar!=ui_bar)dirty=1;
        for(i=0;i<sizeof(m);i++)if(saved[i]!=bytes[i])dirty=1;
        if(!dirty || (ui_valid && (uint32_t)(milliseconds-ui_stamp)<200u))return;
        if(mode && !menu_video_ui_begin())return;
        if(!mode)game_video_begin();
        menu_draw(&m,ticks,sink);video_finish();
        if(mode)menu_video_ui_present();
        else game_video_present();
        for(i=0;i<sizeof(m);i++)saved[i]=bytes[i];
        ui_valid=1;ui_mode=(uint8_t)mode;ui_stage=(uint8_t)stage;ui_bar=(uint8_t)bar;ui_stamp=milliseconds;
    }
#else
    menu_draw(&m,ticks,sink);
#endif
}
#ifndef HUD_HOST_TEST
int main(void) {
    /* Startup branches directly here; explicitly initialize mutable state. */
    uint32_t camera=0,last_time,frame_start,keys;
    int paused=0,points=0;
    unsigned state=GAME_RUNNING,selected=0,card_count=0,sd_error=0,focus=0;
    int menu=1;
    unsigned frame_phase=0;
    static const RoadSink sink={begin_line,span,0,stream_pixels,sprite_prepare,sprite_span,tile_materials,tile_row};
    milliseconds=0; key_events=0; road_frame_count=0; road_last_frame_ms=0; art_sd_loaded=0;
    player_select(0); menu_cached_level=255;
    road_init(&road);
    road.overlay=&hud_layer;
    level_load(&road,&level_one,&camera);
    REG(0xe000e014)=31999; REG(0xe000e018)=0; REG(0xe000e010)=7;
    REG(0xe000e100)=1;
    video_init(); sound_init(); last_time=milliseconds;
#ifdef ROAD_PROFILE
    {
#define PROFILE_MARK REG(0x4005008c)
        unsigned scene;
        for(scene=0;scene<2;scene++) {
            ui_valid=0;
            player_select(scene?4:0);
            PROFILE_MARK=scene*4+17;
            menu_paint(&sink,0,scene?64:0,1024);
            video_finish();
            PROFILE_MARK=scene*4+20;
        }
        player_select(0);
        sprite_load();video_finish();
        for(scene=0;scene<2;scene++) {
            level_load(&road,&level_one,&camera);
            if(scene) camera=21*256-road.depth_q8[ROAD_BALL_Y]-256;
            PROFILE_MARK=scene*4+1;
            (void)level_step(&road,&camera,0,33);
            PROFILE_MARK=scene*4+2;
            road_draw(&road,camera,points,&sink); video_finish();
            PROFILE_MARK=scene*4+4;
        }
        for(;;) {}
#undef PROFILE_MARK
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
#ifdef MENU_VIDEO
    ui_valid=0;(void)menu_video_init();menu_video_start();
#endif
#endif
    for(;;) {
        frame_start=milliseconds;
        __asm volatile("cpsid i" ::: "memory");
        keys=keys_player_frame(key_events); key_events=0;
        __asm volatile("cpsie i" ::: "memory");
        adc_poll(); adc_select();
        /* The animated menu composites each pixel once, including volume. */
        (void)volume_keys(milliseconds);
        if(menu) {
#ifdef MENU_VIDEO
            menu_video_start();menu_video_service();
#endif
            if(keys&(1u<<2)) focus^=1u;
            if(keys&(1u<<4)) {
#ifdef MENU_VIDEO
                menu_video_stop();
#endif
                card_count=sd_levels_mount(); appearance_load(card_count!=0); sd_error=!card_count;
                menu_cached_level=255;
#ifdef MENU_VIDEO
                ui_valid=0;(void)menu_video_init();menu_video_start();
#endif
                selected=0;
            }
            if(keys&(1u<<12)) adc_cycle();
            if(keys&(1u<<3)) adc_calibrate();
            if((keys&(1u<<6)) && !(keys&(1u<<14))) {
                if(focus) player_select(player_selected()?player_selected()-1:PLAYER_COUNT-1);
                else if(selected) selected--;
            }
            if((keys&(1u<<14)) && !(keys&(1u<<6))) {
                if(focus) player_select(player_selected()+1<PLAYER_COUNT?player_selected()+1:0);
                else if(selected+1<(card_count?card_count:LEVEL_COUNT)) selected++;
            }
            if((keys&(1u<<8)) && focus) { focus=0; keys&=~(1u<<8); }
            if(keys&(1u<<8)) {
#ifdef MENU_VIDEO
                menu_video_stop();
#endif
                if(!start_level(selected,card_count,&road,&camera)) {
                    card_count=0; sd_error=1; selected=0;
                    continue;
                }
                sprite_load();
#ifdef MENU_VIDEO
                ui_valid=0;
#endif
                state=GAME_RUNNING; paused=0; menu=0; scored=0;
                last_time=frame_start=milliseconds; keys=0;
                game_fps_stamp=milliseconds;game_fps_frames=game_present_base=REG(0x400500f8);
            } else {
                menu_paint(&sink,selected,card_count|(sd_error<<5)|(focus<<6),milliseconds);
                wait_span();
                /* The menu has no music at all: the sequencer is told to
                 * stand down while the level picker is up, so the beat only
                 * exists while the road is moving. */
                sound_frame((unsigned)(milliseconds-last_time),0);
                last_time=milliseconds;
                finish_frame(frame_start,&frame_phase);
                continue;
            }
        } else if(keys&(1u<<12)) {
            menu=1;
            continue;
        }
        if(keys&(1u<<8)) paused=!paused;
        if(keys&(1u<<4)) {
            if(!start_level(selected,road.level==&sd_level,&road,&camera)) {
                menu=1; card_count=0; sd_error=1; selected=0;
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
            menu=1; card_count=0; sd_error=1; selected=0;
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
        game_render_begin(frame_start);
        road_draw(&road,camera,points,&sink); wait_span();
        game_render_end(frame_start);
        finish_frame(frame_start,&frame_phase);
    }
}
#endif
