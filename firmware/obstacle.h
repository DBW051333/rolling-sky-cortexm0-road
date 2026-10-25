#ifndef OBSTACLE_H
#define OBSTACLE_H
#include <stdint.h>

/* World-space collision primitives use Q8 tile coordinates. Keep the ball's
 * collision radius below its rendered radius to allow visible near misses. */
#define BALL_COLLISION_RADIUS_Q8 24u

enum { OBSTACLE_SHAPE_BOX=0 };
enum {
    OBSTACLE_BEHAVIOR_STATIC=0,
    OBSTACLE_BEHAVIOR_TRIGGERED=1,
    OBSTACLE_BEHAVIOR_TIMED=2,
    OBSTACLE_BEHAVIOR_MOVING=3
};

/* A generic world-space collider. Animation code updates its pose once, then
 * supplies the same pose to the renderer and this volume; artwork may extend
 * past these deliberately forgiving collision bounds. */
typedef struct {
    int32_t x_min_q8,x_max_q8;
    uint32_t z_min_q8,z_max_q8;
    uint16_t bottom_q8,top_q8;
    uint8_t shape,behavior,kind;
} ObstacleVolume;

int obstacle_volume_from_tile(unsigned kind,uint32_t row,unsigned column,
                              ObstacleVolume *volume);
int obstacle_hits_ball(const ObstacleVolume *volume,int32_t x_q8,
                       uint32_t z_q8,unsigned bottom_q8);

#endif
