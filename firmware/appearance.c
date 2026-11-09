#include "appearance.h"
#include "level.h"
#include "sd_levels.h"
typedef struct { uint16_t level_id,first_row,end_row; uint8_t preset; } AppearanceCue;
#include "appearance_data.h"
_Static_assert(sizeof(Appearance)==56,"Artwork palette layout");
_Static_assert(sizeof(ArtModel)==68,"Artwork model layout");
volatile unsigned art_sd_loaded;
static const uint8_t *const art=(const uint8_t *)ART_CACHE_ADDRESS;
static unsigned word(unsigned at) { return art[at]|(unsigned)art[at+1]<<8; }
static uint32_t dword(unsigned at) { return word(at)|(uint32_t)word(at+2)<<16; }
static int region(unsigned at,unsigned n,unsigned size) { return at>=64 && !(at&1u) && at<=size && n<=size-at; }
/* The pack no longer carries a bitmap panorama: the four scene backdrops are
 * generated from a sky ramp plus a silhouette profile in road.c, which costs
 * 2.3 kB of code instead of 2.7 kB of cache per picture and gives each scene
 * its own. Header slots 20..28 are kept (and must be zero) so an older pack
 * is rejected by the size check rather than drawn as garbage. */
void appearance_load(int card) {
    unsigned i,j,size,looks,cues,models;
    uint32_t crc=0xffffffffu;
    art_sd_loaded=0;
    if(!card || !sd_art_load((uint8_t *)ART_CACHE_ADDRESS,ART_CACHE_BYTES)) return;
    for(i=0;i<8;i++) if(art[i]!=(uint8_t)"RSART002"[i]) return;
    size=dword(8);
    if(size<64 || size>ART_CACHE_BYTES) return;
    for(i=64;i<size;i++) {
        crc^=art[i];
        for(j=0;j<8;j++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0);
    }
    if((crc^0xffffffffu)!=dword(12)) return;
    looks=word(32); cues=word(36); models=word(40);
    if(word(18)!=144 || !looks || looks>8 || cues>32 || !models || models>4 ||
       !region(word(30),looks*56,size) || !region(word(34),cues*8,size) ||
       !region(word(38),models*68,size)) return;
    for(i=0;i<looks;i++) {
        const Appearance *p=(const Appearance *)(art+word(30)+i*56);
        if(p->solid_model>=models || p->air_model>=models || p->ground_model>=models) return;
    }
    for(i=0;i<cues;i++) {
        const AppearanceCue *c=(const AppearanceCue *)(art+word(34)+i*8);
        if(c->first_row>=c->end_row || c->preset>=looks) return;
    }
    for(i=0;i<models;i++) {
        const ArtModel *m=(const ArtModel *)(art+word(38)+i*68);
        if(!m->count || m->count>4) return;
        for(j=0;j<m->count;j++) {
            const ArtBox *b=&m->boxes[j];
            if(b->x<0 || b->z<0 || b->width<=0 || b->depth<=0 ||
               b->x+b->width>256 || b->z+b->depth>256 ||
               b->bottom<0 || b->top<=b->bottom || b->top>512 ||
               b->front>=26 || b->side>=26 || b->roof>=26 || (b->flags&~7u)) return;
        }
    }
    art_sd_loaded=1;
}
const Appearance *appearance_for(const Level *level,unsigned row) {
    unsigned i,count=sizeof(appearance_cues)/sizeof(appearance_cues[0]);
    const AppearanceCue *cues=appearance_cues;
    const Appearance *looks=appearances;
    if(art_sd_loaded) {
        count=word(36); cues=(const AppearanceCue *)(art+word(34));
        looks=(const Appearance *)(art+word(30));
    }
    if(level) for(i=0;i<count;i++) {
        const AppearanceCue *c=&cues[i];
        if(level->id==c->level_id && row>=c->first_row && row<c->end_row)
            return &looks[c->preset];
    }
    return &looks[0];
}
/* The row range a scene covers, so a caller can do something once per scene
 * instead of on a clock. road.c uses it to flip the backdrop to its second
 * look at the middle of each stretch: Massif's devil and fantasy stretches are
 * only 102 and 60 rows long, and a "every N rows of travel" timer simply never
 * ticked inside them, so two of the eight skies were never drawn at all. */
unsigned appearance_cue_span(const Level *level,unsigned row,unsigned *first,unsigned *end) {
    unsigned i,count=sizeof(appearance_cues)/sizeof(appearance_cues[0]);
    const AppearanceCue *cues=appearance_cues;
    if(art_sd_loaded) { count=word(36); cues=(const AppearanceCue *)(art+word(34)); }
    if(level) for(i=0;i<count;i++) {
        const AppearanceCue *c=&cues[i];
        if(level->id==c->level_id && row>=c->first_row && row<c->end_row) {
            *first=c->first_row; *end=c->end_row;
            return 1;
        }
    }
    return 0;
}
/* Which slot of the active palette bank a look points at. The scene backdrop
 * is chosen by this index, so a card that reorders or recolours the palettes
 * still gets a matching sky without any extra table in the pack. */
unsigned appearance_index(const Appearance *look) {
    const Appearance *base=appearances,*p;
    unsigned count=sizeof(appearances)/sizeof(appearances[0]),i;
    if(art_sd_loaded) { base=(const Appearance *)(art+word(30)); count=word(32); }
    for(i=0,p=base;i<count;i++,p++) if(p==look) return i;
    return 0;
}
const ArtModel *appearance_model(unsigned index) {
    static const ArtModel fallback={1,0,{{0,0,256,256,0,256,12,13,14,3}}};
    static const ArtModel air_fallback={1,0,{{0,64,256,128,384,512,23,24,25,0}}};
    static const ArtModel ground_fallback={1,0,{{0,0,256,256,0,128,9,10,11,1}}};
    if(art_sd_loaded && index<word(40)) return (const ArtModel *)(art+word(38)+index*68);
    return index==2?&air_fallback:index==3?&ground_fallback:&fallback;
}
