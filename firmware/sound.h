#ifndef SOUND_H
#define SOUND_H
/* Master switch. 0 compiles the synthesiser out entirely: sound_init() only
 * clears CTRL so the PWM pin sits at zero (no hum, no buzz), and the other two
 * entry points become empty, so the frame costs nothing and the 1.2 kB of note
 * tables leave the image. Set back to 1 to restore audio (music additionally
 * requires SOUND_BGM_ENABLED); the level code keeps raising Road::sfx either way. */
#ifndef SOUND_ENABLED
#define SOUND_ENABLED 1
#endif
/* Development default: mute the background score and drums, retain effects.
 * Set to 1 and rebuild to restore the level BGM. SOUND_ENABLED remains the
 * separate master switch for all audio. */
#ifndef SOUND_BGM_ENABLED
#define SOUND_BGM_ENABLED 0
#endif
/* Event ids carried in Road::sfx; the renderer never looks at them, main.c
 * drains the word once per frame. */
enum { SFX_GEM=1, SFX_JUMP=2, SFX_HAMMER=4, SFX_LASER=8, SFX_DEATH=16, SFX_CLEAR=32 };
/* Master volume in steps, 0 = mute. Every voice's 4-bit level is scaled by
 * this, so the balance between lead, bass, arp and kit survives a change. */
#define SOUND_VOLUME_MAX 10
void sound_init(void);
void sound_frame(unsigned elapsed_ms,int advance);
void sound_sfx(unsigned id);
void sound_volume(int delta);
unsigned sound_volume_get(void);
#endif
