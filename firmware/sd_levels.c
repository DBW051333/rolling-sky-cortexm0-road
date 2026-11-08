#include "sd_levels.h"
#define IO(a) (*(volatile uint32_t *)(a))
#define SPI IO(0x40060000)
#define CS IO(0x40060004)
#define DIV IO(0x40060008)
extern volatile uint32_t milliseconds;
static uint8_t sector[512],rows[1024];
static uint32_t row_tags[2],fat_lba,data_lba,clusters,part_end,spc;
static uint32_t root_cluster;
static uint32_t first_cluster,file_bytes,cursor_cluster,cursor_index,level_start;
static uint32_t catalog[SD_LEVEL_MAX][2];
static uint32_t gem_offsets[SD_LEVEL_MAX],mech_offsets[SD_LEVEL_MAX];
static unsigned count,row_shift,row_stride,format_version;
static uint16_t level_ids[SD_LEVEL_MAX];
static TileEvent visible[64];
static LevelGem sd_gems[GEM_TABLE_MAX];
static LevelMech sd_mechs[MECH_TABLE_MAX];
Level sd_level;
volatile unsigned sd_diag_stage,sd_diag_response,sd_diag_detail;
static uint32_t le32(const uint8_t *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned le16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static uint32_t divide(uint32_t n,uint32_t d) {
    uint32_t q=0,bit=1;
    if(!d) return 0;
    while(d<=n && !(d&0x80000000u)) { d<<=1; bit<<=1; }
    while(bit) { if(n>=d) { n-=d; q|=bit; } d>>=1; bit>>=1; }
    return q;
}
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
    for(i=0;i<16;i++) { response=transfer(255); if(!(response&128)) { sd_diag_response=response; return response; } }
    sd_diag_response=255; return 255;
}
static int initialize(void) {
    unsigned i,r;
    uint32_t started=milliseconds,ocr;
    DIV=63; CS=1;
    for(i=0;i<12;i++) (void)transfer(255);
    do { r=command(0,0); } while(r!=1 && (uint32_t)(milliseconds-started)<500);
    if(r!=1) { release(); return 0; }
    sd_diag_stage=2;
    if(command(8,0x1aa)!=1) { release(); return 0; }
    ocr=0;
    for(i=0;i<4;i++) ocr=(ocr<<8)|transfer(255);
    sd_diag_detail=ocr;
    /* Only the accepted voltage and check pattern are defined for this
     * host; do not reject cards that drive nonzero upper/reserved bits. */
    if((ocr&0xffffu)!=0x1aa) { release(); return 0; }
    sd_diag_stage=3;
    started=milliseconds;
    do {
        if(command(55,0)>1) { release(); return 0; }
        r=command(41,0x40000000);
    } while(r==1 && (uint32_t)(milliseconds-started)<1500);
    sd_diag_stage=4;
    if(r || command(58,0)) { release(); return 0; }
    ocr=0; for(i=0;i<4;i++) ocr=(ocr<<8)|transfer(255);
    release();
    sd_diag_detail=ocr;
    if((ocr&0xc0000000u)!=0xc0000000u) return 0; /* SDHC block addresses. */
    sd_diag_stage=5;
    DIV=3; /* 4 MHz, conservative board wiring rate. */
    return 1;
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
    uint32_t wanted=divide(index,spc),steps=0;
    if(index>=((file_bytes+511)>>9)) return 0;
    if(wanted<cursor_index) { cursor_index=0; cursor_cluster=first_cluster; }
    while(cursor_index<wanted) {
        cursor_cluster=next_cluster(cursor_cluster); cursor_index++;
        if(!valid_cluster(cursor_cluster) || ++steps>clusters) return 0;
    }
    return read_sector(cluster_lba(cursor_cluster)+index-wanted*spc,target);
}
unsigned sd_levels_mount(void) {
    uint32_t start=0,total,fat_size,root,c,steps;
    unsigned i,s,found=0;
    static const char name[]="RSLEVELSBIN";
    count=0; part_end=0xffffffffu; sd_diag_stage=1; sd_diag_response=255; sd_diag_detail=0;
    if(!initialize()) return 0;
    sd_diag_stage=6;
    if(!read_sector(0,sector)) return 0;
    sd_diag_stage=7;
    if(le16(sector+510)!=0xaa55) return 0;
    if(le16(sector+11)!=512 || sector[16]==0) {
        for(i=0;i<4;i++) {
            const uint8_t *p=sector+446+i*16;
            if(p[4]==0x0b || p[4]==0x0c) { start=le32(p+8); break; }
        }
        if(!start || !read_sector(start,sector)) return 0;
    }
    sd_diag_stage=8;
    spc=sector[13]; total=le32(sector+32); fat_size=le32(sector+36);
    if(le16(sector+11)!=512 || !spc || (spc&(spc-1)) || spc>128 ||
       !le16(sector+14) || !sector[16] || sector[16]>2 || !fat_size ||
       le16(sector+17) || le16(sector+22) || !total || start+total<start ||
       le16(sector+510)!=0xaa55 || (le16(sector+40)&0x80u)) return 0;
    part_end=start+total;
    fat_lba=start+le16(sector+14);
    data_lba=fat_lba+sector[16]*fat_size;
    if(data_lba<fat_lba || data_lba>=part_end) return 0;
    clusters=divide(part_end-data_lba,spc);
    if(clusters<65525 || clusters>0x0ffffff5u || fat_size<divide(clusters+129,128)) return 0;
    sd_diag_stage=9;
    root=le32(sector+44)&0x0fffffffu; root_cluster=root;
    c=root;
    for(steps=0;valid_cluster(c) && steps<clusters && !found;steps++) {
        for(s=0;s<spc && !found;s++) {
            if(!read_sector(cluster_lba(c)+s,sector)) return 0;
            for(i=0;i<512;i+=32) {
                unsigned k;
                const uint8_t *p=sector+i;
                if(!p[0]) return 0;
                if(p[0]==0xe5 || (p[11]&0x18) || p[11]==0x0f) continue;
                for(k=0;k<11 && p[k]==(uint8_t)name[k];k++) {}
                if(k==11) {
                    first_cluster=((uint32_t)le16(p+20)<<16)|le16(p+26);
                    file_bytes=le32(p+28); found=1; break;
                }
            }
        }
        if(!found) c=next_cluster(c);
    }
    if(!found || !valid_cluster(first_cluster) || file_bytes<512 || file_bytes>0x01000000u) return 0;
    sd_diag_stage=10;
    cursor_cluster=first_cluster; cursor_index=0;
    if(!file_sector(0,sector)) return 0;
    sd_diag_stage=11;
    for(i=0;i<7;i++) if(sector[i]!=(uint8_t)"RSLEVEL"[i]) return 0;
    if(sector[7]<'1' || sector[7]>'4') return 0;
    format_version=sector[7]-'0';
    row_shift=format_version>=2?6:7; row_stride=format_version>=2?8:4;
    count=le16(sector+8);
    if(!count || count>SD_LEVEL_MAX || le16(sector+10)!=row_stride) { count=0; return 0; }
    for(i=0;i<count;i++) {
        uint32_t n=le32(sector+16+i*8),offset=le32(sector+20+i*8);
        if(!n || n>65535 || !offset || offset>file_bytes/512 ||
           n*row_stride>file_bytes-offset*512) { count=0; return 0; }
        catalog[i][0]=n; catalog[i][1]=offset;
        level_ids[i]=(uint16_t)(row_stride==8?le16(sector+208+i*2):i+1);
    }
    /* V4 adds a per-level extension directory in sector 0: sparse gem and
     * mechanism tables stored past the row data, referenced by sector. */
    if(format_version>=4) {
        uint32_t sectors=file_bytes/512;
        for(i=0;i<count;i++) {
            uint32_t gem=le32(sector+256+i*8),mech=le32(sector+260+i*8);
            if((gem && gem>=sectors) || (mech && mech>=sectors)) { count=0; return 0; }
            gem_offsets[i]=gem; mech_offsets[i]=mech;
        }
    }
    sd_diag_stage=12;
    return count;
}
unsigned sd_levels_count(void) { return count; }
unsigned sd_levels_id(unsigned index) { return index<count?level_ids[index]:0; }
/* Menu readout: the pack revision and how many mechanisms the selected level
 * actually carries. A V4 pack written before mechanisms existed answers 0,
 * which is what "insert the card and still see no laser" looks like. */
