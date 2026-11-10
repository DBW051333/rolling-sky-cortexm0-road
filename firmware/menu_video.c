#include "menu_video.h"
#include "sd_levels.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define CONTROL REG(0x400500c0)
#define STATUS REG(0x400500c4)
#define CONSUMED REG(0x400500c8)
#define TIME REG(0x400500cc)
#define UPLOAD REG(0x400500d4)
#define DESC_OFFSET REG(0x400500d8)
#define DESC_META REG(0x400500dc)
#define DESC_CRC REG(0x400500e0)
#define UI_PRESENT REG(0x400500e4)
#define UI_FRONT REG(0x400500e8)
#define TARGET REG(0x400500b0)
extern volatile uint32_t milliseconds;
static uint8_t directory[512];
static uint32_t file_size,data_start,producer,frame_sector,frame_crc,directory_tag;
static unsigned available,running,next_frame,next_time,phase,loading,length,sectors_left;
static unsigned frame_offset,frame_time,ui_back;
static uint32_t le32(const uint8_t *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t crc_bytes(uint32_t c,const uint8_t *p,unsigned n) {
    unsigned bit;
    while(n--) {
        c^=*p++;
        for(bit=0;bit<8;bit++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);
    }
    return c;
}
int menu_video_active(void) { return running!=0; }
uint32_t menu_video_time(void) { return running?TIME:0; }
void menu_video_stop(void) {
    CONTROL=0;
    while((STATUS&4u) || !(STATUS&8u)) {}
    running=0;
    while(REG(0x40050088)&1u) {}
    TARGET=0;
}
int menu_video_init(void) {
    const uint8_t *p;
    uint32_t crc=0xffffffffu,expected,index;
    menu_video_stop();CONTROL=4;
    available=loading=next_frame=next_time=phase=producer=0;directory_tag=0xffffffffu;
    file_size=sd_video_open();
    if(!file_size || !(p=sd_video_sector(0)))return 0;
    if(le32(p)!=0x454d5352u || le32(p+4)!=0x3130554eu ||
       le32(p+8)!=0x02000001u || le32(p+12)!=0x02800168u ||
       le32(p+16)!=0x0001001eu || le32(p+20)!=2400 || le32(p+24)!=80000 ||
       le32(p+28)!=230400 || le32(p+32)!=512 || le32(p+36)!=0x00010010u ||
       le32(p+44)!=file_size || crc_bytes(0xffffffffu,p,508)!=~le32(p+508))return 0;
    data_start=le32(p+40);expected=le32(p+48);
    if(data_start!=38912 || (file_size&511u))return 0;
    for(index=1;index<=75;index++) {
        p=sd_video_sector(index);if(!p)return 0;
        crc=crc_bytes(crc,p,512);
    }
    if(~crc!=expected)return 0;
    available=1;return 1;
}
void menu_video_service(void) {
    uint32_t started=milliseconds;
    if(!available)return;
    if(STATUS&16u) { menu_video_stop();available=0;return; }
    do {
        const uint8_t *p;
        unsigned i;
        if(!loading) {
            uint32_t tag=next_frame>>5,offset,bytes;
            if(!(STATUS&2u))break;
            if(directory_tag!=tag) {
                p=sd_video_sector(tag+1);if(!p)goto failed;
                for(i=0;i<512;i++)directory[i]=p[i];
                directory_tag=tag;
            }
            p=directory+(next_frame&31u)*16;
            offset=le32(p);bytes=le32(p+4);
            if(offset<data_start || (offset&511u) || !bytes || bytes>32766 || (bytes&1u) ||
               offset>file_size || bytes>file_size-offset)goto failed;
            if(((CONSUMED-producer-512u)&0x3fffffu)<((bytes+511u)&~511u))break;
            frame_offset=producer;frame_time=next_time;length=(unsigned)bytes;
            frame_sector=offset>>9;sectors_left=(bytes+511u)>>9;frame_crc=le32(p+12);loading=1;
        }
        p=sd_video_sector(frame_sector);if(!p)goto failed;
        for(i=0;i<512;i+=16) {
            while(!(STATUS&1u)) {}
            UPLOAD=le32(p+i);UPLOAD=le32(p+i+4);UPLOAD=le32(p+i+8);UPLOAD=le32(p+i+12);
        }
        producer=(producer+512u)&0x3fffffu;frame_sector++;sectors_left--;
        if(!sectors_left) {
            while(!(STATUS&1u)) {}
            DESC_OFFSET=frame_offset;DESC_META=(frame_time<<15)|length;DESC_CRC=frame_crc;
            loading=0;next_frame++;
            next_time+=phase==2?34u:33u;phase=phase==2?0:phase+1;
            if(next_frame==2400) { next_frame=0;next_time=0;phase=0; }
        }
    } while((uint32_t)(milliseconds-started)<24u);
    return;
failed:
    menu_video_stop();available=0;
}
void menu_video_start(void) {
    if(!available || running)return;
    while(((STATUS>>16)&511u)<8u && available)menu_video_service();
    if(available) {
        /* Frame fences must use the independent UI cache even when no UI
         * pixels changed after returning from a game. */
        while(REG(0x40050088)&1u) {}
        TARGET=2u+UI_FRONT;
        CONTROL=3;running=1;
    }
}
int menu_video_ui_begin(void) {
    if(!running || (STATUS&32u))return 0;
    ui_back=UI_FRONT^1u;
    while(REG(0x40050088)&1u) {}
    TARGET=2u+ui_back;return 1;
}
void menu_video_ui_present(void) { UI_PRESENT=ui_back; }
