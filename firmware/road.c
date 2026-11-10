#include "road.h"
#include "level.h"
#include "player.h"
#include "walls.h"
#include "glass.h"
#include "appearance.h"
static int ball_lift;
static const uint8_t *sprite_runs;
static unsigned sprite_count;
static const uint16_t *sprite_palette;
static unsigned wall_active,wall_cursor,ball_hidden,ball_red,ball_line_active;
/* ------------------------------------------------------------------------
 * Scene backdrop
 *
 * The four scene palettes each carry their own sky, horizon silhouette and
 * void colour, so every section of the level gets a background that matches
 * it: 初晴山丘's clear sky over rolling hills, 恶魔之路's violet night,
 * 原木森林's teal haze behind pale ridges and 幻想之光's near-black glow.
 *
 * The old pack stored one 240x160 panorama (assets/hills-background.png) in
 * the art cache and reused it for every scene -- 2.7 kB of a 6 kB cache for
 * the same picture four times. A silhouette is a step function of x, so it
 * is cheaper to keep thirty height samples and interpolate two of them per
 * column than to carry 38400 pixels: the whole backdrop costs 2.3 kB of
 * code and nothing per frame beyond sixty texture lookups.
 * --------------------------------------------------------------------- */
#define BD_COLS ((ROAD_WIDTH+7)/8)
#define BD_COL_W 8
#define BD_STEP 256
#define BD_HORIZON 100
#define BD_BANDS 8
#define BD_LOOKS 8
/* Accent drawn over the sky. Two looks per scene, picked by distance, so a
 * long level does not stare at the same skyline for a thousand rows. */
enum { BD_PLAIN=0, BD_DISC=1, BD_CRESCENT=2, BD_STARS=3, BD_AURORA=4 };
/* The moon sits in the upper right corner on purpose: the road's distant rows
 * project above the horizon and cover the middle of the screen, so a moon at
 * x=120 was sliced in half by the road and read as an abstract blob. */
#define BD_MOON_X (ROAD_WIDTH-24)
#define BD_RADIUS 22
#define BD_HOLE_R 19
#define BD_HOLE_DX (-10)
typedef struct {
    uint16_t sky[BD_BANDS];
    uint16_t far,near,ground,flare;
    uint8_t far_amp,near_amp,flare_y,feature;
} Backdrop;
/* Silhouette profiles: 0 is the horizon, 255 is the tall end of the range.
 * One profile per scene; the two layers read it at different phases and
 * amplitudes, which reads as two ranges without a second table. */
static const uint8_t bd_hills[32]={
    40,52,66,84,96,110,124,120,104,88,74,62,54,48,44,42,
    46,58,72,86,98,92,80,70,62,56,52,48,44,42,40,38};
static const uint8_t bd_forest[32]={
    96,120,140,150,144,128,112,98,86,78,72,68,66,70,80,92,
    104,110,102,88,76,68,62,58,60,68,80,94,106,112,104,98};
static const uint8_t bd_devil[32]={
    28,64,96,132,160,182,196,188,170,148,120,96,78,66,58,54,
    60,80,108,140,168,186,178,156,130,104,84,70,58,46,36,30};
static const uint8_t bd_fantasy[32]={
    54,58,66,80,98,118,136,146,142,130,112,94,80,72,68,66,
    70,82,100,120,136,140,128,110,92,78,68,62,58,56,54,52};
static const uint8_t bd_moon_hw[BD_RADIUS+1]={22,21,21,21,21,21,21,20,20,20,19,19,18,17,16,16,15,13,12,11,9,6,0};
static const uint8_t bd_hole_hw[BD_HOLE_R+1]={19,18,18,18,18,18,18,17,17,16,16,15,14,13,12,11,10,8,6,0};
static const Backdrop backdrops[BD_LOOKS]={
    /* 初晴山丘 · 晨阳 */
    {{0x2b7a,0x43fa,0x5c7b,0x751c,0x8d9c,0xa61d,0xbe9e,0xd71e},
     0x3bc9,0x2245,0x19a4,0xff72,44,64,22,BD_DISC},
    /* 初晴山丘 · 高云 */
    {{0x3bfb,0x547b,0x6cfc,0x857c,0x9ddd,0xb65e,0xcede,0xe75f},
     0x442a,0x2245,0x19a4,0xfff8,36,56,26,BD_DISC},
    /* 原木森林 · 密林: daylight through the canopy, so it keeps a sun; the
     * moonlit variant of the same scene only swaps the disc for a crescent. */
    {{0x0aeb,0x1b8c,0x2c0e,0x3caf,0x4d31,0x5dd2,0x6e54,0x7ef6},
     0x3bcc,0x1a47,0x11a5,0xd7ff,40,72,28,BD_DISC},
    /* 原木森林 · 月下林 */
    {{0x0947,0x11a8,0x1a0a,0x224c,0x2aad,0x3b0f,0x4371,0x4bd2},
     0x2a4b,0x1187,0x1125,0xdfff,44,68,24,BD_CRESCENT},
    /* 恶魔之路 · 残月 */
    {{0x1845,0x2066,0x20a7,0x28c9,0x30ea,0x390b,0x412c,0x494d},
     0x3909,0x20a6,0x1064,0xeedf,28,44,24,BD_CRESCENT},
    /* 恶魔之路 · 星海 */
    {{0x0823,0x1043,0x1064,0x1865,0x1886,0x20a7,0x20a8,0x28c9},
     0x30e8,0x1885,0x1043,0xffff,30,46,0,BD_STARS},
    /* 幻想之光 · 极光 */
    {{0x0001,0x0042,0x0063,0x0085,0x00c6,0x08e7,0x0928,0x0949},
     0x2104,0x1082,0x0021,0x5dff,34,52,30,BD_AURORA},
    /* 幻想之光 · 星芒 */
    {{0x0001,0x0042,0x0063,0x0085,0x08c6,0x08e7,0x0928,0x1149},
     0x2104,0x1082,0x0021,0x9eff,34,52,0,BD_STARS}
};
/* Per-scanline state. The active set only grows as y falls, so a column is
 * re-tested only until it turns on: at most BD_COLS changes per frame. */