unsigned sd_levels_format(void) { return format_version; }
unsigned sd_levels_mech_count(unsigned index) {
    unsigned total;
    if(index>=count || format_version<4 || !mech_offsets[index]) return 0;
    if(!file_sector(mech_offsets[index],sector)) return 0;
    total=le16(sector);
    /* Same ceiling as load_tables and sd_mechs[]: 32 since the Massif decode
     * left the cap at 24, which made the menu report M00 for a 32-mechanism
     * level and read as "the card has no mechanisms". */
    return total>MECH_TABLE_MAX?0:total;
}
static int load_chunk(uint32_t chunk) {
    unsigned slot=chunk&1u,i;
    uint8_t *target=rows+slot*512;
    if((chunk<<row_shift)>=sd_level.row_count) {
        for(i=0;i<512;i++) target[i]=0;
    } else if(!file_sector(level_start+chunk,target)) return 0;
    for(i=0;i<512;i+=row_stride) {
        unsigned k,objects=0;
        for(k=0;k<row_stride;k++) {
            if(target[i+k]&224u || (k && k!=TILE_AIR_BLOCK && (target[i+k]&~target[i]))) return 0;
            if(k && k!=TILE_FRAGILE) objects|=target[i+k];
        }
        if(target[i+3]&objects) return 0;
        if(format_version<3 && row_stride==8 && (target[i+5] || target[i+6] || target[i+7])) return 0;
        if(format_version>=3 && (target[i+1]&target[i+6])) return 0;
    }
    row_tags[slot]=chunk;
    return 1;
}
/* Sparse side tables: gems (3 B each) and mechanisms (9 B each). Both must
 * arrive sorted by row because the runtime advances cursors over them. */
