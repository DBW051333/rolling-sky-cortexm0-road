#include "walls.h"
#include "level.h"
#include "obstacle.h"
#include "glass.h"
#include "appearance.h"
/* Fixed-size polygon cache plus disjoint color intervals; no pixel buffer. */
typedef struct { int16_t x,y; } Point;
typedef struct {
    Point p[4];
    int32_t slope[4];
    int16_t top,bottom;
    uint32_t color;
} Face;
static Face faces[32];
static unsigned face_count;
static unsigned face_foreground;
WallSpan wall_spans[67];
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
    p.x=(int16_t)(120+quotient(4992*x,d));
    p.y=(int16_t)(56+quotient(384*(48*256-5*z-12*h),d));
    return p;
}
static void face(Point a,Point b,Point c,Point d,uint32_t color) {
    Face *f;
    unsigned i;
    if(face_count==32) return;
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
        f->slope[i]=quotient((v.x-u.x)*256,v.y-u.y);
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
    /* Object rows are sorted ascending; paint far walls before near walls. */
    for(i=(int)r->level->tile_count-1;i>=0;i--) {
        const TileEvent *tile=&r->level->tiles[i];
        const Appearance *look=appearance_for(r->level,tile->row);
        int z=(int)((uint32_t)tile->row*256)-(int)camera;
        if(z<512 || z>(int)r->depth_q8[0]+256) continue;
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
    /* Dynamic mechanisms take their pose from the same distance-driven phase
     * the collision test uses; the cannon stays visible while the beam rests,
     * so the warning window reads as "about to fire". Marker colors are fixed
     * until skins catch up. */
    for(i=0;r->level && i<(int)r->level->mech_count;i++) {
        const LevelMech *mech=&r->level->mechs[i];
        uint32_t z_ball=camera+r->depth_q8[ROAD_BALL_Y];
        int z=(int)((uint32_t)mech->row*256u)-(int)camera;
        int x;
        if(z<320 || z>(int)r->depth_q8[0]+400) continue;
        x=mech->column*256-r->camera_x_q8;
        face_foreground=z<(int)r->depth_q8[ROAD_BALL_Y];
        if(mech->kind==MECH_MOVER)
            box(x-80+obstacle_mover_offset(mech,z_ball),z+48,160,160,0,512,
                0xf81fu,0x9813u,0xffdfu);
        else if(mech->kind==MECH_HAMMER) {
            static const int16_t bounds[3][2]={{24,488},{560,720},{792,1256}};
            /* Bands are absolute road coordinates; the nominal column is
             * unused for a hammer sweeping the whole track. */
            int base=-r->camera_x_q8;
            unsigned index=obstacle_hammer_stage(mech,z_ball);
            int left=bounds[index][0],right=bounds[index][1];
            int center=left+(right-left)/2,top=mech->param;
            /* Head box plus a thin arm rising to the swing pivot. */
            box(base+left,z+64,right-left,128,0,top,0xffe0u,0xd29bu,0xffffu);
            face(project(base+center-12,z+128,960),project(base+center+12,z+128,960),
                 project(base+center+12,z+128,top),project(base+center-12,z+128,top),0x7befu);
        }
        else if(mech->kind==MECH_LASER) {
            unsigned state=obstacle_laser_state(mech,z_ball);
            box(x+56,z+56,144,144,0,320,0x7befu,0x39e7u,0x9cd3u);
            if(state)
                box(x+80,z-(int)mech->param,96,(int)mech->param+152,0,896,
                    state==2u?0xf800u:0xfd20u,
                    state==2u?0xb900u:0xa500u,0xffe0u);
        }
    }
}
/* Each painted face adds at most two boundaries: 32*2+1 <= 67. */
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
int walls_line(unsigned y) {
    unsigned i;
    int active=0;
    wall_span_count=0;
    if((int)y<first_y || (int)y>=last_y) return 0;
    wall_spans[0].left=0; wall_spans[0].right=ROAD_WIDTH; wall_spans[0].color=0;
    wall_span_count=1;
    for(i=0;i<face_count;i++) {
        const Face *f=&faces[i];
        int left=32767,right=-32768;
        unsigned edge;
        if((int)y<f->top || (int)y>=f->bottom) continue;
        for(edge=0;edge<4;edge++) {
            Point a=f->p[edge],b=f->p[(edge+1)&3];
            int hit;
            if(a.y>b.y) { Point t=a; a=b; b=t; }
            if((int)y<a.y || (int)y>=b.y) continue;
            hit=a.x+(((int)y-a.y)*f->slope[edge]>>8);
            if(hit<left) left=hit;
            if(hit>right) right=hit;
        }
        if(left<0) left=0;
        if(right>ROAD_WIDTH) right=ROAD_WIDTH;
        if(left<right) {
            unsigned begin=split_at(left),end=split_at(right),j;
            wall_spans[begin].right=(int16_t)right;
            wall_spans[begin].color=f->color;
            for(j=end;j<wall_span_count;j++) wall_spans[begin+1+j-end]=wall_spans[j];
            wall_span_count-=end-begin-1;
            active=1;
        }
    }
    if(active) {
        unsigned out=0;
        for(i=0;i<wall_span_count;i++) {
            if(out && wall_spans[out-1].color==wall_spans[i].color)
                wall_spans[out-1].right=wall_spans[i].right;
            else wall_spans[out++]=wall_spans[i];
        }
        wall_span_count=out;
    }
    return active;
}