static uint8_t bd_far[BD_COLS],bd_near[BD_COLS],bd_level[BD_COLS];
static uint16_t bd_run_x[BD_COLS+1],bd_run_l[BD_COLS+1];
static unsigned bd_runs;
static const Backdrop *bd_scene;
static unsigned bd_sample(const uint8_t *prof,unsigned phase,unsigned amp) {
    unsigned i=(phase>>8)&31u,f=phase&255u;
    int a=prof[i],b=prof[(i+1)&31u];
    unsigned v=(unsigned)((a*(256-(int)f)+b*(int)f)>>8);
    unsigned drop=(v*amp)>>8;
    return drop>=(unsigned)BD_HORIZON?0u:(unsigned)BD_HORIZON-drop;
}
static void backdrop_prepare(unsigned scene,uint32_t travel_q8,unsigned second) {
    const uint8_t *prof;
    unsigned c,far_off,near_off,base,look;
    base=scene<4u?scene:0u;
    /* Two looks per scene; the caller passes 1 for the second one. */
    look=base*2u+second;
    bd_scene=&backdrops[look<BD_LOOKS?look:0u];
    prof=base==1u?bd_forest:base==2u?bd_devil:base==3u?bd_fantasy:bd_hills;
    /* Parallax runs off the *forward* distance, never off the ball's lateral
     * position. The wiper puts a couple of counts of noise on the target every
     * frame, which used to move the skyline with the player's hand: the whole
     * picture shivered at frame rate, reported as "the screen is twitching".
     * Travel only ever increases, so the skyline drifts steadily and cannot
     * wobble; the far range gets an eighth of the motion and the near range a
     * quarter, which is enough separation to read as depth. */
    far_off=(travel_q8>>3)+320u;
    near_off=(travel_q8>>2)+1024u;
    for(c=0;c<BD_COLS;c++) {
        bd_far[c]=(uint8_t)bd_sample(prof,far_off+c*BD_STEP,bd_scene->far_amp);
        bd_near[c]=(uint8_t)bd_sample(prof,near_off+c*BD_STEP,bd_scene->near_amp);
        bd_level[c]=0;
    }
    /* Seed the run table with "sky everywhere". Without this the table starts
     * empty, every column is already at level 0 so nothing is ever dirty, and
     * the whole stretch above the skyline paints no pixels at all -- the frame
     * write cursor then runs a few hundred pixels short per frame and the
     * picture is displaced by a different amount every frame. */
    bd_runs=1;
    bd_run_x[0]=0;
    bd_run_l[0]=0;
    bd_run_x[1]=(uint16_t)(BD_COL_W*BD_COLS);
}

/* A scanline is assembled in RAM only when an overlay shares it with the
 * road. Every other line still streams straight to the framebuffer, so the HUD and
 * the gems cost no extra stores and, more importantly, no second write. */
static const RoadSink *line_sink;
static int line_buffered;
static unsigned line_end;
static unsigned line_pixels;   /* pixels streamed for the current scanline */
static uint16_t line_buf[ROAD_WIDTH] __attribute__((aligned(4)));
uint16_t *road_scanline_buffer(void) { return line_buf; }
/* Gem sprites for the current frame: perspective-scaled diamonds centred on
 * their cell, resolved once per frame instead of once per scanline. */
typedef struct { int16_t cx,cy; uint16_t hw,hh; } GemSprite;
#define GEM_SPRITES 16
static GemSprite gem_sprites[GEM_SPRITES];
static unsigned gem_sprite_count;
static uint32_t divide(uint32_t n,uint32_t d) {
    uint32_t q=0,bit=1;
    if(!d) return 0;
    while(d<=n && !(d&0x80000000u)) { d<<=1; bit<<=1; }
    while(bit) {
        if(n>=d) { n-=d; q|=bit; }
        d>>=1; bit>>=1;
    }
    return q;
}
static int signed_divide(int n,unsigned d) {
    return n<0?-(int)divide((unsigned)-n,d):(int)divide((unsigned)n,d);
}
void road_init(Road *r) {
    unsigned i;
    for(i=0;i<ROAD_ROWS;i++) { r->tags[i]=0xffffffffu; r->masks[i]=31; r->center_y[i]=-1; }
    for(i=0;i<ROAD_HEIGHT;i++) {
        r->depth_q8[i]=(uint16_t)divide((39104-20*i)*256,2496+12*i);
        r->scale_q8[i]=(uint16_t)divide((2496+12*i)*256,52);
    }
    r->camera_x_q8=ROAD_CAMERA_CENTER; r->forward_remainder=0;
    r->level=0;
    r->jump_ms=0; r->jump_row=0xffffu; r->jump_total=ROAD_JUMP_MS;
    r->visual_jump_ms=0; r->visual_jump_total=ROAD_JUMP_MS+ROAD_VISUAL_LANDING_MS;
    r->visual_next_total=0;
    r->death_ms=0; r->death_kind=0;
    r->overlay=0;
    r->sfx=0; r->mech_fired=0;
}
uint32_t road_forward(Road *r,uint32_t ms) {
    unsigned fractional=ms*3u+r->forward_remainder;
    r->forward_remainder=(uint8_t)(fractional&3u);
    return fractional>>2; /* 0.75 Q8/ms: 20 percent faster than 0.625. */
}
void road_pan(Road *r,int direction,uint32_t ms) {
    int next;
    if(!direction) return;
    if(ms>200) ms=200; /* Avoid a large jump after a pause/debug stall. */
    next=r->camera_x_q8+(direction<0?-(int)(ms*2u):(int)(ms*2u));
    if(next<ROAD_CAMERA_MIN) next=ROAD_CAMERA_MIN;
    if(next>ROAD_CAMERA_MAX) next=ROAD_CAMERA_MAX;
    r->camera_x_q8=(uint16_t)next;
}
unsigned road_mask(const Road *r,uint32_t row) {
    unsigned slot=row&(ROAD_ROWS-1);
    if(r->tags[slot]==row) return r->masks[slot];
    return r->level?level_ground(r->level,row):31;
}
void road_set_cell(Road *r,uint32_t row,unsigned col,int present) {
    unsigned slot=row&(ROAD_ROWS-1);
    if(col>=ROAD_COLS || row==0xffffffffu) return;
    if(r->tags[slot]!=row) {
        unsigned mask=road_mask(r,row);
        r->tags[slot]=row; r->masks[slot]=(uint8_t)mask;
    }
    if(present) r->masks[slot]|=(uint8_t)(1u<<col);
    else r->masks[slot]&=(uint8_t)~(1u<<col);
}
int road_query(const Road *r,int32_t x,uint32_t z,RoadPoint *p) {
    if(x<0 || x>=ROAD_COLS*256) return 0;
    p->row=z>>8; p->column=(uint8_t)((unsigned)x>>8);
    p->center_x_q8=p->column*256+128; p->center_z_q8=(z&~255u)+128;
    p->present=(uint8_t)((road_mask(r,p->row)>>p->column)&1u);
    return p->present;
}
/* Clip BEFORE converting to unsigned. Offscreen spans never become enormous
 * positive lengths. Full scanline coverage also erases previous pan positions. */
