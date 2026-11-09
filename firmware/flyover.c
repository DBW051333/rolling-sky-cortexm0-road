#include "flyover.h"
#include "walls.h"
typedef struct { int16_t x,y; } Point;
typedef struct { Point p[4]; int16_t slope[4],top,bottom; uint16_t color; } Face;
/* At most 66 faces (forest); bounds skip most scanline work. */
#define CAPACITY 70
typedef char cache_fits[(sizeof(Face)*CAPACITY<=2120)?1:-1];
static Face *mesh;
static unsigned count,scene,clock_ms;
static int camera_x;
static int divide(int n,int d) {
    unsigned a=n<0?(unsigned)-n:(unsigned)n,b=(unsigned)d,q=0,bit=1;
    if(!b) return 0;
    while(b<=a && !(b&0x80000000u)) { b<<=1;bit<<=1; }
    while(bit) { if(a>=b) {a-=b;q|=bit;} b>>=1;bit>>=1; }
    return n<0?-(int)q:(int)q;
}
unsigned flyover_time(uint32_t ticks) {
    unsigned divisor=2621440000u;
    do { if(ticks>=divisor) ticks-=divisor;divisor>>=1; } while(divisor>=FLYOVER_PERIOD);
    return ticks;
}
static unsigned noise(unsigned n) {
    n=(n^61u)^(n>>16);n*=9;n^=n>>4;n*=0x27d4eb2du;return n^(n>>15);
}
static int bend(unsigned n) {
    n&=31u;return ((int)(n<16?n:32-n)-8)*8;
}
static Point project(int x,int z,int h) {
    Point p;int d;
    if(!scene) {
        p.x=(int16_t)(99+divide(160*x,z));
        p.y=(int16_t)(132+divide(160*h,z));return p;
    }
    d=4*z+3*(256-h);
    p.x=(int16_t)(99+divide(800*(x-camera_x),d));
    p.y=(int16_t)(125+divide(160*(4*(256-h)-3*z),d));return p;
}
static uint16_t fog(uint16_t c,int z) {
    unsigned f=(unsigned)z>>5;
    int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    if(f>26) f=26;
    r+=((16-r)*(int)f)>>5;g+=((36-g)*(int)f)>>5;b+=((22-b)*(int)f)>>5;
    return (uint16_t)(((r<<11)|(g<<5)|b)&0xfff8u);
}
static void quad(Point a,Point b,Point c,Point d,uint16_t color) {
    Face *f;unsigned i;
    int left=a.x,right=a.x,top=a.y,bottom=a.y;
    Point points[4];points[0]=a;points[1]=b;points[2]=c;points[3]=d;
    for(i=1;i<4;i++) {
        if(points[i].x<left)left=points[i].x;if(points[i].x>right)right=points[i].x;
        if(points[i].y<top)top=points[i].y;if(points[i].y>bottom)bottom=points[i].y;
    }
    if(count==CAPACITY || right<0 || left>=ROAD_WIDTH || bottom<0 || top>=ROAD_HEIGHT) return;
    f=&mesh[count++];f->color=color;f->top=(int16_t)top;f->bottom=(int16_t)bottom;
    for(i=0;i<4;i++) {
        Point u=points[i],v=points[(i+1)&3];int slope;
        f->p[i]=u;if(v.y<u.y) {Point t=u;u=v;v=t;}
        slope=divide((v.x-u.x)*256,v.y-u.y);
        if(slope>32767)slope=32767;if(slope< -32768)slope= -32768;
        f->slope[i]=(int16_t)slope;
    }
}
static void plane(int x,int z,int w,int depth,int h,uint16_t c) {
    quad(project(x,z,h),project(x+w,z,h),project(x+w,z+depth,h),project(x,z+depth,h),c);
}
static void tower(int x,int z,int width,int h,unsigned seed) {
    Point a=project(x,z,0),b=project(x+width,z,0),c=project(x+width,z,h),d=project(x,z,h);
    Point e=project(x,z+74,h),f=project(x+width,z+74,h);
    uint16_t front=fog((seed&1)?0x6372u:0x52cfu,z),side=fog(0x2149u,z),roof=fog(0xbdf8u,z);
    if(x<0)quad(b,project(x+width,z+74,0),f,c,side);
    else quad(project(x,z+74,0),a,d,e,side);
    quad(d,c,f,e,roof);
    /* Material bit 0 is discarded by RGB332, and marks facade windows. */
    quad(a,b,c,d,(uint16_t)((front&0xfff8u)|1u));
    quad(project(x+8,z+6,h+4),project(x+width-8,z+6,h+4),
         project(x+width-8,z+48,h+4),project(x+8,z+48,h+4),fog(0x8410u,z));
}
static void crown(int x,int z,int w,int h,unsigned seed) {
    Point top=project(x,z+22,h),bottom=project(x,z-8,h-56);
    uint16_t light=fog((seed&1)?0x4cc8u:0x65aau,z),dark=fog(0x1ae6u,z);
    quad(top,project(x-w*3/4,z+20,h-8),project(x-w,z,h-32),bottom,light);
    quad(top,bottom,project(x+w,z,h-32),project(x+w*3/4,z+20,h-8),dark);
}
static void mountain(int x,int z,int w,int h) {
    Point peak=project(x+w/2,z+70,h);
    quad(project(x,z,0),project(x+w,z,0),peak,peak,fog(0x8c4du,z));
    quad(project(x+w,z,0),project(x+w,z+120,0),peak,peak,fog(0x426au,z));
    quad(project(x+w/2-w/8,z+48,h-32),project(x+w/2+w/8,z+48,h-32),
         peak,peak,fog(0xefffu,z));
}
void flyover_prepare(unsigned time) {
    unsigned local,band,position;
    scene=time<20000?0:time<40000?1:time<60000?2:3;
    local=time-scene*20000u;
    clock_ms=local;position=local>>4;
    camera_x=(bend(position>>7)+((bend((position>>7)+1)-bend(position>>7))*(int)(position&127u)>>7))>>2;
    mesh=(Face *)walls_menu_cache();count=0;
    if(!scene) {
        for(band=6;band--;) {
            unsigned world=(position>>7)+band,j;
            int z=(int)(band*128+128)-(int)(position&127u);
            if(z<48)continue;
            for(j=0;j<8;j++) {
                unsigned seed=noise(world*97+j*37);
                Point p=project((int)(seed&1023u)-512,z,(int)((seed>>10)&1023u)-512);
                int s=z<220?2:1;
                quad(p,(Point){p.x+s,p.y},(Point){p.x+s,p.y+s},(Point){p.x,p.y+s},
                     z<240?0xffffu:0x9cbfu);
            }
            if(z>100) {
                unsigned seed=noise(world*41);
                int x=(int)(seed&255u)-128,y=(int)((seed>>8)&255u)-128;
                Point top=project(x,z,y-20),left=project(x-20,z,y),bottom=project(x,z,y+24),right=project(x+26,z,y);
                quad(top,left,bottom,bottom,0x8c72u);quad(top,bottom,right,right,0x31a9u);
            }
        }
        return;
    }
    for(band=scene==2?4u:5u;band--;) {
        unsigned world=(position>>7)+band,j,seed=noise(world*31);
        int z=(int)(band*128)-(int)(position&127u),far=z+128;
        int a=bend(world),b=bend(world+1);
        if(z<0) {a+=(b-a)*(-z)/128;z=0;}
        if(scene==1) {
            int high=100+(int)(seed&31u),next=100+(int)(noise((world+1)*31)&31u);
            if(z==0)high+=(next-high)*(int)(position&127u)/128;
            quad(project(-650,z,high),project(a-170,z,28),project(b-170,far,28),project(-650,far,next),fog(0x6270u,z));
            quad(project(a-170,z,28),project(a-26,z,0),project(b-26,far,0),project(b-170,far,28),fog(0x5d2bu,z));
            quad(project(a+26,z,0),project(a+170,z,28),project(b+170,far,28),project(b+26,far,0),fog(0x3289u,z));
            quad(project(a+170,z,28),project(650,z,high),project(650,far,next),project(b+170,far,28),fog(0x39c9u,z));
            quad(project(a-26,z,0),project(a+26,z,0),project(b+26,far,0),project(b-26,far,0),fog(0x2c99u,z));
            if(z>=48) {
                mountain(a-230,z,130,140+(int)(seed&63u));
                mountain(a+80,z+16,160,120+(int)((seed>>7)&63u));
                plane(a-8,z+24,16,16,1,fog(0x9f3fu,z));
            }
        } else if(scene==2) {
            plane(-650,z,1300,far-z,0,fog(0x52acu,z));
            plane(-42,z,84,far-z,1,fog(0x2107u,z));
            plane(-2,z+14,4,42,2,fog(0xffd7u,z));
            if(z<48)continue;
            plane(-170,z,480,96,1,fog(0x2127u,z));
            /* Two towers on each side, tall and varied, frame the avenue. */
            for(j=4;j--;) {
                unsigned s=noise(world*97+j*53);
                int x=(j&1u)?64+(int)(j>>1)*100:-140-(int)(j>>1)*100;
                tower(x,z+(int)(j>>1)*20,58+(int)(s&15u),100+(int)((s>>5)&127u),s);
            }
        } else {
            plane(-650,z,1300,far-z,0,fog(0x2225u,z));
            quad(project(a-12,z,1),project(a+12,z,1),project(b+12,far,1),project(b-12,far,1),fog(0x23acu,z));
            if(z<40)continue;
            plane(a-210,z+24,460,70,1,fog(0x19c4u,z));
            quad(project(a+40,z+30,0),project(a+46,z+30,0),project(a+46,z+30,55),project(a+40,z+30,55),fog(0x72e7u,z));
            for(j=6;j--;) {
                unsigned s=noise(world*131+j*41);
                int offset=54+(int)(j>>1)*72;
                crown(a+((j&1u)?offset:-offset),z+24+(int)(j>>1)*28,
                      42+(int)(s&15u),92+(int)((s>>6)&31u),s);
            }
        }
    }
}
void flyover_line(uint16_t *pixels,unsigned y) {
    unsigned x,i;
    uint16_t sky=scene==0?0x0803u:y<40?0x4bf6u:y<80?0x8d5au:0x9d97u;
    for(x=0;x<ROAD_WIDTH;x++) {
        uint16_t c=sky;
        if(!scene) {
            int dx=(int)x-151+(int)(clock_ms>>10),dy=(int)y-105-(int)(clock_ms>>12),d=dx*dx+dy*dy;
            if(((x*17u+y*37u)&127u)==0 && (noise(x+y*198)&7u)==0)c=0x8412u;
            if(d<1764) {
                c=dx+dy/2< -12?0x96bfu:dx+dy/2<28?0x2c57u:0x1108u;
                if(((y+(unsigned)(dx*dx>>5))&15u)<3 && dx+dy/2<28)c=0xb7bfu;
            }
        } else if(y>68) {
            unsigned ridge=(x+37u)&63u;if(ridge>32)ridge=64-ridge;
            if(scene==2)ridge=20+(((x>>3)*37u)^((x>>3)*7u))%32u;
            if(y>78+ridge/2)c=scene==2?0x5b91u:scene==3?0x3b49u:0x6372u;
        }
        pixels[x]=c;
    }
    for(i=0;i<count;i++) {
        Face *f=&mesh[i];int left=32767,right=-32768;unsigned edge;
        if((int)y<f->top || (int)y>=f->bottom)continue;
        for(edge=0;edge<4;edge++) {
            Point a=f->p[edge],b=f->p[(edge+1)&3];int hit;
            if(a.y>b.y){Point t=a;a=b;b=t;}
            if((int)y<a.y || (int)y>=b.y)continue;
            hit=a.x+(((int)y-a.y)*f->slope[edge]>>8);
            if(hit<left)left=hit;if(hit>right)right=hit;
        }
        if(left>0)left--;if(left<0)left=0;if(right>ROAD_WIDTH)right=ROAD_WIDTH;
        {
        unsigned width=(unsigned)(right-left),mask=width>=48?7u:width>=24?3u:1u;
        for(;left<right;left++) {
            uint16_t c=f->color;
            if(scene==2 && (c&1u)) {
                if((y&mask)<(mask>>1)+1 && ((unsigned)left&mask)<(mask>>1)+1)c=0xaddbu;
            }
            pixels[left]=c;
        }
        }
    }
    /* A short cinematic fade conceals the space/terrain camera change. */
    if(clock_ms<512 || clock_ms>19488) {
        unsigned v=clock_ms<512?clock_ms>>6:(20000-clock_ms)>>6;
        for(x=0;x<ROAD_WIDTH;x++) {
            unsigned c=pixels[x],r=((c>>11)*v)>>3,g=(((c>>5)&63u)*v)>>3,b=((c&31u)*v)>>3;
            pixels[x]=(uint16_t)((r<<11)|(g<<5)|b);
        }
    }
}
