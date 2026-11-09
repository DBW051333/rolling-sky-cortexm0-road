#include "sound.h"
#include <stdint.h>

#if SOUND_ENABLED
/* Four-voice synth in rtl/audio_synth.v, reached through the same AHB port as
 * video and the card reader (0x4007xxxx). The CPU never feeds samples: it
 * writes a note and walks away, and the FPGA keeps the phase running at
 * 31.25 kHz. That is the only way this fits -- the renderer already uses most
 * of a 33 ms frame.
 *
 * Tempo is frame-locked, not sample-locked: one sixteenth every three frames
 * (99 ms, 151 BPM). Music therefore cannot drift away from the game no matter
 * how long a frame takes, which is the property that matters when the beat is
 * supposed to land with the obstacle pattern. */
#define AU(x) (*(volatile uint32_t *)(0x40070000u+4u*(x)))
#define AU_CTRL AU(0)
/* One voice = four words: INC, DELTA, CTL, unused. Voice n starts at 1+4n. */
#define AU_VOICE(n,field) AU(1u+4u*(n)+(field))
#define AU_STATUS AU(17)
#define W_SQUARE 0u
#define W_TRI    1u
#define W_SAW    2u
#define W_NOISE  3u
/* Phase increment for note n at 31.25 kHz: round(440*2^((n-69)/12)*2^24/31250)
 * >> 4, i.e. the top 16 bits so the table stays 120 bytes. Notes 24..83. */
static const uint16_t note_inc[60]={
    1097,1162,1231,1304,1382,1464,1551,1644,1741,1845,1955,2071,
    2194,2325,2463,2609,2765,2929,3103,3288,3483,3691,3910,4143,
    4389,4650,4926,5219,5530,5859,6207,6576,6967,7382,7820,8286,
    8778,9300,9853,10439,11060,11718,12414,13153,13935,14763,15641,16572,
    17557,18601,19707,20879,22120,23436,24829,26306,27870,29527,31283,33143,
};
#if SOUND_BGM_ENABLED
/* Eight bars of A minor, four bars per pass: Am, F, C, G. The lead keeps to
 * the pentatonic so the arpeggio underneath can move without clashing. */
static const uint8_t lead[64]={
    69,0,0,72, 0,0,76,0, 74,0,72,0, 69,0,0,0,
    65,0,0,69, 0,0,72,0, 70,0,69,0, 65,0,0,0,
    67,0,0,72, 0,0,76,0, 79,0,76,0, 72,0,0,0,
    67,0,0,71, 0,0,74,0, 79,0,74,0, 71,0,0,0,
};
static const uint8_t bass[64]={
    45,0,45,0, 45,0,45,0, 45,0,45,0, 45,0,52,0,
    41,0,41,0, 41,0,41,0, 41,0,41,0, 41,0,48,0,
    48,0,48,0, 48,0,48,0, 48,0,48,0, 48,0,55,0,
    43,0,43,0, 43,0,43,0, 43,0,43,0, 43,0,50,0,
};
static const uint8_t arp[64]={
    72,0,76,0, 81,0,76,0, 72,0,76,0, 81,0,76,0,
    65,0,69,0, 72,0,69,0, 65,0,69,0, 72,0,69,0,
    67,0,72,0, 76,0,72,0, 67,0,72,0, 76,0,72,0,
    67,0,71,0, 74,0,71,0, 67,0,71,0, 74,0,71,0,
};
/* The sequencer runs on elapsed milliseconds, not on frames: the menu loops
 * every 10 ms and gameplay every 33, and a 16th note must not speed up four
 * times over just because the screen is idle. */
#define STEP_MS 99u
static unsigned step,step_ms;
#endif
static unsigned v3_ms;
/* Master volume. Scaling happens on the way into the CTL word, so the mix
 * keeps its shape: the lead, the bass and the arp all drop by the same
 * factor instead of the quietest voice disappearing first (which is what a
 * naive post-mix gain does once the 10-bit duty clamps).
 *
 * The scale is Q8 in `scale_q8`, not level*256/10: this image deliberately
 * links no EABI division helper, and -Oz turns a divide by a constant back
 * into one. 26 per step is the same 1/10 within 2%, and the top step is
 * pinned at exactly 256 so full volume is bit-for-bit what it used to be. */
static uint8_t master;
static unsigned scale_q8;
static unsigned scaled(unsigned vol) {
    unsigned v=(vol*scale_q8)>>8;
    return v>15u?15u:v;
}
unsigned sound_volume_get(void) { return master; }
void sound_volume(int delta) {
    int v=(int)master+delta;
    if(v<0) v=0;
    if(v>(int)SOUND_VOLUME_MAX) v=(int)SOUND_VOLUME_MAX;
    master=(uint8_t)v;
    scale_q8=v==(int)SOUND_VOLUME_MAX?256u:(unsigned)v*26u;
}
static unsigned pitch(unsigned note) {
    return note>=24u && note<=83u?(unsigned)note_inc[note-24u]<<4:0u;
}
/* A resting voice is gated off rather than left at volume zero, so the phase
 * stops advancing and a note always starts from the top of its waveform. */
