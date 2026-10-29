#pragma once
/* Music replacement: mods/music/<bgmId>.mp3|ogg|wav (or the same paths inside any .o2r/.zip in mods/).
 * Add "_once" to the name (<id>_once.mp3) for tracks that must not loop (jingles). */
#ifdef __cplusplus
extern "C" {
#endif
/* Returns 1 when an override took over this BGM id (caller must silence the original sequence). */
int portMusicPlay(int player, unsigned int bgm);
void portMusicStop(int player);
void portMusicSetVolume(int player, unsigned int vol); /* 0..30720, same scale as the sequence player */
int portMusicTrackCount(void);            /* Audio Editor menu: number of music files found in mods/ */
const char* portMusicTrackName(int i);   /* display name of track i */
void portMusicRescan(void);
/* Adds the active override streams on top of one synthesized frame (interleaved stereo s16). */
void portMusicMix(short *pcm, int sampleCount);
#ifdef __cplusplus
}
#endif
