/* Host-only renderer for the menu video encoder. Uses the production
 * flyover geometry and shading, with no title, controls or skin overlay. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "../firmware/flyover.h"

static union { uint32_t alignment; uint8_t bytes[2120]; } mesh_cache;
static uint16_t scanline[ROAD_WIDTH];
static uint8_t frame[ROAD_WIDTH*ROAD_HEIGHT];
void *walls_menu_cache(void) { return mesh_cache.bytes; }
void road_fill_pixels(uint16_t *pixels,unsigned count,uint16_t color) {
    while(count--) *pixels++=color;
}
static void le16(unsigned value) {
    putchar((int)(value&255u));putchar((int)((value>>8)&255u));
}
static void le32(uint32_t value) {
    le16(value&65535u);le16(value>>16);
}
int main(int argc,char **argv) {
    unsigned fps=argc>1?(unsigned)strtoul(argv[1],0,10):30;
    unsigned index,x,y,frames;
    if(!fps || fps>60) { fprintf(stderr,"FPS must be between 1 and 60\n");return 1; }
#ifdef _WIN32
    if(_setmode(_fileno(stdout),_O_BINARY)==-1) return 1;
#endif
    frames=(FLYOVER_PERIOD/1000)*fps;
    if(fwrite("RSRAW001",1,8,stdout)!=8) return 1;
    le16(ROAD_WIDTH);le16(ROAD_HEIGHT);le16(fps);le16(0);
    le32(frames);le32(FLYOVER_PERIOD);
    for(index=0;index<frames;index++) {
        unsigned time=(unsigned)((uint64_t)index*1000/fps);
        flyover_prepare(time);
        for(y=0;y<ROAD_HEIGHT;y++) {
            flyover_line(scanline,y);
            for(x=0;x<ROAD_WIDTH;x++) {
                uint16_t c=scanline[x];
                frame[y*ROAD_WIDTH+x]=(uint8_t)(((c>>8)&0xe0u)|((c>>6)&0x1cu)|((c>>3)&3u));
            }
        }
        if(fwrite(frame,1,sizeof(frame),stdout)!=sizeof(frame)) return 1;
        if((index+1)%fps==0) {
            fprintf(stderr,"Rendered %u/%u frames\n",index+1,frames);
            fflush(stderr);
        }
    }
    return fflush(stdout)!=0;
}
