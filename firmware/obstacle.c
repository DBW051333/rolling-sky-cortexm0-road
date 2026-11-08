#include "obstacle.h"
#include "level.h"

/* Per-kind local boxes are intentionally narrower than their artwork. The
 * ball radius is included by the sphere-vs-box test, so tune the two together. */
typedef struct {
    unsigned kind;
    uint16_t x_min,x_max,z_min,z_max,bottom,top;
} ObstacleRule;

static const ObstacleRule rules[]={
    /* The low wall occupies the first quarter-row visually. Centering a short
     * box within that band makes its effective depth remain one quarter-row. */
    {TILE_LOW_BARRIER,48,208,24,40,0,COLLISION_GROUND_TOP_Q8},
    /* Tree/pillar archetype: narrower footprint, still too tall to jump over. */
    {TILE_SOLID,48,208,48,208,0,COLLISION_AIR_TOP_Q8},
    /* The beam is a raised, half-row-depth volume; short jumps pass below it. */
    {TILE_AIR_BLOCK,48,208,64,192,COLLISION_AIR_BOTTOM_Q8,COLLISION_AIR_TOP_Q8},
    {TILE_GROUND_BLOCK,48,208,48,208,0,COLLISION_GROUND_TOP_Q8}
};

int obstacle_volume_from_tile(unsigned kind,uint32_t row,unsigned column,
                              ObstacleVolume *volume) {
    unsigned i;
    const ObstacleRule *rule=0;
    if(!volume || column>=5u) return 0;
    for(i=0;i<sizeof(rules)/sizeof(rules[0]);i++)
        if(rules[i].kind==kind) { rule=&rules[i]; break; }
    if(!rule) return 0;
    volume->x_min_q8=(int32_t)(column*256u+rule->x_min);
    volume->x_max_q8=(int32_t)(column*256u+rule->x_max);
    volume->z_min_q8=row*256u+rule->z_min;
    volume->z_max_q8=row*256u+rule->z_max;
    volume->bottom_q8=rule->bottom;
    volume->top_q8=rule->top;
    volume->shape=OBSTACLE_SHAPE_BOX;
    volume->behavior=OBSTACLE_BEHAVIOR_STATIC;
    volume->kind=(uint8_t)kind;
    return 1;
}

static int32_t distance_to_interval(int32_t point,int32_t low,int32_t high) {
    if(point<low) return low-point;
    if(point>high) return point-high;
    return 0;
}

int obstacle_hits_ball(const ObstacleVolume *volume,int32_t x_q8,
                       uint32_t z_q8,unsigned bottom_q8) {
    const int32_t radius=(int32_t)BALL_COLLISION_RADIUS_Q8;
    int32_t dx,dy,dz,center_y;
    if(!volume || volume->shape!=OBSTACLE_SHAPE_BOX) return 0;
    center_y=(int32_t)bottom_q8+radius;
    dx=distance_to_interval(x_q8,volume->x_min_q8,volume->x_max_q8);
    dy=distance_to_interval(center_y,volume->bottom_q8,volume->top_q8);
    dz=distance_to_interval((int32_t)z_q8,(int32_t)volume->z_min_q8,
                            (int32_t)volume->z_max_q8);
    return dx*dx+dy*dy+dz*dz<=radius*radius;
}

/* Mechanism phases advance with travelled distance, not wall-clock time. The
 * unsigned subtraction wraps only for lasers still far ahead, whose masked
 * phase is then arbitrary but harmless: the window checks in level_step keep
 * untested mechanisms out of reach until they matter. */
static uint32_t mech_phase(const LevelMech *mech,uint32_t z) {
    return (z-(uint32_t)mech->row*256u+mech->phase)&((256u<<mech->period_shift)-1u);
}

/* Reach and height live in obstacle.h so the renderer and the collider share
 * one set of figures. The state thresholds encode the reference telegraph:
 * inert nine rows out, the ring warms up across rows nine..five, firing at
 * five. */

unsigned obstacle_laser_state(const LevelMech *mech,uint32_t z) {
    /* Distance REMAINING to the cannon. The 2026-10-29 image had this sign
     * flipped, which lit the full-length beam the moment the cannon entered
     * the view window instead of telegraphing an approach. */
    int32_t d=(int32_t)((uint32_t)mech->row*256u)-(int32_t)z;
    if(d>2304) return 0u;   /* inert: the cannon is nine or more rows ahead */
    if(d>1280) return 1u;   /* charging: ring warms up across rows nine..five */
    return 2u;              /* firing: the reference cannon fires at five */
}

int obstacle_mover_offset(const LevelMech *mech,uint32_t z) {
    uint32_t period=256u<<mech->period_shift,quarter=period>>2;
    uint32_t phase=mech_phase(mech,z),position;
    position=phase<(period>>1)?phase:period-phase;
    return (int)((((int32_t)position-(int32_t)quarter)*(int32_t)mech->param)>>
                 (mech->period_shift+6));
}