/* Kept out of line: every road span would otherwise carry its own copy of
 * the store loop, and this image has little code space to spare. */
void road_fill_pixels(uint16_t *pixels,unsigned count,uint16_t color) {
    typedef uint32_t AliasWord __attribute__((may_alias));
    AliasWord *words;
    uint32_t pair=(uint32_t)color|((uint32_t)color<<16);
    if(((uintptr_t)pixels&2u) && count) { *pixels++=color;count--; }
    words=(AliasWord *)pixels;
    while(count>=8) {
        words[0]=pair;words[1]=pair;words[2]=pair;words[3]=pair;
        words+=4;count-=8;
    }
    while(count>=2) { *words++=pair;count-=2; }
    if(count) *(uint16_t *)words=color;
}
static __attribute__((noinline)) void put_buffered(int left,int right,uint16_t color) {
    if((unsigned)right>line_end) line_end=(unsigned)right;
    road_fill_pixels(line_buf+left,(unsigned)(right-left),color);
}
static void put(int left,int right,uint16_t color) {
    if(left<0) left=0;
    if(right>ROAD_WIDTH) right=ROAD_WIDTH;
    if(right<=left) return;
    if(line_buffered) put_buffered(left,right,color);
    else {
        line_pixels+=(unsigned)(right-left);
        line_sink->span((unsigned)(right-left),color,line_sink->context);
    }
}
static void ball_overlay_segment(int left,int right,uint16_t color,unsigned y,
                         uint16_t foreground) {
    unsigned i,end,source_x=0;
    int x;
    if(left<0) left=0;
    if(right>ROAD_WIDTH) right=ROAD_WIDTH;
    if(right<=left) return;
    y+=ball_lift; /* Screen row -> sprite row; reuse the same ball data. */
    if(ball_hidden || y<GAME_BALL_TOP || y>=GAME_BALL_TOP+GAME_BALL_HEIGHT || right<=GAME_BALL_LEFT || left>=GAME_BALL_LEFT+GAME_BALL_WIDTH) {
        put(left,right,color); return;
    }
    if(!line_buffered && line_sink->sprite_span) {
        if(foreground)put(left,right,color);
        else {
            line_pixels+=(unsigned)(right-left);
            line_sink->sprite_span((unsigned)(right-left),color,line_sink->context);
        }
        return;
    }
    if(left<GAME_BALL_LEFT) put(left,GAME_BALL_LEFT,color);
    x=GAME_BALL_LEFT;
    end=sprite_count;
    for(i=0;i<end;i++) {
        uint8_t run=sprite_runs[i];
        source_x+=run>>3;
        int next=GAME_BALL_LEFT+(int)((source_x*15u)>>3);
        int start=left>x?left:x,stop=right<next?right:next;
        uint16_t overlay=foreground?foreground:sprite_palette[run&7u];
        if(overlay && ball_red && !foreground) overlay=0xf800u;
        if(stop>start) put(start,stop,overlay?overlay:color);
        x=next;
        if(x>=right) break;
    }
    if(right>GAME_BALL_LEFT+GAME_BALL_WIDTH) put(left>GAME_BALL_LEFT+GAME_BALL_WIDTH?left:GAME_BALL_LEFT+GAME_BALL_WIDTH,right,color);
}
static __attribute__((always_inline)) inline void ball_segment(int left,int right,uint16_t color,
                                                               unsigned y,uint16_t foreground) {
    if(ball_line_active) ball_overlay_segment(left,right,color,y,foreground);
    else put(left,right,color);
}
/* Merge the one-line wall layer before compositing the ball. Each screen
 * pixel still reaches the framebuffer exactly once, regardless of polygon overlap. */