#if SOUND_BGM_ENABLED
static void voice(unsigned index,unsigned note,unsigned wave,unsigned vol) {
    AU_VOICE(index,0)=pitch(note);
    AU_VOICE(index,1)=0;
    AU_VOICE(index,2)=note?wave|(scaled(vol)<<4)|256u:0u;
}
#endif
/* Voice three carries the drums and every effect, since both are short bursts
 * with a pitch slide. Delta is in phase units per 31.25 kHz sample: a whole
 * semitone at 440 Hz is about 22,000, so these are deliberately small. */
static void burst(unsigned note,unsigned wave,unsigned vol,unsigned ms,int delta) {
    AU_VOICE(3,0)=pitch(note);
    AU_VOICE(3,1)=(unsigned)(int)(short)delta;
    AU_VOICE(3,2)=wave|(scaled(vol)<<4)|256u;
    v3_ms=ms;
}
void sound_init(void) {
    unsigned i;
    /* Reset_Handler enters main directly without a C runtime data copy. */
    master=SOUND_VOLUME_MAX; scale_q8=256u;
    AU_CTRL=0;
    for(i=0;i<4u;i++) { AU_VOICE(i,0)=0; AU_VOICE(i,1)=0; AU_VOICE(i,2)=0; }
#if SOUND_BGM_ENABLED
    step=0; step_ms=0;
#endif
    v3_ms=0;
    AU_CTRL=1;
}
void sound_sfx(unsigned id) {
    switch(id) {
    case SFX_GEM:    burst(81,W_SQUARE,9u,66u,30); break;   /* bright ding */
    case SFX_JUMP:   burst(57,W_SQUARE,8u,132u,45); break;  /* rising boing */
    case SFX_HAMMER: burst(48,W_NOISE,12u,165u,-3); break;  /* clunk */
    case SFX_LASER:  burst(76,W_SAW,9u,300u,-30); break;    /* zap with a fall */
    case SFX_DEATH:  burst(64,W_SAW,10u,800u,-8); break;
    case SFX_CLEAR:  burst(72,W_SQUARE,10u,400u,20); break;
    default: break;
    }
}
/* One frame of music. `advance` is 0 whenever the road is not moving -- the
 * level picker, the pause screen, a failed or cleared run -- and that is the
 * whole of the "music only exists inside a level" rule. The three music
 * voices are shut on the way out, otherwise the last chord would drone for as
 * long as the menu stays up. Sound effects own voice three and are untouched,
 * so a death rattle or the clear jingle still lands on the result screen. */
void sound_frame(unsigned ms,int advance) {
#if SOUND_BGM_ENABLED
    unsigned s;
#endif
    if(ms>200u) ms=200u;
    if(v3_ms) {
        if(ms>=v3_ms) { v3_ms=0; AU_VOICE(3,2)=0; }
        else v3_ms-=ms;
    }
#if SOUND_BGM_ENABLED
    if(!advance) {
        AU_VOICE(0,2)=0; AU_VOICE(1,2)=0; AU_VOICE(2,2)=0;
        return;
    }
    step_ms+=ms;
    if(step_ms<STEP_MS) return;
    step_ms-=STEP_MS;
    voice(0,lead[step],W_SQUARE,9u);
    voice(1,bass[step],W_TRI,10u);
    voice(2,arp[step],W_SAW,5u);
    /* Trap-flavoured kit: kick on 1, 4-and and 5, snare on 2 and 4, hats on
     * the off-beats. An effect in flight owns the voice, so the kit sits out
     * rather than clipping it. */
    if(!v3_ms) {
        s=step&15u;
        if(s==0u||s==6u||s==8u) burst(36,W_TRI,12u,99u,-8);
        else if(s==4u||s==12u) burst(60,W_NOISE,10u,66u,0);
        else if(!(s&1u)) burst(72,W_NOISE,5u,33u,0);
    }
    step=(step+1u)&63u;
#else
    // Voices 0..2 stay disabled after sound_init(). Voice 3 is still timed
    // above so effects end normally; the drum sequencer never runs.
    (void)advance;
#endif
}

#else /* SOUND_ENABLED == 0: muted build */

/* CTRL bit 0 is the synth enable, and audio_pwm = enable && (tick < duty), so
 * writing 0 parks the pin at ground. Everything else is compiled out: no
 * register writes at all, no note tables in the image. */
void sound_init(void) { *(volatile uint32_t *)0x40070000u=0u; }
void sound_sfx(unsigned id) { (void)id; }
void sound_frame(unsigned ms,int advance) { (void)ms; (void)advance; }
void sound_volume(int delta) { (void)delta; }
unsigned sound_volume_get(void) { return 0; }

#endif
