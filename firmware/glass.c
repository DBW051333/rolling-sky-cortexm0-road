#include "glass.h"
GlassFall glass_falls[GLASS_CELLS];
void glass_reset(void) {
    unsigned i;
    for(i=0;i<GLASS_CELLS;i++) glass_falls[i].state=0;
}
unsigned glass_moving_mask(uint32_t row) {
    unsigned i,mask=0;
    for(i=0;i<GLASS_CELLS;i++) {
        const GlassFall *g=&glass_falls[i];
        if(g->state==2 && g->row==row) mask|=1u<<g->column;
    }
    return mask;
}
void glass_update(Road *road,uint32_t z) {
    unsigned i;
    (void)road;
    for(i=0;i<GLASS_CELLS;i++) {
        GlassFall *g=&glass_falls[i];
        if(!g->state || z<g->start_z) continue;
        if(z-g->start_z>=512) g->state=0;
        else g->state=2;
    }
}
void glass_touch(const Road *road,uint32_t row,unsigned column,uint32_t z) {
    unsigned i,slot=GLASS_CELLS;
    (void)road; (void)z;
    for(i=0;i<GLASS_CELLS;i++) {
        const GlassFall *g=&glass_falls[i];
        if(g->state && g->row==row && g->column==column) return;
        if(!g->state) slot=i;
    }
    if(slot==GLASS_CELLS) return;
    glass_falls[slot].row=(uint16_t)row;
    glass_falls[slot].column=(uint8_t)column;
    /* Start falling behind the ball, after it leaves this row. */
    glass_falls[slot].start_z=(row+1)*256;
    glass_falls[slot].state=1;
}