static int load_tables(unsigned index) {
    unsigned i;
    sd_level.gems=sd_gems; sd_level.mechs=sd_mechs;
    sd_level.gem_count=0; sd_level.mech_count=0;
    if(format_version<4) return 1;
    if(gem_offsets[index]) {
        unsigned total;
        if(!file_sector(gem_offsets[index],sector)) return 0;
        total=le16(sector);
        if(total>GEM_TABLE_MAX) return 0;
        for(i=0;i<total;i++) {
            unsigned row=le16(sector+2+i*3);
            if(row>=sd_level.row_count || sector[4+i*3]>=5 ||
               (i && row<sd_gems[i-1].row)) return 0;
            sd_gems[i].row=(uint16_t)row;
            sd_gems[i].column=sector[4+i*3];
        }
        sd_level.gem_count=(uint8_t)total;
    }
    if(mech_offsets[index]) {
        unsigned total;
        if(!file_sector(mech_offsets[index],sector)) return 0;
        total=le16(sector);
        if(total>MECH_TABLE_MAX) return 0;
        for(i=0;i<total;i++) {
            const uint8_t *p=sector+2+i*9;
            unsigned row=le16(p),kind=p[3],shift=p[4],param;
            if(row>=sd_level.row_count || p[2]>=5 || kind<1 || kind>5 ||
               shift<2 || shift>5 || le16(p+5)>=(256u<<shift)) return 0;
            param=le16(p+7);
            if(kind==MECH_HAMMER ? (param<256u || param>680u)
               : param>(kind==MECH_LASER?640u:kind==MECH_FLY?900u:512u)) return 0;
            if(i && row<sd_mechs[i-1].row) return 0;
            sd_mechs[i].row=(uint16_t)row;
            sd_mechs[i].column=p[2];
            sd_mechs[i].kind=(uint8_t)kind;
            sd_mechs[i].period_shift=(uint8_t)shift;
            sd_mechs[i].phase=(uint16_t)le16(p+5);
            sd_mechs[i].param=(uint16_t)param;
        }
        sd_level.mech_count=(uint8_t)total;
    }
    return 1;
}
int sd_levels_begin(unsigned index,Road *road,uint32_t *camera) {
    if(index>=count) return 0;
    level_start=catalog[index][1];
    sd_level.ground=0; sd_level.ground_count=0;
    sd_level.tiles=visible; sd_level.tile_count=0;
    sd_level.row_count=(uint16_t)catalog[index][0];
    sd_level.id=level_ids[index];
    row_tags[0]=row_tags[1]=0xffffffffu;
    sd_diag_stage=14;
    if(!load_chunk(0)) return 0;
    sd_diag_stage=15;
    if(!load_chunk(1)) return 0;
    sd_diag_stage=16;
    if(!load_tables(index)) return 0;
    level_load(road,&sd_level,camera);
    return sd_levels_prepare(road,*camera);
}
unsigned sd_levels_cell(uint32_t row,unsigned field) {
    if(field>=row_stride || row>=sd_level.row_count || row_tags[(row>>row_shift)&1u]!=(row>>row_shift)) return 0;
    return rows[(row&((1u<<(row_shift+1))-1u))*row_stride+field]&31u;
}
int sd_levels_prepare(Road *road,uint32_t camera) {
    uint32_t first=(camera+road->depth_q8[ROAD_HEIGHT-1])>>8;
    uint32_t last=((camera+road->depth_q8[0])>>8)+1;
    /* Keep seven rows behind the ball available for connected-glass lookup. */
    uint32_t chunk=(first>8?first-8:0)>>row_shift,row;
    unsigned n=0,kind;
    if(row_tags[chunk&1u]!=chunk && !load_chunk(chunk)) return 0;
    if(row_tags[(chunk+1)&1u]!=chunk+1 && !load_chunk(chunk+1)) return 0;
    for(row=first;row<=last && row<sd_level.row_count;row++) for(kind=1;kind<=7;kind++) {
        unsigned mask;
        if(kind==TILE_FRAGILE || kind==TILE_JUMP || kind==TILE_SHORT_JUMP) continue; /* Glass geometry is handled by the ground layer. */
        mask=sd_levels_cell(row,kind);
        if(mask) {
            /* 64 slots cover every tile event a 14-row window can hold, so a
             * packed level truncates nothing and only real I/O fails here. */
            if(n==64) return 0;
            visible[n].row=(uint16_t)row; visible[n].columns=(uint8_t)mask;
            visible[n++].kind=(uint8_t)kind;
        }
    }
    sd_level.tile_count=(uint8_t)n;
    return 1;
}

