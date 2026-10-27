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

unsigned obstacle_laser_state(const LevelMech *mech,uint32_t z) {
    uint32_t period=256u<<mech->period_shift,half=period>>1;
    uint32_t phase=mech_phase(mech,z);
    if(phase>=half) return 2u;              /* firing */
    if(phase>=half-(period>>4)) return 1u;  /* warning window before firing */
    return 0u;                              /* safe */
}

int obstacle_mover_offset(const LevelMech *mech,uint32_t z) {
    uint32_t period=256u<<mech->period_shift,quarter=period>>2;
    uint32_t phase=mech_phase(mech,z),position;
    position=phase<(period>>1)?phase:period-phase;
    return (int)((((int32_t)position-(int32_t)quarter)*(int32_t)mech->param)>>
                 (mech->period_shift+6));
}

/* Hammer stages map 0..3 to band indices left/middle/right/middle. */
unsigned obstacle_hammer_stage(const LevelMech *mech,uint32_t z) {
    unsigned stage=mech_phase(mech,z)>>(mech->period_shift+6u);
    return stage==2u?2u:(stage?1u:0u);
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
        volume->top_q8=COLLISION_GROUND_TOP_Q8; /* low enough to jump over */
        return 1;
    }
    if(mech->kind==MECH_HAMMER) {
        /* Discrete swing approximation: quarter-period stages sweep
         * left -> middle -> right -> middle, each a fixed column band. */
        static const int16_t bounds[3][2]={{24,488},{560,720},{792,1256}};
        unsigned index=obstacle_hammer_stage(mech,z);
        volume->x_min_q8=bounds[index][0];
        volume->x_max_q8=bounds[index][1];
        volume->z_min_q8=(uint32_t)mech->row*256u+48u;
        volume->z_max_q8=(uint32_t)mech->row*256u+208u;
        volume->bottom_q8=0;
        volume->top_q8=mech->param; /* head height; the peak jump clears it */
        return 1;
    }
    if(mech->kind!=MECH_LASER || obstacle_laser_state(mech,z)!=2u) return 0;
    volume->x_min_q8=mech->column*256+48;
    volume->x_max_q8=mech->column*256+208;
    /* The beam reaches back toward the approaching player and covers the
     * cannon cell; full height, so the only answers are timing or a lane. */
    volume->z_min_q8=(uint32_t)mech->row*256u-(uint32_t)mech->param;
    volume->z_max_q8=(uint32_t)mech->row*256u+208u;
    volume->bottom_q8=0;
    volume->top_q8=COLLISION_AIR_TOP_Q8;
    return 1;
}