static void composite_segment(int left,int right,uint16_t color,unsigned y,unsigned background) {
    int start;
    if(left<0) left=0;
    if(right>ROAD_WIDTH) right=ROAD_WIDTH;
    if(right<=left) return;
    if(!wall_active) { ball_segment(left,right,color,y,0); return; }
    while(wall_cursor<wall_span_count && wall_spans[wall_cursor].right<=left) wall_cursor++;
    for(start=left;start<right;) {
        const WallSpan *run=&wall_spans[wall_cursor];
        int end=right<run->right?right:run->right;
        /* Falling glass is below intact floor; walls remain above it. */
        uint16_t overlay=(uint16_t)run->color;
        uint16_t foreground=0;
        if((run->color&0x20000u) && !(run->color&0x10000u))
            foreground=overlay;
        if((run->color&0x10000u) && !background) overlay=0;
        ball_segment(start,end,overlay?overlay:color,y,foreground);
        start=end;
        if(start==run->right) wall_cursor++;
    }
}
/* Eight stops, one per sixteen rows: a nearest-stop lookup would paint twelve
 * flat rows at a time, which on a hundred-row sky reads as visible stairs.
 * One linear step per channel between neighbouring stops costs a handful of
 * shifts per scanline and removes them completely. */
static uint16_t bd_ramp(uint16_t a,uint16_t b,unsigned f) {
    int ar=(a>>11)&31,ag=(a>>5)&63,ab=a&31;
    int dr=((int)((b>>11)&31)-ar)*(int)f>>4;
    int dg=((int)((b>>5)&63)-ag)*(int)f>>4;
    int db=((int)(b&31)-ab)*(int)f>>4;
    return (uint16_t)(((ar+dr)<<11)|((ag+dg)<<5)|(ab+db));
}
/* Paint [left,right) of scanline y from the scene backdrop: sky bands above
 * the horizon, the two silhouettes between, the void colour below. Only the
 * margins and the holes are painted, so the cost tracks what is visible
 * rather than the full width. Runs are rebuilt only when a column changes
 * layer, which happens at most BD_COLS times per frame. */
static void backdrop_line(int left,int right,unsigned y) {
    const Backdrop *b=bd_scene;
    unsigned c,i,band,dirty=0;
    uint16_t sky;
    int mx0=0,mx1=0,hx0=0,hx1=0;
    if(!b || right<=left) return;
    if(y>=(unsigned)BD_HORIZON) { composite_segment(left,right,b->ground,y,1); return; }
    for(c=0;c<BD_COLS;c++) {
        unsigned lv=bd_near[c]<=y?2u:(bd_far[c]<=y?1u:0u);
        if(lv>bd_level[c]) { bd_level[c]=(uint8_t)lv; dirty=1; }
    }
    if(dirty) {
        bd_runs=0;
        for(c=0;c<BD_COLS;c++) {
            if(bd_runs && bd_run_l[bd_runs-1]==bd_level[c]) continue;
            bd_run_x[bd_runs]=(uint16_t)(c*BD_COL_W);
            bd_run_l[bd_runs]=(uint8_t)bd_level[c];
            bd_runs++;
        }
        bd_run_x[bd_runs]=(uint16_t)(BD_COL_W*BD_COLS);
    }
    band=y>>4; if(band>=BD_BANDS-1) band=BD_BANDS-2;
    sky=bd_ramp(b->sky[band],b->sky[band+1],y&15u);
    /* Aurora: a band that starts at full accent colour and fades into the sky
     * over eight rows, so the horizon keeps its own gradient underneath. */
    if(b->feature==BD_AURORA && y>=(unsigned)b->flare_y && y<(unsigned)b->flare_y+8u)
        sky=bd_ramp(b->flare,sky,(y-(unsigned)b->flare_y)*2u);
    if(b->feature==BD_DISC || b->feature==BD_CRESCENT) {
        int dy=(int)y-(int)b->flare_y;
        if(dy<0) dy=-dy;
        if(dy<BD_RADIUS) {
            /* Half-width from a sqrt table: the parabola (r^2-dy^2)/2r that
             * the first version used is half as wide as it is tall, so the
             * "moon" rendered as a vertical capsule. */
            int hw=bd_moon_hw[dy];
            mx0=BD_MOON_X-hw; mx1=BD_MOON_X+hw;
        }
    }
    if(b->feature==BD_CRESCENT) {
        /* The dark disc is the same circle shifted left and two rows up;
         * subtracting it leaves a crescent, and the sky under the bite is
         * simply never painted over. */
        int dy=(int)y-(int)b->flare_y+2;
        if(dy<0) dy=-dy;
        if(dy<BD_HOLE_R) {
            int hw=bd_hole_hw[dy];
            hx0=BD_MOON_X+BD_HOLE_DX-hw; hx1=BD_MOON_X+BD_HOLE_DX+hw;
        }
    }
    if(b->feature==BD_STARS && y<64u) {
        unsigned h=y*2654435761u;
        if(!((h>>9)&3u)) { int sx=6+(int)((h>>17)%(ROAD_WIDTH-12u)); mx0=sx; mx1=sx+2; }
    }
    for(i=0;i<bd_runs;i++) {
        int a=bd_run_x[i],e=bd_run_x[i+1];
        unsigned lv=bd_run_l[i];
        if(e<=left) continue;
        if(a>=right) break;
        if(a<left) a=left;
        if(e>right) e=right;
        if(lv) { composite_segment(a,e,lv==2u?b->near:b->far,y,1); continue; }
        /* Sky run. The accent (disc, crescent lobes or a star) is painted in
         * ascending x with the sky between the pieces: the streaming path
         * counts every pixel it emits, so a pixel must never be written twice
         * and never be skipped. */
        {
            int cursor=a,k,acc[2][2],nacc=0;
            if(mx1>mx0) {
                if(b->feature==BD_CRESCENT) {
                    if(hx0>mx0) { acc[nacc][0]=mx0; acc[nacc][1]=hx0<mx1?hx0:mx1; nacc++; }
                    if(hx1<mx1) {
                        int s=hx1>mx0?hx1:mx0;
                        if(s<mx1) { acc[nacc][0]=s; acc[nacc][1]=mx1; nacc++; }
                    }
                } else { acc[0][0]=mx0; acc[0][1]=mx1; nacc=1; }
            }
            for(k=0;k<nacc;k++) {
                int s0=acc[k][0],s1=acc[k][1];
                if(s1<=cursor) continue;
                if(s0>=e) break;
                if(s0<cursor) s0=cursor;
                if(s1>e) s1=e;
                if(s0>cursor) composite_segment(cursor,s0,sky,y,1);
                if(s1>s0) composite_segment(s0,s1,b->flare,y,1);
                cursor=s1;
            }
            if(cursor<e) composite_segment(cursor,e,sky,y,1);
        }
    }
}
static int projected_x(int world_x_q8,unsigned scale_q8) {
    int n=world_x_q8*(int)scale_q8;
    /* Signed truncation towards zero without an ARM division helper. */
    return road_display_x(ROAD_WIDTH/2+(n<0?-(int)((unsigned)-n>>16):(int)((unsigned)n>>16)));
}
/* Screen row of a world depth; shared by the row centres and the gems so a
 * sprite sits exactly on the row it belongs to. */
