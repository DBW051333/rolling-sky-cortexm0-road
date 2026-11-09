#include "walls.h"
#include "level.h"
#include "obstacle.h"
#include "glass.h"
#include "appearance.h"
/* Fixed-size polygon cache plus disjoint color intervals; no pixel buffer. */
typedef struct { int16_t x,y; } Point;
typedef struct {
    Point p[4];
    /* x step per scanline in Q4 (1/16 px). Q8 would need an int32 per edge,
     * which costs 16 bytes a face; at 240 px wide the coarse step is still
     * under a pixel of drift over a whole face, and the cache is the scarce
     * resource here. */
    int16_t slope[4];
    int16_t top,bottom;
    uint32_t color;
} Face;
#define FACE_CACHE 44
#define SCENERY_ROWS 4    /* how far ahead scenery is worth three faces a box */
static Face faces[FACE_CACHE];
static unsigned face_count;
/* Static scenery may not exhaust the cache before the sparse mechanisms are
 * appended, or a busy row of walls would silently erase a laser. */
static unsigned face_limit=32;
static unsigned static_count;   /* faces[0..static_count) is scenery, nearest first */
static unsigned face_foreground;
WallSpan wall_spans[89];   /* 2*FACE_CACHE+1: every face adds two edges */
unsigned wall_span_count;
static int first_y,last_y;
static int quotient(int n,int d) {
    unsigned a=n<0?(unsigned)-n:(unsigned)n,b=(unsigned)d,q=0,bit=1;
    if(!b) return 0;
    while(b<=a && !(b&0x80000000u)) { b<<=1; bit<<=1; }
    while(bit) { if(a>=b) { a-=b; q|=bit; } b>>=1; bit>>=1; }
    return n<0?-(int)q:(int)q;
}
static __attribute__((noinline)) Point project(int x,int z,int h) {
    int d=12*z+20*256-5*h;
    Point p;
    p.x=(int16_t)(ROAD_WIDTH/2+quotient(4992*x,d));
    p.y=(int16_t)(56+quotient(384*(48*256-5*z-12*h),d));
    return p;
}
static void face(Point a,Point b,Point c,Point d,uint32_t color) {
    Face *f;
    unsigned i;
    if(face_count>=face_limit) return;
    f=&faces[face_count++];
    f->p[0]=a; f->p[1]=b; f->p[2]=c; f->p[3]=d;
    f->top=f->bottom=a.y;
    /* RGB565 uses only the low 16 bits. Keep lightweight layer flags in the
     * span color so the road compositor can put a near wall in front of the
     * ball without adding a framebuffer or enlarging every span. */
    f->color=color|(face_foreground?0x20000u:0u);
    for(i=0;i<4;i++) {
        Point u=f->p[i],v=f->p[(i+1)&3];
        if(u.y<f->top) f->top=u.y;
        if(u.y>f->bottom) f->bottom=u.y;
        if(v.y<u.y) { Point temp=u; u=v; v=temp; }
        f->slope[i]=(int16_t)quotient((v.x-u.x)*16,v.y-u.y);
    }
    if(f->top<first_y) first_y=f->top;
    if(f->bottom>last_y) last_y=f->bottom;
}
/* Three visible faces for an axis-aligned solid. */
static __attribute__((noinline)) void box(int x,int z,int width,int depth,int bottom,int top,
                uint16_t front,uint16_t side,uint16_t roof) {
    Point a=project(x,z,bottom),b=project(x+width,z,bottom);
    Point c=project(x+width,z,top),d=project(x,z,top);
    Point e=project(x,z+depth,top),f=project(x+width,z+depth,top);
    if(x+width/2<0) face(b,project(x+width,z+depth,bottom),f,c,side);
    else face(project(x,z+depth,bottom),a,d,e,side);
    face(d,c,f,e,roof);
    face(a,b,c,d,front);
}
void walls_prepare(const Road *r,uint32_t camera) {
    int i,col;
    face_count=0; first_y=ROAD_HEIGHT; last_y=0;
    face_foreground=0;
    if(!r->level) return;
    /* Independent, visual-only glass falls behind the ball. */
    for(i=0;i<GLASS_CELLS;i++) {
        const GlassFall *g=&glass_falls[i];
        uint32_t z_ball=camera+r->depth_q8[ROAD_BALL_Y];
        unsigned age;
        int x,z,h;
        const Appearance *look;
        if(g->state!=2 || z_ball<g->start_z) continue;
        age=z_ball-g->start_z;
        if(age>=384) continue;
        z=(int)g->row*256-(int)camera;
        if(z<512 || z>(int)r->depth_q8[0]+256) continue;
        h=-(int)((age*age)>>7); x=g->column*256-r->camera_x_q8;
        look=appearance_for(r->level,g->row);
        face_foreground=0;
        face(project(x,z,h),project(x+256,z,h),
             project(x+256,z+256,h),project(x,z+256,h),
             0x10000u|(age<128?look->fragile_fall_light:look->fragile_fall_dark));
    }
    /* Object rows are sorted ascending; paint far walls before near walls.
     * The cache pays for 32 scenery faces. Walking back from the far end
     * spent them on scenery that is a few pixels tall: in Massif a tree row
     * filled all 20 and the pillars right in front of the ball were dropped
     * without a word, which reads as "the maze is missing its blocks".
     * Count only as far back as the cache can actually pay for. */
    face_limit=32;
    {
        /* Rows ascend with distance, so walking the array forwards goes from
         * the row the camera just left outwards. Filling the cache nearest
         * first keeps the scenery the player is about to hit and drops the
         * far end, where a pillar is two pixels tall; the old code did the
         * opposite and let a distant tree row evict everything closer. */
        int lo=0;
        while(lo<(int)r->level->tile_count &&
              (int)((uint32_t)r->level->tiles[lo].row*256u)-(int)camera<512) lo++;
        for(i=(unsigned)lo;i<r->level->tile_count;i++) {
        const TileEvent *tile=&r->level->tiles[i];
        const Appearance *look=appearance_for(r->level,tile->row);
        int z=(int)((uint32_t)tile->row*256)-(int)camera;
        if(z<512 || z>(int)r->depth_q8[ROAD_BALL_Y]+SCENERY_ROWS*256) continue;
        /* The ball is fixed at ROAD_BALL_Y. Obstacles whose world row has
         * moved closer to the camera than that plane are composited in front. */
        face_foreground=z<(int)r->depth_q8[ROAD_BALL_Y];
        if(tile->kind==TILE_SOLID || tile->kind==TILE_AIR_BLOCK || tile->kind==TILE_GROUND_BLOCK) {
            int gap=z-(int)r->depth_q8[ROAD_BALL_Y];
            for(col=0;col<5;col++) if(tile->columns&(1u<<col)) {
                int x=col*256-r->camera_x_q8;
                unsigned width=1;
                unsigned model_index=tile->kind==TILE_AIR_BLOCK?look->air_model:
                    tile->kind==TILE_GROUND_BLOCK?look->ground_model:look->solid_model;
                const ArtModel *model=appearance_model(model_index);
                const uint16_t *colors=(const uint16_t *)look;
                unsigned part;
                if(tile->kind==TILE_AIR_BLOCK)
                    while(col+(int)width<5 && (tile->columns&(1u<<(col+width)))) width++;
                for(part=0;part<model->count;part++) {
                    const ArtBox *b=&model->boxes[part];
                    int top=b->top;
                    /* Detail boxes only pay for themselves up close: a tree's
                     * trunk is two pixels tall from three rows out, yet it
                     * used to cost the same three faces as the crown and
                     * pushed the whole row over the cache. */
                    if((b->flags&4u) && gap>512) continue;
                    if(b->flags&2u) {
                        unsigned scale=gap>=1024?8u:gap>512?(unsigned)(1024-gap)>>1:256u;
                        top=b->bottom+((b->top-b->bottom)*(int)scale>>8);
                    }
                    box(x+b->x*(int)width,z+b->z,b->width*(int)width,b->depth,b->bottom,top,
                        colors[b->front],colors[b->side],colors[b->roof]);
                    if(b->flags&1u)
                        face(project(x+b->x*(int)width+28,z+b->z+28,top),
                             project(x+(b->x+b->width)*(int)width-28,z+b->z+28,top),
                             project(x+(b->x+b->width)*(int)width-28,z+b->z+b->depth-28,top),
                             project(x+b->x*(int)width+28,z+b->z+b->depth-28,top),look->solid_inset);
                }
                col+=(int)width-1;
            }
            continue;
        }
        if(tile->kind!=TILE_LOW_BARRIER) continue;
        for(col=0;col<5;col++) if(tile->columns&(1u<<col)) {
            int x=col*256-r->camera_x_q8;
            Point a=project(x,z,0),b=project(x+256,z,0);
            Point c=project(x+256,z,128),d=project(x,z,128);
            Point e=project(x,z+64,128),f=project(x+256,z+64,128);
            if(x+256<0 && !(tile->columns&(1u<<(col+1))))
                face(b,project(x+256,z+64,0),f,c,look->barrier_side);
            if(x>0 && (col==0 || !(tile->columns&(1u<<(col-1)))))
                face(project(x,z+64,0),a,d,e,look->barrier_side);
            face(d,c,f,e,look->barrier_top);
            /* Narrow dark seams make the five blocks read as a wall. */
            a.x++; d.x++; b.x--; c.x--;
            face(a,b,c,d,look->barrier_front);
        }
        }
    }
    static_count=face_count;
    /* Mechanisms are posed by the same distance-driven phase the collision
     * test uses: the cannon charges before it fires, the pendulum hangs from a
     * fixed pivot, and the mover rides a rail that marks its whole travel. */
    face_limit=FACE_CACHE;
    for(i=0;r->level && i<(int)r->level->mech_count;i++) {
        const LevelMech *mech=&r->level->mechs[i];
        uint32_t z_ball=camera+r->depth_q8[ROAD_BALL_Y];
        int z=(int)((uint32_t)mech->row*256u)-(int)camera;
        int x;
        if(z<64 || z>(int)r->depth_q8[0]+400) continue;
        x=mech->column*256-r->camera_x_q8;
        face_foreground=z<(int)r->depth_q8[ROAD_BALL_Y];
        if(mech->kind==MECH_MOVER) {
            int base=x+128;
            int center=base+obstacle_mover_offset(mech,z_ball);
            int reach=112+(int)mech->param;
            const Appearance *look=appearance_for(r->level,mech->row);
            /* The reference mover is a one-cell cube in the level's own block
             * colours, sliding along a thin rail that stays anchored to the
             * base column, so the travel reads before the cube moves. The
             * offset spans +-param, hence the rail half length. */
            box(base-reach,z+160,reach*2,48,0,8,0x18c3u,0x10a2u,0x2945u);
            box(center-112,z+64,224,128,0,224,
                look->solid_front,look->solid_side,look->solid_roof);
        } else if(mech->kind==MECH_HAMMER) {
            int pivot=640-r->camera_x_q8;
            int head=pivot+obstacle_hammer_offset(mech,z_ball);
            unsigned height=obstacle_hammer_height(mech,z_ball);
            /* Arm from the pivot above the track down to the head, then the
             * head itself; both follow the same swing phase. */
            face(project(pivot-8,z+96,1256),project(pivot+8,z+96,1256),
                 project(head+16,z+96,(int)height+40),
                 project(head-16,z+96,(int)height+40),0x7bcfu);
            box(head-112,z+48,224,160,(int)height-160,(int)height+160,
                0xd6bbu,0x8c71u,0xffffu);
        } else if(mech->kind==MECH_ROLLER) {
            /* Three saw wheels on one shared axle rolling across the track;
             * the group pose is the same triangle wave the collider uses, and
             * the 512 Q8 span is exactly the collider's width so what hits is
             * what the player sees. */
            int center=x+128+obstacle_mover_offset(mech,z_ball);
            box(center-256,z+64,160,128,0,256,0xfd20u,0x7a40u,0xffe0u);
            box(center-80,z+64,160,128,0,256,0xfd20u,0x7a40u,0xffe0u);
            box(center+96,z+64,160,128,0,256,0xfd20u,0x7a40u,0xffe0u);
        } else if(mech->kind==MECH_FLY) {
            /* Head on a vertical shaft: slams to the floor and lifts again.
             * The arm drops from above the screen like the pendulum's, and
             * the head height comes from the shared stroke function. */
            unsigned h=obstacle_fly_height(mech,z_ball);
            face(project(x+108,z+96,1256),project(x+148,z+96,1256),
                 project(x+148,z+96,(int)h+60),project(x+108,z+96,(int)h+60),0x7bcfu);
            box(x+32,z+48,192,160,(int)h-88,(int)h+88,0x8c71u,0x528au,0xffffu);
        } else {
            unsigned state=obstacle_laser_state(mech,z_ball);
            /* Painter order far to near: turret, then beam, then the muzzle
             * ring over the joint. The ring is the whole telegraph - grey
             * idle, amber charge, bright cyan rim while firing. */
            box(x+80,z+96,96,96,0,128,0x2945u,0x1082u,0x4a69u);
            if(state==2u) {
                /* Four and a half rows of tube, one tile thick, lying on the
                 * ground; the near end cap is the hot white core, roof and
                 * side carry the cyan glow. */
                int near=z-LASER_REACH_Q8;
                if(near<24) near=24;
                box(x+16,near,224,z+48-near,0,224,0xe7ffu,0x0ad0u,0x05fau);
                face(project(x+88,near,160),project(x+152,near,160),
                     project(x+152,z+40,160),project(x+88,z+40,160),0xd7ffu);
            }
            {
                uint16_t rim=state==2u?0xf858u:state==1u?0xfd20u:0x4a69u;
                uint16_t eye=state==2u?0x0ef8u:state==1u?0x9a40u:0x2945u;
                face(project(x+16,z+48,16),project(x+240,z+48,16),
                     project(x+240,z+48,240),project(x+16,z+48,240),rim);
                face(project(x+36,z+44,36),project(x+220,z+44,36),
                     project(x+220,z+44,220),project(x+36,z+44,220),eye);
                face(project(x+88,z+40,88),project(x+168,z+40,88),
                     project(x+168,z+40,168),project(x+88,z+40,168),0x1082u);
            }
        }
    }
}
/* Each painted face adds at most two boundaries: 44*2+1 = 89. */
static unsigned split_at(int x) {
    unsigned i,j;
    for(i=0;i<wall_span_count;i++) {
        if(x<=wall_spans[i].left) return i;
        if(x<wall_spans[i].right) {
            for(j=wall_span_count;j>i+1;j--) wall_spans[j]=wall_spans[j-1];
            wall_spans[i+1]=wall_spans[i];
            wall_spans[i+1].left=(int16_t)x; wall_spans[i].right=(int16_t)x;
            wall_span_count++;
            return i+1;
        }
    }
    return wall_span_count;
}
/* One face on one scanline: the span between its leftmost and rightmost edge
 * crossing. Split out so the two paint passes below share it instead of
 * carrying a copy each. */
