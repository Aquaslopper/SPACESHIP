#pragma once
// Minimal raylib audio ABI for the playback-state test. The client itself
// compiles against pinned raylib, not this fake implementation.
typedef struct rAudioBuffer rAudioBuffer;
typedef struct rAudioProcessor rAudioProcessor;
typedef struct AudioStream { rAudioBuffer* buffer; rAudioProcessor* processor; unsigned int sampleRate, sampleSize, channels; } AudioStream;
typedef struct Wave { unsigned int frameCount, sampleRate, sampleSize, channels; void* data; } Wave;
typedef struct Sound { AudioStream stream; unsigned int frameCount; } Sound;
typedef struct Music { AudioStream stream; unsigned int frameCount; bool looping; int ctxType; void* ctxData; } Music;
void InitAudioDevice();
bool IsAudioDeviceReady();
void CloseAudioDevice();
Wave LoadWaveFromMemory(const char*, const unsigned char*, int);
Sound LoadSound(const char*);
Sound LoadSoundFromWave(Wave);
void UnloadWave(Wave);
void UnloadSound(Sound);
void PlaySound(Sound);
void StopSound(Sound);
bool IsSoundPlaying(Sound);
void SetSoundVolume(Sound, float);
Music LoadMusicStream(const char*);
void UnloadMusicStream(Music);
void PlayMusicStream(Music);
bool IsMusicStreamPlaying(Music);
void UpdateMusicStream(Music);
void StopMusicStream(Music);
void SetMusicVolume(Music, float);