static int projected_y(int depth) {
    return 112+signed_divide(768*(48*256-5*depth),(unsigned)(12*depth+20*256));
}
/* Diamond half-width for one row of a gem: hw scaled by (hh-|dy|)/hh. */
static unsigned gem_halfwidth(const GemSprite *g,int dy) {
    unsigned ad=(unsigned)(dy<0?-dy:dy);
    return (unsigned)divide((uint32_t)g->hw*(g->hh-ad),g->hh);
}
/* The ball is opaque and sits nearer than any gem on its own row, so a
 * diamond must never paint over it. Clip each gem run against the opaque runs
 * of the sprite row instead of drawing the gem last. */
static void gem_put(unsigned y,int left,int right,uint16_t color) {
    unsigned row=(unsigned)((int)y+ball_lift),i,end,source_x=0;
    int x=GAME_BALL_LEFT,cursor=left;
    if(ball_hidden || row<(unsigned)GAME_BALL_TOP || row>=(unsigned)(GAME_BALL_TOP+GAME_BALL_HEIGHT) ||
       right<=GAME_BALL_LEFT || left>=GAME_BALL_LEFT+GAME_BALL_WIDTH) { put(left,right,color); return; }
    end=sprite_count;
    for(i=0;i<end && x<right;i++) {
        unsigned run=sprite_runs[i];
        source_x+=run>>3;
        int next=GAME_BALL_LEFT+(int)((source_x*15u)>>3);
        if(run&7u) {                      /* opaque pixel of the ball */
            int stop=next<right?next:right;
            if(x>cursor && x<right) put(cursor,x,color);
            if(stop>cursor) cursor=stop;
        }
        x=next;
    }
    if(cursor<right) put(cursor,right,color);
}
static void gem_line(unsigned y) {
    unsigned i;
    for(i=0;i<gem_sprite_count;i++) {
        const GemSprite *g=&gem_sprites[i];
        int dy=(int)y-(int)g->cy,cx=g->cx;
        unsigned halfw,edge,inner;
        if(dy>(int)g->hh || -dy>(int)g->hh) continue;
        halfw=gem_halfwidth(g,dy);
        if(halfw<2) { gem_put(y,cx-(int)halfw,cx+(int)halfw+1,ROAD_GEM_LIGHT); continue; }
        edge=halfw>6?2u:1u;
        inner=(halfw*11u)>>5; /* /3 without the EABI divide helper */
        gem_put(y,cx-(int)halfw,cx-(int)halfw+(int)edge,ROAD_GEM_EDGE);
        gem_put(y,cx-(int)halfw+(int)edge,cx-(int)inner,dy<0?ROAD_GEM_TOP:ROAD_GEM_MID);
        gem_put(y,cx-(int)inner,cx+(int)inner+1,dy<0?ROAD_GEM_LIGHT:ROAD_GEM_TOP);
        gem_put(y,cx+(int)inner+1,cx+(int)halfw-(int)edge,dy<0?ROAD_GEM_MID:ROAD_GEM_LOW);
        gem_put(y,cx+(int)halfw-(int)edge,cx+(int)halfw+1,ROAD_GEM_EDGE);
    }
}
/* Uncollected gems of the visible window, nearest first. A gem already at
 * the ball's own row is being picked up and is left to the ball sprite. */
static void collect_gems(Road *r,uint32_t camera,uint32_t first,uint32_t last) {
    const LevelGem *list;
    unsigned i,total;
    uint32_t ball_row;
    gem_sprite_count=0;
    if(!r->level) return;
    total=r->level->gem_count;
    if(!total) return;
    list=r->level->gems;
    ball_row=(camera+r->depth_q8[ROAD_BALL_Y])>>8;
    for(i=0;i<total && gem_sprite_count<GEM_SPRITES;i++) {
        uint32_t row;
        int depth,py;
        unsigned sc,tile_w;
        GemSprite *g;
        if(r->gem_bits>>i&1u) continue;
        row=list[i].row;
        if(row<=ball_row || row<first || row>last) continue;
        depth=(int)((row<<8)+128-camera);
        if(depth<=0) continue;
        py=projected_y(depth);
        sc=r->scale_q8[py<0?0:py>=ROAD_HEIGHT?ROAD_HEIGHT-1:(unsigned)py];
        tile_w=sc>>8;
        if(py<-(int)tile_w || py>ROAD_HEIGHT+(int)tile_w) continue;
        g=&gem_sprites[gem_sprite_count++];
        g->hw=(uint16_t)(tile_w>>2); if(g->hw<3) g->hw=3;
        g->hh=(uint16_t)((tile_w*3)>>3); if(g->hh<4) g->hh=4;
        g->cx=(int16_t)projected_x((int)(list[i].column*256u+128u)-(int)r->camera_x_q8,sc);
        g->cy=(int16_t)py;
    }
}
static int gem_touches(unsigned y) {
    unsigned i;
    for(i=0;i<gem_sprite_count;i++) {
        int dy=(int)y-(int)gem_sprites[i].cy;
        if(dy<=(int)gem_sprites[i].hh && -dy<=(int)gem_sprites[i].hh) return 1;
    }
    return 0;
}
static void flush_line(void) {
    unsigned x=0;
    if(line_sink->pixels) { line_sink->pixels(line_buf,ROAD_WIDTH,line_sink->context);return; }
    while(x<ROAD_WIDTH) {
        uint16_t color=line_buf[x];
        unsigned end=x+1;
        while(end<ROAD_WIDTH && line_buf[end]==color) end++;
        line_sink->span(end-x,color,line_sink->context);
        x=end;
    }
}
/* Three kinds of obstacle, told apart on the floor because the face cache
 * only builds geometry four rows ahead.
 *
 * The previous build tinted every occupied cell with the same dark plate,
 * which made a tree maze read as "something is about to rise here" all the
 * way to the horizon -- and the player was right: in the reference only the
 * EnemyRiser family and the moving mechanisms actually emerge, while trees,
 * pillars and low walls are simply there and can be seen coming. So static
 * bodies take the colour of the object that stands on them, and only the
 * risers and the mechanisms keep the sunken plate. */
