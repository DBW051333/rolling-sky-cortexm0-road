#ifndef SD_LEVELS_H
#define SD_LEVELS_H
#include "level.h"
#define SD_LEVEL_MAX 24
extern Level sd_level;
extern volatile unsigned sd_diag_stage,sd_diag_response,sd_diag_detail;
unsigned sd_levels_mount(void);
int sd_art_load(uint8_t *target,unsigned capacity);
unsigned sd_levels_count(void);
unsigned sd_levels_id(unsigned index);
int sd_levels_begin(unsigned index,Road *road,uint32_t *camera);
int sd_levels_prepare(Road *road,uint32_t camera);
unsigned sd_levels_cell(uint32_t row,unsigned field);
#endif