/* Pendulum: one sine cycle drives both the sweep across the road and the rise
 * away from it, so the head really hangs from a pivot and arcs instead of
 * teleporting between column bands. */
static const int16_t swing_sine[16]={0,98,181,237,256,237,181,98,
                                     0,-98,-181,-237,-256,-237,-181,-98};
static int swing_sin(const LevelMech *mech,uint32_t z) {
    unsigned index=(mech_phase(mech,z)>>(mech->period_shift+4u))&15u;
    return (swing_sine[index]*196)>>8; /* 50 degree swing */
}
int obstacle_hammer_offset(const LevelMech *mech,uint32_t z) {
    return (swing_sin(mech,z)*1100)>>8;
}
unsigned obstacle_hammer_height(const LevelMech *mech,uint32_t z) {
    int s=swing_sin(mech,z);
    int cosine=256-((s*s)>>9); /* cos(asin(s)) to second order, in Q8 */
    return (unsigned)(1256-((cosine*1100)>>8));
}
/* Fly hammer: the head rides a vertical shaft between the floor and the air
 * on the distance clock. A grounded ball is caught at the bottom of the
 * stroke and passes underneath at the top, matching the reference jackhammer
 * that "can be passed under while raised". param is the stroke amplitude. */
unsigned obstacle_fly_height(const LevelMech *mech,uint32_t z) {
    unsigned index=(mech_phase(mech,z)>>(mech->period_shift+4u))&15u;
    return (unsigned)(450+((int32_t)swing_sine[index]*(int32_t)mech->param>>8));
}

int obstacle_mech_volume(const LevelMech *mech,uint32_t z,ObstacleVolume *volume) {
    if(!volume) return 0;
    volume->shape=OBSTACLE_SHAPE_BOX;
    volume->behavior=OBSTACLE_BEHAVIOR_TIMED;
    volume->kind=mech->kind;
    if(mech->kind==MECH_MOVER) {
        int center=mech->column*256+128+obstacle_mover_offset(mech,z);
        volume->x_min_q8=center-80;
        volume->x_max_q8=center+80;
        volume->z_min_q8=(uint32_t)mech->row*256u+48u;
        volume->z_max_q8=(uint32_t)mech->row*256u+208u;
        volume->bottom_q8=0;
        /* The reference mover is a one-cell cube: hop over it or dodge it. */
        volume->top_q8=256u;
        return 1;
    }
    if(mech->kind==MECH_HAMMER) {
        /* Head box of the swinging pendulum. `param` stays reserved: the pose
         * comes from the shared pivot geometry, not from per-level data. */
        int center=640+obstacle_hammer_offset(mech,z);
        unsigned height=obstacle_hammer_height(mech,z);
        volume->x_min_q8=center-112;
        volume->x_max_q8=center+112;
        volume->z_min_q8=(uint32_t)mech->row*256u+48u;
        volume->z_max_q8=(uint32_t)mech->row*256u+208u;
        /* The volume keeps unsigned heights, so the half cell the head spends
         * below the floor is simply clipped away. */
        volume->bottom_q8=(uint16_t)(height>160u?height-160u:0u);
        volume->top_q8=(uint16_t)(height+160u);
        return 1;
    }
    if(mech->kind==MECH_ROLLER) {
        /* The three-wheel saw group rolls across the track; the whole group
         * shares one box as wide as the wheels. `param` is the sweep
         * amplitude, the same triangle wave the mover uses. */
        int center=mech->column*256+128+obstacle_mover_offset(mech,z);
        /* Two tiles wide, matching the three wheels the renderer draws. The
         * collision used to be 2.6 tiles, which made a saw group close a
         * five-lane road wherever the track narrows to two lanes. */
        volume->x_min_q8=center-256;
        volume->x_max_q8=center+256;
        volume->z_min_q8=(uint32_t)mech->row*256u+48u;
        volume->z_max_q8=(uint32_t)mech->row*256u+208u;
        volume->bottom_q8=0;
        volume->top_q8=280u;
        return 1;
    }
    if(mech->kind==MECH_FLY) {
        unsigned h=obstacle_fly_height(mech,z);
        volume->x_min_q8=mech->column*256+32;
        volume->x_max_q8=mech->column*256+224;
        volume->z_min_q8=(uint32_t)mech->row*256u+48u;
        volume->z_max_q8=(uint32_t)mech->row*256u+208u;
        volume->bottom_q8=(uint16_t)(h>88u?h-88u:0u);
        volume->top_q8=(uint16_t)(h+88u);
        return 1;
    }
    if(mech->kind!=MECH_LASER || obstacle_laser_state(mech,z)!=2u) return 0;
    volume->x_min_q8=mech->column*256+48;
    volume->x_max_q8=mech->column*256+208;
    volume->z_min_q8=(uint32_t)mech->row*256u-LASER_REACH_Q8;
    volume->z_max_q8=(uint32_t)mech->row*256u+208u;
    volume->bottom_q8=0;
    volume->top_q8=LASER_TOP_Q8;
    return 1;
}