static unsigned body_mask(const Level *level,uint32_t row,unsigned kind) {
    return level?level_tiles(level,row,kind):0;
}
static unsigned warn_mask(const Level *level,uint32_t row) {
    const LevelMech *mech;
    unsigned i,mask;
    if(!level) return 0;
    mask=level_tiles(level,row,TILE_GROUND_BLOCK);
    for(i=0;i<level->mech_count;i++) {
        mech=&level->mechs[i];
        if(mech->row!=row) continue;
        mask|=1u<<mech->column;
        /* A saw group sweeps two lanes either side of its own, so it marks
         * the whole band it can reach. */
        if(mech->kind==MECH_ROLLER) {
            if(mech->column) mask|=1u<<(mech->column-1);
            if(mech->column<ROAD_COLS-1) mask|=1u<<(mech->column+1);
        }
    }
    return mask;
}
/* First or second sky of the scene? Each stretch flips once, at its middle,
 * so all four scenes put both of their looks on screen. A "flip every N rows
 * of travel" timer was tried first and it never ticked inside Massif's devil
 * (102 rows) or fantasy (60 rows) stretches, so two of the eight skies were
 * dead data. The span comes from the same cue table that picks the palette.
 * Stretches shorter than 48 rows keep the first look: flipping them would put
 * two sky changes inside thirty rows, which reads as a glitch. */
#define BD_FLIP_MIN 48u
static unsigned scene_second(const Road *r,unsigned row) {
    unsigned lo,hi;
    if(r->level && appearance_cue_span(r->level,row,&lo,&hi) && hi-lo>=BD_FLIP_MIN)
        return row>=(lo+hi)/2u;
    return 0;
}
/* --- finishing gate -------------------------------------------------------
 * A doorway rather than a painted stripe. It is a vertical object, so it
 * covers scanlines that belong to rows *beyond* the finish, where the
 * streaming path has already committed its pixels; the only way to paint over
 * those is the line buffer, which is why gate_here() forces buffering on the
 * handful of lines it touches. */
static int gate_y,gate_top,gate_cy,gate_cx,gate_r,gate_rc;
static int gate_here(unsigned y) {
    return gate_y>0 && (int)y>=gate_top && (int)y<=gate_y;
}
/* Half-width of a row of a circle, without a divider and without a per-pixel
 * distance test. The panel has 240x320 pixels and a third of a frame's budget
 * goes to the road itself, so the ring recurses instead: sq keeps "r^2-dy^2
 * minus what has already been handed to w", and each step only adds and
 * subtracts. Two circles are tracked at once -- the rim and the cyan disc --
 * and each needs a cursor per half, because dy grows going down and shrinks
 * going up so the two halves cannot share one state. */
