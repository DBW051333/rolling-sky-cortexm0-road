#ifndef GLASS_H
#define GLASS_H
#include "road.h"
#define GLASS_CELLS 12
/* Each entry animates only one touched cell. Support is never changed. */
typedef struct {
    uint32_t start_z;
    uint16_t row;
    uint8_t column,state;
} GlassFall;
extern GlassFall glass_falls[GLASS_CELLS];
void glass_reset(void);
void glass_update(Road *road,uint32_t z);
void glass_touch(const Road *road,uint32_t row,unsigned column,uint32_t z);
unsigned glass_moving_mask(uint32_t row);
#endif
