#include <userland/applications/games/DOOM/linuxdoom-1.10/i_sound.h>

void I_InitSound(void) {}
void I_UpdateSound(void) {}
void I_SubmitSound(void) {}
void I_ShutdownSound(void) {}
void I_SetChannels(void) {}
int I_GetSfxLumpNum(sfxinfo_t *sfxinfo) {
    return sfxinfo ? sfxinfo->lumpnum : 0;
}
int I_StartSound(int id, int vol, int sep, int pitch, int priority) {
    (void)id; (void)vol; (void)sep; (void)pitch; (void)priority;
    return 1;
}
void I_StopSound(int handle) { (void)handle; }
int I_SoundIsPlaying(int handle) { (void)handle; return 0; }
void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) {
    (void)handle; (void)vol; (void)sep; (void)pitch;
}
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) { (void)volume; }
void I_PauseSong(int handle) { (void)handle; }
void I_ResumeSong(int handle) { (void)handle; }
int I_RegisterSong(void *data) { (void)data; return 1; }
void I_PlaySong(int handle, int looping) { (void)handle; (void)looping; }
void I_StopSong(int handle) { (void)handle; }
void I_UnRegisterSong(int handle) { (void)handle; }