static int gu_dy,gu_w0,gu_w1,gu_sq0,gu_sq1;
static int gd_dy,gd_w0,gd_w1,gd_sq0,gd_sq1;
static void gate_step_up(void) {
    int d;
    if(gu_dy<=0) return;
    gu_dy--;
    d=(gu_dy<<1)+1;
    gu_sq0+=d;
    while(gu_sq0>=(gu_w0<<1)+1) { gu_sq0-=(gu_w0<<1)+1; gu_w0++; }
    gu_sq1+=d;
    while(gu_sq1>=(gu_w1<<1)+1) { gu_sq1-=(gu_w1<<1)+1; gu_w1++; }
}
static void gate_step_down(void) {
    int d;
    gd_dy++;
    d=(gd_dy<<1)-1;
    gd_sq0-=d;
    while(gd_sq0<0 && gd_w0>0) { gd_sq0+=(gd_w0<<1)-1; gd_w0--; }
    gd_sq1-=d;
    while(gd_sq1<0 && gd_w1>0) { gd_sq1+=(gd_w1<<1)-1; gd_w1--; }
}
static void gate_paint(unsigned y) {
    int x,hw,hc,ring,l,r;
    if(!gate_here(y)) return;
    if((int)y<gate_cy) { hw=gu_w0; hc=gu_w1; gate_step_up(); }
    else { hw=gd_w0; hc=gd_w1; gate_step_down(); }
    if(hw<=0) return;
    ring=hw-hc;
    if(ring<=0) ring=1;
    l=gate_cx-hw;
    r=gate_cx+hw+1;
    if(l<0) l=0;
    if(r>ROAD_WIDTH) r=ROAD_WIDTH;
    for(x=l;x<r;x++) {
        int ax=x-gate_cx;
        if(ax<0) ax=-ax;
        if(ax>hw) continue;            /* outside the rim: sky shows through */
        if(ax<=hc) {
            /* Cyan disc: four bands so the centre reads as a glow rather than
             * a flat plate. A true radial ramp would need one multiply per
             * pixel; four compares cost a fraction of that and the steps sit
             * under the eye's threshold at this size. */
            uint16_t c;
            if(ax*4<hc) c=ROAD_GATE_CORE;
            else if(ax*2<hc) c=ROAD_GATE_GLOW;
            else if(ax*4<hc*3) c=ROAD_GATE_MEM_A;
            else c=ROAD_GATE_MEM_B;
            line_buf[x]=c;
        } else {
            int t=ax-hc;
            uint16_t c;
            if(ax>hw-2) c=ROAD_GATE_EDGE;
            else if(t*8<ring) c=ROAD_GATE_BAND;
            else if(t*20<ring*11) c=ROAD_GATE_RIM2;
            else c=ROAD_GATE_RIM;
            line_buf[x]=c;
        }
    }
}
static void gate_prepare(Road *r,uint32_t camera) {
    int d,py,r2,c2,start,y0;
    unsigned sc,tw;
    gate_y=0;
    if(!r->level) return;
    /* Anchored to the last row that actually has road under it, not to the row
     * past it: the portal has to stand on the floor the player walks in on,
     * otherwise it hangs in the air with a strip of void beneath. */
    d=(int)(((uint32_t)(r->level->row_count?r->level->row_count-1u:0u)<<8)+128u-camera);
    if(d<=0) return;
    py=projected_y(d);
    if(py<-96 || py>=ROAD_HEIGHT+96) return;
    sc=r->scale_q8[py<0?0:py>=ROAD_HEIGHT?ROAD_HEIGHT-1:(unsigned)py];
    tw=sc>>8;
    gate_y=py;
    /* One and a half lanes of radius: the rim is three lanes across, so a lane
     * of road still shows either side of it as in the reference, and the whole
     * circle fits on screen by the time the ball reaches it. Two and a half was
     * tried first and the top of the rim was always cut off by the top edge. */
    gate_r=(int)tw+(int)(tw>>1);
    gate_rc=gate_r-(gate_r>>1)+(gate_r>>4);
    gate_cy=py-gate_r+(gate_r>>2);     /* the rim sinks a quarter into the floor */
    gate_cx=(projected_x(-(int)r->camera_x_q8,sc)+projected_x(5*256-(int)r->camera_x_q8,sc))/2;
    gate_top=gate_cy-gate_r;
    if(gate_top>=(int)ROAD_HEIGHT || gate_y<0) { gate_y=0; return; }
    r2=gate_r*gate_r;
    c2=gate_rc*gate_rc;
    /* Both cursors are parked at the top of their circle and then walked to
     * the first visible line, so no square root is needed anywhere: the
     * recursive sq is all the state a row of a circle needs. */
    gu_dy=gate_r; gu_w0=0; gu_sq0=0; gu_w1=0; gu_sq1=c2-r2;
    y0=gate_top<0?0:gate_top;
    start=gate_cy-y0;
    if(start<0) start=0;
    if(start>gate_r) start=gate_r;
    while(gu_dy>start) gate_step_up();
    gd_dy=0; gd_w0=gate_r; gd_sq0=0; gd_w1=gate_rc; gd_sq1=0;
    start=y0>gate_cy?y0-gate_cy:0;
    if(start>gate_r) start=gate_r;
    while(gd_dy<start) gate_step_down();
}
/* Keep projection preparation outside this scanline frame: on an 8 KiB RAM
 * target their two large stack frames must never be live at the same time. */
