#include "road.h"
#include "level.h"
#include "ball_sprite.h"
#include "walls.h"
#include "glass.h"
#include "appearance.h"
static int ball_lift;
static unsigned wall_active,wall_cursor,ball_hidden,ball_red;
static uint16_t scene_bg;
static const ArtBackground *scene_background;
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
        r->depth_q8[i]=(uint16_t)divide((19552-20*i)*256,1248+12*i);
        r->scale_q8[i]=(uint16_t)divide((1248+12*i)*256,52);
    }
    r->camera_x_q8=ROAD_CAMERA_CENTER; r->pan_remainder=0; r->forward_remainder=0;
    r->level=0;
    r->jump_ms=0; r->jump_row=0xffffu; r->jump_total=ROAD_JUMP_MS;
    r->death_ms=0; r->death_kind=0;
}
uint32_t road_forward(Road *r,uint32_t ms) {
    unsigned fractional=ms*3u+r->forward_remainder;
    r->forward_remainder=(uint8_t)(fractional&3u);
    return fractional>>2; /* 0.75 Q8/ms: 20 percent faster than 0.625. */
}
void road_pan(Road *r,int direction,uint32_t ms) {
    int next;
    if(!direction) { r->pan_remainder=0; return; }
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
static void ball_segment(const RoadSink *s,int left,int right,uint16_t color,unsigned y,
                         uint16_t foreground) {
    unsigned i,end;
    int x;
    if(left<0) left=0;
    if(right>ROAD_WIDTH) right=ROAD_WIDTH;
    if(right<=left) return;
    y+=ball_lift; /* Screen row -> sprite row; reuse the same ball data. */
    if(ball_hidden || y<BALL_TOP || y>=BALL_TOP+BALL_HEIGHT || right<=BALL_LEFT || left>=BALL_LEFT+BALL_WIDTH) {
        s->span((unsigned)(right-left),color,s->context); return;
    }
    if(left<BALL_LEFT) s->span((unsigned)(BALL_LEFT-left),color,s->context);
    x=BALL_LEFT;
    end=ball_row_offset[y-BALL_TOP+1];
    for(i=ball_row_offset[y-BALL_TOP];i<end;i++) {
        uint8_t run=ball_runs[i];
        int next=x+(int)(run>>3);
        int start=left>x?left:x,stop=right<next?right:next;
        uint16_t overlay=foreground?foreground:ball_palette[run&7u];
        if(overlay && ball_red && !foreground) overlay=0xf800u;
        if(stop>start) s->span((unsigned)(stop-start),overlay?overlay:color,s->context);
        x=next;
        if(x>=right) break;
    }
    if(right>BALL_LEFT+BALL_WIDTH) {
        int start=left>BALL_LEFT+BALL_WIDTH?left:BALL_LEFT+BALL_WIDTH;
        s->span((unsigned)(right-start),color,s->context);
    }
}
/* Merge the one-line wall layer before compositing the ball. Each screen
 * pixel still reaches the LCD exactly once, regardless of polygon overlap. */
static void composite_segment(const RoadSink *s,int left,int right,uint16_t color,unsigned y,unsigned background) {
    int start;
    if(left<0) left=0;
    if(right>ROAD_WIDTH) right=ROAD_WIDTH;
    if(!wall_active) { ball_segment(s,left,right,color,y,0); return; }
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
        ball_segment(s,start,end,overlay?overlay:color,y,foreground);
        start=end;
        if(start==run->right) wall_cursor++;
    }
}
/* Decode only the background spans exposed by holes or outside the road.
 * Width/height are the original 240x235; extend its uniform bottom to 320. */