static void paint_face(const Face *f,int y,unsigned *active) {
    int left=32767,right=-32768;
    unsigned edge,begin,end,j;
    if(y<f->top || y>=f->bottom) return;
    for(edge=0;edge<4;edge++) {
        Point a=f->p[edge],b=f->p[(edge+1)&3];
        int hit;
        if(a.y>b.y) { Point t=a; a=b; b=t; }
        if(y<a.y || y>=b.y) continue;
        hit=a.x+((y-a.y)*f->slope[edge]>>4);
        if(hit<left) left=hit;
        if(hit>right) right=hit;
    }
    if(left<0) left=0;
    if(right>ROAD_WIDTH) right=ROAD_WIDTH;
    if(left>=right) return;
    begin=split_at(left); end=split_at(right);
    wall_spans[begin].right=(int16_t)right;
    wall_spans[begin].color=f->color;
    for(j=end;j<wall_span_count;j++) wall_spans[begin+1+j-end]=wall_spans[j];
    wall_span_count-=end-begin-1;
    *active=1;
}
int walls_line(unsigned y) {
    unsigned i,out,active=0;
    wall_span_count=0;
    if((int)y<first_y || (int)y>=last_y) return 0;
    wall_spans[0].left=0; wall_spans[0].right=ROAD_WIDTH; wall_spans[0].color=0;
    wall_span_count=1;
    /* Scenery is collected nearest-first so the cache always holds what the
     * ball is about to hit, which means it has to be painted in reverse;
     * mechanisms follow in their own order and stay on top. */
    for(i=static_count;i--;) paint_face(&faces[i],y,&active);
    for(i=static_count;i<face_count;i++) paint_face(&faces[i],y,&active);
    if(active) {
        out=0;
        for(i=0;i<wall_span_count;i++) {
            if(out && wall_spans[out-1].color==wall_spans[i].color)
                wall_spans[out-1].right=wall_spans[i].right;
            else wall_spans[out++]=wall_spans[i];
        }
        wall_span_count=out;
    }
    return (int)active;
}