static __attribute__((noinline)) void draw(Road *r,uint32_t camera,int points,const RoadSink *s,int cached) {
    uint32_t first=(camera+r->depth_q8[ROAD_HEIGHT-1])>>8;
    uint32_t last=(camera+r->depth_q8[0])>>8;
    uint32_t row,previous=0xffffffffu;
    unsigned y,col,mask=31,pads=0,glass=0,shorts=0,bodies=0,barrier=0,warn=0;
    uint32_t tile_materials[ROAD_COLS];
    unsigned ball_row=(unsigned)((camera+r->depth_q8[ROAD_BALL_Y])>>8);
    const Appearance *look=appearance_for(r->level,ball_row);
    uint16_t body_color=look->solid_front,barrier_color=look->barrier_front;
    backdrop_prepare(appearance_index(look),camera,scene_second(r,ball_row));
    ball_lift=(int)road_jump_height(r);
    ball_hidden=0; ball_red=0;
    line_sink=s;
    if(r->death_kind==GAME_FELL) {
        ball_lift-=(int)(r->death_ms*r->death_ms>>11);
        ball_hidden=r->death_ms>=800;
    } else if(r->death_kind==GAME_HIT) {
        unsigned t=r->death_ms;
        if(t<400) ball_lift-=(int)(t*(400-t)>>11);
        ball_red=(t>>6)&1u;
        ball_hidden=t>=800;
    }
    gate_prepare(r,camera);
    collect_gems(r,camera,first,last);
#ifdef ROAD_PROFILE
    (*(volatile uint32_t *)0x20001800)++;
#endif
    for(col=0;col<ROAD_ROWS;col++) r->center_y[col]=-1;
    if(points || r->level) for(row=first;row<=last;row++) {
        int depth=(int)((row<<8)+128-camera);
        if(depth>0) {
            int py=projected_y(depth);
            if(py>=0 && py<ROAD_HEIGHT) r->center_y[row&(ROAD_ROWS-1)]=(int16_t)py;
        }
    }
    sprite_palette=player_colors();
    if(s->sprite_prepare)s->sprite_prepare(GAME_BALL_LEFT,GAME_BALL_TOP-ball_lift,ball_red,s->context);
    for(y=0;y<ROAD_HEIGHT;y++) {
        ball_line_active=!ball_hidden && (unsigned)((int)y+ball_lift-GAME_BALL_TOP)<GAME_BALL_HEIGHT;
        unsigned depth=cached?r->depth_q8[y]:divide((39104-20*y)*256,2496+12*y);
        unsigned scale=cached?r->scale_q8[y]:divide((2496+12*y)*256,52);
        int x=projected_x(-(int)r->camera_x_q8,scale);
        int middle,horizontal;
        row=(camera+depth)>>8;
        if(row!=previous) {
            mask=road_mask(r,row)&~glass_moving_mask(row);
            pads=r->level?level_tiles(r->level,row,TILE_JUMP):0;
            shorts=r->level?level_tiles(r->level,row,TILE_SHORT_JUMP):0;
            glass=r->level?level_tiles(r->level,row,TILE_FRAGILE):0;
            bodies=body_mask(r->level,row,TILE_SOLID)|body_mask(r->level,row,TILE_AIR_BLOCK);
            barrier=body_mask(r->level,row,TILE_LOW_BARRIER);
            warn=warn_mask(r->level,row);
            /* The tint is the body's own front material, read from the same
             * model walls.c builds geometry from, so a distant tree reads as
             * dark-green timber and a pillar as stone instead of every one of
             * them reading as the same warning plate. */
            body_color=((const uint16_t *)look)[
                appearance_model(look->solid_model)->boxes[0].front];
            barrier_color=look->barrier_front;
            for(col=0;col<ROAD_COLS;col++) {
                uint16_t color=look->light;
                if(pads&(1u<<col))color=look->pad;
                if(shorts&(1u<<col))color=look->short_pad;
                if(glass&(1u<<col))color=look->fragile;
                if(bodies&(1u<<col))color=body_color;
                if(barrier&(1u<<col))color=barrier_color;
                if(warn&(1u<<col))color=look->dark;
                tile_materials[col]=(uint32_t)((glass&(1u<<col))?look->fragile_edge:look->grid)<<16|color;
            }
            if(s->tile_materials)s->tile_materials(tile_materials,s->context);
        }
        middle=r->center_y[row&(ROAD_ROWS-1)]; horizontal=row!=previous;
        wall_active=(unsigned)walls_line(y); wall_cursor=0;
        line_buffered=(r->overlay && y<r->overlay->rows) || gem_touches(y) || gate_here(y);
        if(ball_line_active && (line_buffered || !s->sprite_span))
            sprite_runs=player_row(((unsigned)((int)y+ball_lift-GAME_BALL_TOP)*17u)>>5,&sprite_count);
        line_end=0;
        line_pixels=0;
        s->begin_line(0,y,ROAD_WIDTH,s->context);
        backdrop_line(0,x,y);
        if(s->tile_row && mask==31 && !line_buffered && !wall_active && !points &&
           (!((pads|shorts)) || middle<0 || (int)y<middle-1 || (int)y>middle+1)) {
            int right=projected_x(ROAD_COLS*256-(int)r->camera_x_q8,scale);
            int left_clipped=x<0?0:x>ROAD_WIDTH?ROAD_WIDTH:x;
            int right_clipped=right<0?0:right>ROAD_WIDTH?ROAD_WIDTH:right;
            s->tile_row(scale,r->camera_x_q8,(unsigned)horizontal|(ball_line_active<<1),s->context);
            line_pixels+=(unsigned)(right_clipped-left_clipped);
            x=right;
        } else
        for(col=0;col<ROAD_COLS;col++) {
            int right=projected_x((int)((col+1)*256)-(int)r->camera_x_q8,scale);
            uint16_t grid=look->grid;
            uint16_t color=(uint16_t)tile_materials[col];
            if(!(mask&(1u<<col))) backdrop_line(x,right,y);
            else if(horizontal) composite_segment(x,right,(glass&(1u<<col))?look->fragile_edge:grid,y,0);
            else {
                composite_segment(x,x+1,(glass&(1u<<col))?look->fragile_edge:grid,y,0);
                if((points || ((pads|shorts)&(1u<<col))) && middle>=0 && (int)y>=middle-1 && (int)y<=middle+1) {
                    int center=projected_x((int)(col*256+128)-(int)r->camera_x_q8,scale);
                    composite_segment(x+1,center-1,color,y,0);
                    composite_segment(center-1,center+1,((pads|shorts)&(1u<<col))?0xffffu:ROAD_POINT,y,0);
                    composite_segment(center+1,right,color,y,0);
                } else composite_segment(x+1,right,color,y,0);
            }
            x=right;
        }
        backdrop_line(x,ROAD_WIDTH,y);
        previous=row;
        if(line_buffered) {
            if(line_end<(unsigned)ROAD_WIDTH) {
                unsigned fill_x;
                for(fill_x=line_end;fill_x<(unsigned)ROAD_WIDTH;fill_x++) line_buf[fill_x]=bd_scene?bd_scene->ground:0;
            }
            if(gate_here(y)) gate_paint(y);
            gem_line(y);
            if(r->overlay && y<r->overlay->rows) r->overlay->line(line_buf,y,r->overlay->context);
            flush_line();
        } else if(line_pixels<ROAD_WIDTH) {
            /* The panel's address pointer auto-increments, so a scanline that
             * emits fewer than ROAD_WIDTH pixels shifts every line below it --
             * one missed margin and the frame is displaced for good. Padding
             * costs one compare per line and makes that failure harmless. */
            line_sink->span(ROAD_WIDTH-line_pixels,
                bd_scene?bd_scene->ground:0u,line_sink->context);
        }
    }
}
void road_draw(Road *r,uint32_t camera,int points,const RoadSink *s) {
    walls_prepare(r,camera);
    draw(r,camera,points,s,1);
}
#ifdef ROAD_BENCHMARK
void road_draw_calculated(Road *r,uint32_t camera,int points,const RoadSink *s) {
    walls_prepare(r,camera);
    draw(r,camera,points,s,0);
}
#endif
