#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))
#define SPI REG(0x40060000u)
#define CS REG(0x40060004u)
#define DIV REG(0x40060008u)
#define AU_CTRL REG(0x40070000u)
#define AU_DATA REG(0x40070048u)
/* PCM_DATA is write-only; the same word reads the request toggle + duty. */
#define AU_STATUS REG(0x40070048u)
volatile uint32_t milliseconds;
void tick_isr(void) { milliseconds++; }
void key_isr(void) { REG(0x40000000u)=1; }
void uart_isr(void) { REG(0x40000108u)=8; }

static uint8_t sector[512];
static uint32_t part_end,fat_lba,data_lba,root_cluster,clusters,spc;
static uint32_t first_cluster,file_bytes,cursor_cluster,cursor_index;
static unsigned row_shift;

static uint32_t le32(const uint8_t *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned le16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static uint32_t div_pow2(uint32_t n,unsigned shift) { return n>>shift; }
static uint8_t transfer(uint8_t value) {
    uint32_t state;
    SPI=value;
    do { state=SPI; } while(state&256u);
    return (uint8_t)state;
}
static void release(void) { CS=1; (void)transfer(255); }
static unsigned command(unsigned cmd,uint32_t arg) {
    unsigned i,response;
    release(); CS=0; (void)transfer(255);
    (void)transfer((uint8_t)(64|cmd));
    for(i=0;i<4;i++) (void)transfer((uint8_t)(arg>>(24-i*8)));
    (void)transfer(cmd==0?0x95:cmd==8?0x87:1);
    for(i=0;i<16;i++) { response=transfer(255); if(!(response&128)) return response; }
    return 255;
}
static int read_sector(uint32_t lba,uint8_t *target) {
    unsigned i,token;
    uint32_t started=milliseconds;
    if(lba>=part_end || command(17,lba)) { release(); return 0; }
    do { token=transfer(255); } while(token==255 && (uint32_t)(milliseconds-started)<250);
    if(token!=254) { release(); return 0; }
    for(i=0;i<512;i++) target[i]=transfer(255);
    (void)transfer(255); (void)transfer(255); release();
    return 1;
}
static int valid_cluster(uint32_t c) { return c>=2 && c<clusters+2; }
static uint32_t next_cluster(uint32_t c) {
    if(!valid_cluster(c) || !read_sector(fat_lba+(c>>7),sector)) return 0;
    return le32(sector+(c&127u)*4)&0x0fffffffu;
}
static uint32_t cluster_lba(uint32_t c) { return data_lba+(c-2)*spc; }
static int file_sector(uint32_t index,uint8_t *target) {
    uint32_t wanted=div_pow2(index,row_shift),steps=0;
    if(index>=((file_bytes+511)>>9)) return 0;
    if(wanted<cursor_index) { cursor_index=0; cursor_cluster=first_cluster; }
    while(cursor_index<wanted) {
        cursor_cluster=next_cluster(cursor_cluster); cursor_index++;
        if(!valid_cluster(cursor_cluster) || ++steps>clusters) return 0;
    }
    return read_sector(cluster_lba(cursor_cluster)+index-(wanted<<row_shift),target);
}
static int mount_music(void) {
    static const char name[]="RSMUSIC PCM";
    uint32_t start=0,total,fat_size,c,steps;
    unsigned i,s,k;
    DIV=63; CS=1;
    for(i=0;i<12;i++) (void)transfer(255);
    if(command(0,0)!=1 || command(8,0x1aa)!=1) return 0;
    for(i=0;i<4;i++) (void)transfer(255);
    do { if(command(55,0)>1) return 0; } while(command(41,0x40000000)==1);
    if(command(58,0)) return 0;
    for(i=0;i<4;i++) (void)transfer(255);
    if(!read_sector(0,sector)) return 0;
    if(le16(sector+11)!=512 || sector[16]==0) {
        for(i=0;i<4;i++) {
            const uint8_t *p=sector+446+i*16;
            if(p[4]==0x0b || p[4]==0x0c) { start=le32(p+8); break; }
        }
        if(!start || !read_sector(start,sector)) return 0;
    }
    total=le32(sector+32); fat_size=le32(sector+36); spc=sector[13];
    if(le16(sector+11)!=512 || !spc || (spc&(spc-1)) || spc>128 ||
       !le16(sector+14) || !sector[16] || !fat_size || !total) return 0;
    for(row_shift=0;(1u<<row_shift)<spc;row_shift++) {}
    part_end=start+total; fat_lba=start+le16(sector+14);
    data_lba=fat_lba+sector[16]*fat_size; clusters=(part_end-data_lba)>>row_shift;
    root_cluster=le32(sector+44)&0x0fffffffu; c=root_cluster;
    for(steps=0;valid_cluster(c) && steps<clusters;steps++) {
        for(s=0;s<spc;s++) {
            if(!read_sector(cluster_lba(c)+s,sector)) return 0;
            for(i=0;i<512;i+=32) {
                const uint8_t *p=sector+i;
                if(!p[0]) return 0;
                if(p[0]==0xe5 || (p[11]&0x18) || p[11]==0x0f) continue;
                for(k=0;k<11 && p[k]==(uint8_t)name[k];k++) {}
                if(k==11) {
                    first_cluster=((uint32_t)le16(p+20)<<16)|le16(p+26);
                    file_bytes=le32(p+28); cursor_cluster=first_cluster;
                    cursor_index=0; return valid_cluster(first_cluster) && file_bytes>=512;
                }
            }
        }
        c=next_cluster(c);
    }
    return 0;
}
static void wait_sample(unsigned *last) {
    unsigned now;
    do { now=(AU_STATUS>>10)&1u; } while(now==*last);
    *last=now;
}
static void play_music(void) {
    uint32_t total=((file_bytes+511)>>9),index;
    unsigned i,last=(AU_STATUS>>10)&1u;
    AU_CTRL=3; /* enable + PCM mode */
    for(index=0;index<total;index++) {
        if(!file_sector(index,sector)) break;
        for(i=0;i<512 && (index<<9)+i<file_bytes;i++) {
            wait_sample(&last); AU_DATA=sector[i];
        }
    }
    AU_CTRL=0;
}
int main(void) {
    REG(0xe000e014u)=31999; REG(0xe000e018u)=0; REG(0xe000e010u)=7;
    REG(0xe000e100u)=1; milliseconds=0; AU_CTRL=0;
    if(mount_music()) for(;;) play_music();
    for(;;) { AU_CTRL=0; }
}
