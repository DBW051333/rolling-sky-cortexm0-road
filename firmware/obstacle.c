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