/* Read a separate art file, preserving the map file cursor and metadata. */
int sd_art_load(uint8_t *target,unsigned capacity) {
    uint32_t saved_first=first_cluster,saved_bytes=file_bytes;
    uint32_t saved_cursor=cursor_cluster,saved_index=cursor_index;
    uint32_t c=root_cluster,steps,s;
    unsigned i,k,found=0,ok=0;
    static const char name[]="RSART   BIN";
    for(steps=0;valid_cluster(c) && steps<clusters && !found;steps++) {
        for(s=0;s<spc && !found;s++) {
            if(!read_sector(cluster_lba(c)+s,sector)) goto done;
            for(i=0;i<512;i+=32) {
                const uint8_t *p=sector+i;
                if(!p[0]) goto done;
                if(p[0]==0xe5 || (p[11]&0x18) || p[11]==0x0f) continue;
                for(k=0;k<11 && p[k]==(uint8_t)name[k];k++) {}
                if(k==11) {
                    first_cluster=((uint32_t)le16(p+20)<<16)|le16(p+26);
                    file_bytes=le32(p+28); found=1; break;
                }
            }
        }
        if(!found) c=next_cluster(c);
    }
    if(!found || !valid_cluster(first_cluster) || file_bytes<512 ||
       file_bytes>capacity || (file_bytes&511u)) goto done;
    cursor_cluster=first_cluster; cursor_index=0;
    for(i=0;i<(file_bytes>>9);i++) if(!file_sector(i,target+i*512)) goto done;
    ok=1;
 done:
    first_cluster=saved_first; file_bytes=saved_bytes;
    cursor_cluster=saved_cursor; cursor_index=saved_index;
    return (int)ok;
}