static void segment(const RoadSink *s,int left,int right,uint16_t color,unsigned y) {
    unsigned background=color==scene_bg;
    if(!background || !scene_background || y>=scene_background->height) {
        if(background && scene_background) color=(uint16_t)scene_background->bottom;
        composite_segment(s,left,right,color,y,background);
    } else {
        unsigned i=scene_background->offsets[y],end=scene_background->offsets[y+1];
        int x=0;
        while(i<end && x<right) {
            unsigned code=scene_background->runs[i++],n,index;
            int next;
            if(code<144) { n=1; index=code; }
            else { n=code==255?scene_background->runs[i++]:code-142; index=scene_background->runs[i++]; }
            next=x+(int)n;
            if(next>left) composite_segment(s,x<left?left:x,next>right?right:next,scene_background->palette[index],y,1);
            x=next;
        }
    }
}
static int projected_x(int world_x_q8,unsigned scale_q8) {
    int n=world_x_q8*(int)scale_q8;
    /* Signed truncation towards zero without an ARM division helper. */
    return ROAD_WIDTH/2+(n<0?-(int)((unsigned)-n>>16):(int)((unsigned)n>>16));
}
static void draw(Road *r,uint32_t camera,int points,const RoadSink *s,int cached) {
    uint32_t first=(camera+r->depth_q8[ROAD_HEIGHT-1])>>8;
    uint32_t last=(camera+r->depth_q8[0])>>8;
    uint32_t row,previous=0xffffffffu;
    unsigned y,col,mask=31,pads=0,glass=0,shorts=0,gems=0,finish=0;
    unsigned gem_masks[16];
    const Appearance *look=appearance_for(r->level,(camera+r->depth_q8[ROAD_BALL_Y])>>8);
    scene_bg=look->background;
    scene_background=look->backdrop?appearance_background():0;
    ball_lift=(int)road_jump_height(r);
    ball_hidden=0; ball_red=0;
    if(r->death_kind==GAME_FELL) {
        ball_lift-=(int)(r->death_ms*r->death_ms>>11);
        ball_hidden=r->death_ms>=800;
    } else if(r->death_kind==GAME_HIT) {
        unsigned t=r->death_ms;
        if(t<400) ball_lift-=(int)(t*(400-t)>>11);
        ball_red=(t>>6)&1u;
        ball_hidden=t>=800;
    }
    walls_prepare(r,camera);
#ifdef ROAD_PROFILE
    (*(volatile uint32_t *)0x20001800)++;
#endif
    for(col=0;col<ROAD_ROWS;col++) r->center_y[col]=-1;
    /* Uncollected gems become per-row masks over the visible window; the
     * window spans under 16 rows, matching the mask slot count. */
    for(col=0;col<16;col++) gem_masks[col]=0;
    if(r->level && last-first<16u) {
        const LevelGem *list=r->level->gems;
        unsigned i,total=r->level->gem_count;
        for(i=0;i<total;i++)
            if(!(r->gem_bits>>i&1u) && list[i].row>=first && list[i].row-first<16u)
                gem_masks[list[i].row&15u]|=1u<<list[i].column;
    }
    if(points || r->level) for(row=first;row<=last;row++) {
        int depth=(int)((row<<8)+128-camera);
        if(depth>0) {
            int py=56+signed_divide(384*(48*256-5*depth),(unsigned)(12*depth+20*256));
            if(py>=0 && py<ROAD_HEIGHT) r->center_y[row&(ROAD_ROWS-1)]=(int16_t)py;
        }
    }
    for(y=0;y<ROAD_HEIGHT;y++) {
        unsigned depth=cached?r->depth_q8[y]:divide((19552-20*y)*256,1248+12*y);
        unsigned scale=cached?r->scale_q8[y]:divide((1248+12*y)*256,52);
        int x=projected_x(-(int)r->camera_x_q8,scale);
        int middle,horizontal;
        row=(camera+depth)>>8;
        if(row!=previous) {
            mask=road_mask(r,row)&~glass_moving_mask(row);
            pads=r->level?level_tiles(r->level,row,TILE_JUMP):0;
            shorts=r->level?level_tiles(r->level,row,TILE_SHORT_JUMP):0;
            glass=r->level?level_tiles(r->level,row,TILE_FRAGILE):0;
            gems=r->level?gem_masks[row&15u]:0;
            finish=r->level && row==r->level->row_count;
        }
        middle=r->center_y[row&(ROAD_ROWS-1)]; horizontal=row!=previous;
        wall_active=(unsigned)walls_line(y); wall_cursor=0;
        s->begin_line(0,y,ROAD_WIDTH,s->context);
        segment(s,0,x,scene_bg,y);
        for(col=0;col<ROAD_COLS;col++) {
            int right=projected_x((int)((col+1)*256)-(int)r->camera_x_q8,scale);
            uint16_t grid=look->grid;
            uint16_t color=((row+col)&1)?look->light:look->dark;
            if(pads&(1u<<col)) color=look->pad;
            if(shorts&(1u<<col)) color=look->short_pad;
            if(glass&(1u<<col)) color=look->fragile;
            if(gems&(1u<<col)) color=ROAD_GEM_CELL;
            if(finish) segment(s,x,right,(col&1u)?ROAD_FINISH_A:ROAD_FINISH_B,y);
            else if(!(mask&(1u<<col))) segment(s,x,right,scene_bg,y);
            else if(horizontal) segment(s,x,right,(glass&(1u<<col))?look->fragile_edge:grid,y);
            else {
                segment(s,x,x+1,(glass&(1u<<col))?look->fragile_edge:grid,y);
                if((points || ((pads|shorts)&(1u<<col))) && middle>=0 && (int)y>=middle-1 && (int)y<=middle+1) {
                    int center=projected_x((int)(col*256+128)-(int)r->camera_x_q8,scale);
                    segment(s,x+1,center-1,color,y);
                    segment(s,center-1,center+1,((pads|shorts)&(1u<<col))?0xffffu:ROAD_POINT,y);
                    segment(s,center+1,right,color,y);
                } else if((gems&(1u<<col)) && middle>=0 && (int)y>=middle-2 && (int)y<=middle+2) {
                    /* Small diamond marker: widths 1/3/5 around the row middle. */
                    int center=projected_x((int)(col*256+128)-(int)r->camera_x_q8,scale);
                    int dy=(int)y-middle,half=2-(dy<0?-dy:dy);
                    segment(s,x+1,center-half,color,y);
                    segment(s,center-half,center+half+1,ROAD_GEM_CORE,y);
                    segment(s,center+half+1,right,color,y);
                } else segment(s,x+1,right,color,y);
            }
            x=right;
        }
        segment(s,x,ROAD_WIDTH,scene_bg,y);
        previous=row;
    }
}
void road_draw(Road *r,uint32_t camera,int points,const RoadSink *s) { draw(r,camera,points,s,1); }
#ifdef ROAD_BENCHMARK
void road_draw_calculated(Road *r,uint32_t camera,int points,const RoadSink *s) { draw(r,camera,points,s,0); }
#endif
