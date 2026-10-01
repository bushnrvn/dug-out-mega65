#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
/* Music and sound effects (src/sound.s). Requests are picked up by the 60 Hz interrupt on the next frame. */
extern volatile uint8_t snd_req_song, snd_req_loop, snd_req_stop, snd_req_sfx, snd_req_pri;
void snd_init(void);       /* start the player (the data must already be at $1A000: see snd_start in main.c) */

enum { SONG_TITLE, SONG_THEME, SONG_CLEAR, SONG_OVER };
enum { SFX_DIG, SFX_SHOOT, SFX_PUMP1, SFX_PUMP2, SFX_PUMP3, SFX_POP, SFX_FALL, SFX_THUD, SFX_DIE, SFX_FLAME, SFX_SQUASH,
       SFX_READY, SFX_ONEUP, SFX_START };

#define snd_song(id, loop) do { snd_req_loop = (loop); snd_req_song = (id); } while (0)
#define snd_stop()         (snd_req_stop = 1)
#define snd_sfx(id, pri)   do { snd_req_pri = (pri); snd_req_sfx = (id); } while (0)
#endif
