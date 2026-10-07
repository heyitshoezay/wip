/*============================================================\
|  This file was made by TheGameratorT.                       |
|                                                             |
|  This code is meant to work with the following template:    |
|  https://github.com/Overblade/NSMB-ASMReference             |
|                                                             |
|  You may modify this file and use it for whatever you want  |
|  just be sure to credit me (TheGameratorT).                 |
|                                                             |
|  Hope you like it just as much as I had fun coding this!    |
|                                                             |
|  ---------------------------------------------------------  |
|                                                             |
|  NWAV player core.                                          |
|  This is main code that allows for music playback on DS.    |
\============================================================*/

#include "../include/NWAVPlayer.h"
#include "../include/types.h"
#include "../include/config.h"
#include "../include/debug.h"
#include "../include/sound.h"
//#include "Memory.h"


/*==============================================================\
|  Player settings, change these according to your game needs.  |
\==============================================================*/

#define CHANNEL_L_NUM 0
//#define CHANNEL_R_NUM 1
#define CHANNEL_MASK (1 << CHANNEL_L_NUM )//| ((1 << CHANNEL_R_NUM) * hInfo.stereo))
#define ALARM_NUM 7
#define STREAM_THREAD_PRIO 1
#define THREAD_STACK_SIZE 2048 // 1024 is tight for FS_ReadFile plus the card driver underneath it
#define STRM_BUF_PAGESIZE (128 * 32) // 4096 bytes = 64 ms of 32 kHz 16 bit mono per page (was 2048). a bigger page rides out longer game loads without a stutter

// 1 = the game thread and the stream thread take turns on the file (recommended). 0 = no locking at all, the way it was before.
#define NWAV_USE_LOCK 1

// The loop points in the NWAV header: 1 = the converter writes them as BYTE offsets (what this port has always assumed)
//                                     0 = the converter writes them as SAMPLE numbers (the original NWAV format)
#define NWAV_LOOP_POINTS_ARE_BYTES 1
//#define STRM_BUF_PAGESIZE_STEREO (STRM_BUF_PAGESIZE * 2)
#define STRM_BUF_SIZE (STRM_BUF_PAGESIZE * 2)


// 1 = print player messages to the debugger. Off by default: every message string costs ROM space, and the region this code
// is linked into is very small (a link error "region 'rom' overflowed" means it is full).
#define NWAV_DEBUG 0
#if NWAV_DEBUG
#define NWAV_LOG(...) debug_printf(__VA_ARGS__)
#else
#define NWAV_LOG(...) ((void)0)
#endif

#ifndef MATH_CLAMP
#define MATH_CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

// Route these to hg-engine's internal memory allocators
// Adjust "0" if you need to allocate to a specific heap ID (like the SOUND heap)
#define NWAV_ALLOC(size) sys_AllocMemory(0, size) 
#define NWAV_FREE(ptr)   sys_FreeMemoryEz(ptr)

#define OS_MESSAGE_NOBLOCK 0
#define OS_MESSAGE_BLOCK 1

#define SND_TIMER_CLOCK 16756991

// Route OS_Panic to Game Freak's native crash handler
#ifndef OS_Panic
#define OS_Panic() GF_ASSERT(FALSE)
#endif

/*==========================\
|  Structure declarations.  |
\==========================*/

typedef struct EventInfo
{
    int eventID;
    int sample;
} EventInfo;

typedef struct Header
{
    int magic;
    int fileSize;
    int sampleRate;
    int loopStart;
    int loopEnd;
    SNDWaveFormat format : 8;
    u8 stereo;
    u8 numEvents;
    u8 padding;
} Header;

// Assuming EventHandler is defined in NWAVPlayer.h as: typedef void (*EventHandler)(int);
typedef struct StreamInfo
{
    u8 isPlaying;
    u8 isPaused;
    u8 stopWait; // updates to wait before a finished track is really stopped
    u8 reserved;

    fx32 speed;
    int volume;

    int bufPage;
    int musicEnd;
    int musicCursor;
    int samplesPerUpdate;
    int playRate;
    int targetVolume;
    int fadeDec;
    int fadeFrame;
    u8 loops;
    u8 stopMode;
    u8 chCount;
    u8 bytesPerSample;

    int eventIDBlockSize;
    int eventBlockSize;

    //EventHandler eventHandler;
} StreamInfo;

#define HEADER_SIZE sizeof(Header)
_Static_assert(sizeof(Header) == 24, "NWAV header must be 24 bytes to match the converter output");
#define EVENT_INFO_SIZE sizeof(EventInfo)

/*=======================\
|  Static declarations.  |
\=======================*/

static Header hInfo;
static StreamInfo sInfo;
static FSFile file;

static u8* strmThreadStack;
static OSThread* strmThread = NULL;
static OSMessageQueue msgQ;
static OSMessage msgBuf;

static EventInfo* events;

// Binary lock built from a message queue (the only blocking primitive this hack has symbols for).
// The stream thread reads the file; the game thread opens, closes and seeks it. Without this they can do both at once.
static OSMessageQueue lockQ;
static OSMessage lockBuf;
static BOOL lockReady = FALSE;

static void lock(void)
{
#if NWAV_USE_LOCK
    OSMessage m;
    if (lockReady)
        OS_ReceiveMessage(&lockQ, &m, OS_MESSAGE_BLOCK);
#endif
}

static void unlock(void)
{
#if NWAV_USE_LOCK
    if (lockReady)
        OS_SendMessage(&lockQ, (OSMessage)1, OS_MESSAGE_NOBLOCK);
#endif
}

//typedef u8(*pStrmBufT)[2][STRM_BUF_SIZE];
//static pStrmBufT pStrmBuf;

static u8* pStrmBufL = NULL;
//static u8* pStrmBufR = NULL;

/*=========================\
|  Function declarations.  |
\=========================*/

static void update(StreamInfo* sInfo);
static void StrmThread(void* arg);
static void stop_internal_locked(int frames, BOOL waitForUpdate);
static void setPaused_locked(BOOL paused);
	


//Goes to a certain position in the file, either based on byte index or music sample.
static void seek(int pos, BOOL sample)
{
    if (sample)
    {
        //Calculate file absolute file position.
        //pos = alignSample(pos);
        sInfo.musicCursor = pos;

        //pos *= sInfo.chCount;
        pos *= sInfo.bytesPerSample;
        pos += HEADER_SIZE;
        pos += sInfo.eventBlockSize;

        //debug_printf("Seek Offset: %d (Sample: %d)\n", pos, sInfo.musicCursor);
    }

    //Seek position in file.
    FS_SeekFile(&file, pos, FS_SEEK_SET);
}

//Use this before playing or resuming music. (Keeps buffer aligned and makes it update instantly)
static void prepareBuffer(void)
{
    sInfo.bufPage = 0;
    update(&sInfo);
    update(&sInfo);
}

//Gets if the music is paused.
BOOL NWAVPlayer_getPaused(void) { return sInfo.isPaused; }

//Gets if a streamed music is playing.
BOOL NWAVPlayer_isPlaying(void) { return sInfo.isPlaying; }

//Sets if the music is paused by stopping or starting the timers. (the caller must hold the lock)
static void setPaused_locked(BOOL paused)
{
    //If music is playing and stopped flag isn't the one already set.
    if (sInfo.isPlaying && sInfo.isPaused != paused)
    {
        if (paused)
        {
            SND_StopTimer(CHANNEL_MASK, 0, 1 << ALARM_NUM, 0);
        }
        else
        {
            prepareBuffer();
            SND_StartTimer(CHANNEL_MASK, 0, 1 << ALARM_NUM, 0);
        }
        sInfo.isPaused = paused;
    }
}

//Sets if the music is paused.
void NWAVPlayer_setPaused(BOOL paused)
{
    lock();
    setPaused_locked(paused);
    unlock();
}

//Gets the current volume.
int NWAVPlayer_getVolume(void) { return sInfo.volume; }

//Sets the volume by shifting it during the specified frame period.
void NWAVPlayer_setVolume(int volume, int frames)
{
    //Do nothing if volume doesn't change.
    if (sInfo.volume == volume)
        return;

    if (frames == 0)
    {
        SND_SetChannelVolume(CHANNEL_MASK, volume, SND_CHANNEL_DATASHIFT_NONE);
        sInfo.volume = volume;
    }
    else
    {
        int volumeDiff = sInfo.volume - volume;
        int remove = volumeDiff > 0 ? 1 : -1;

        sInfo.fadeDec = (volumeDiff / frames) + remove;
        sInfo.fadeFrame = frames;
        sInfo.targetVolume = volume;
    }
}

//Stops the music. (For internal use ONLY, the caller must hold the lock)
static void stop_internal_locked(int frames, BOOL waitForUpdate)
{
    //Do nothing if not playing.
    if (!sInfo.isPlaying)
        return;

    //A fade from volume 0 never starts (the volume does not change), so the music would never stop. Stop it right away instead.
    if (frames && !waitForUpdate && sInfo.volume == 0)
        frames = 0;

    if (frames || waitForUpdate)
    {
        //Begin special sound stop.
        if (frames)
            NWAVPlayer_setVolume(0, frames);
        sInfo.stopMode = waitForUpdate + 1;
        sInfo.stopWait = waitForUpdate ? 1 : 0;
    }
    else
    {
        //Stop music timers.
        setPaused_locked(TRUE);
        sInfo.isPlaying = FALSE;
        sInfo.stopMode = 0;
        sInfo.fadeFrame = 0;

        //Close the file.
        FS_CloseFile(&file);

        //Free memory (Removed C++ new/delete and NSMBDS heap code)
        if (hInfo.numEvents && events != NULL)
            NWAV_FREE(events);
            
       // if (pStrmBuf != NULL)
       //     NWAV_FREE(pStrmBuf); 

        //if (pStrmBufL != NULL) {
        //    NWAV_FREE(pStrmBufL);
        //    pStrmBufL = NULL;
        //}
        /*
        if (pStrmBufR != NULL) {
            NWAV_FREE(pStrmBufR);
            pStrmBufR = NULL;
        }*/
    }
}

//Stops the music. (For external use ONLY)
void NWAVPlayer_stop(int frames)
{
    lock();
    stop_internal_locked(frames, FALSE);
    unlock();
}

/*
//Sets the event handler function.
void NWAVPlayer_setEventHandler(EventHandler func) { sInfo.eventHandler = func; }

//Updates the events.
static void updateEvents(StreamInfo* sInfo)
{
    if (!hInfo.numEvents || !sInfo->eventHandler)
        return;

    for (int i = 0; i < hInfo.numEvents; i++)
    {
        EventInfo info = events[i];
        if (info.sample > sInfo->musicCursor &&
            info.sample < sInfo->musicCursor + sInfo->samplesPerUpdate)
        {
            sInfo->eventHandler(info.eventID);
        }
    }
}*/

//Updates the music fading.
BOOL NWAVPlayer_updateFade(void)
{
    if (sInfo.isPlaying && sInfo.fadeFrame)
    {
        int newVolume = sInfo.volume - sInfo.fadeDec;
        newVolume = MATH_CLAMP(newVolume, 0, 127);

        //Never step past the target. A partial fade (like 127 -> 64) used to dip below the target and then jump back up at the end.
        if (sInfo.fadeDec > 0 && newVolume < sInfo.targetVolume)
            newVolume = sInfo.targetVolume;
        else if (sInfo.fadeDec < 0 && newVolume > sInfo.targetVolume)
            newVolume = sInfo.targetVolume;

        NWAVPlayer_setVolume(newVolume, 0);
        sInfo.fadeFrame--;

        if (sInfo.fadeFrame == 0)
        {
            NWAVPlayer_setVolume(sInfo.targetVolume, 0);
            if (sInfo.stopMode == 1)
            {
                lock();
                stop_internal_locked(0, FALSE);
                unlock();
                return FALSE;
            }
        }
    }
    return TRUE;
}

//Checks if the music has reached end or loop point and updates the music state accordingly.
static void updateCheckEnd(StreamInfo* sInfo, int len, u32 offset)
{
    int leftOver = STRM_BUF_PAGESIZE - len;
    if (sInfo->loops)
    {
        if (sInfo->musicCursor >= hInfo.loopEnd)
        {
            seek(hInfo.loopStart, TRUE);
            if (leftOver > 0)
            {   /*
                FS_ReadFile(&file, pStrmBufL + offset + len, leftOver);
                if (sInfo->chCount > 1)
                    FS_ReadFile(&file, pStrmBufR + offset + len, leftOver);
                
                for (int i = 0; i < sInfo->chCount; i++)
                    FS_ReadFile(&file, &(*pBuf)[i][len], leftOver);
                */
                // Read Left
                FS_ReadFile(&file, pStrmBufL + offset + len, leftOver);
                //DC_InvalidateRange(pStrmBufL + offset + len, leftOver);
                //DC_FlushRange(pStrmBufL + offset + len, leftOver);

                // Read Right (if stereo)
                /*
                if (sInfo->chCount > 1) {
                    FS_ReadFile(&file, pStrmBufR + offset + len, leftOver);
                    DC_InvalidateRange(pStrmBufR + offset + len, leftOver);
                    DC_FlushRange(pStrmBufR + offset + len, leftOver);
                }
                */
            }
            seek(hInfo.loopStart + (leftOver / sInfo->bytesPerSample), TRUE);
        }
    }
    else
    {
        //(">=" on purpose: the cursor is clamped to musicEnd by update(), so ">" could never be true and the final pages would loop forever)
        if (sInfo->musicCursor >= sInfo->musicEnd)
        {
            //Instead of instantly stopping the music, we must wait for the buffer end, otherwise the music will stop sooner.
            if (leftOver > 0)
            {
                /*
                for (int i = 0; i < sInfo->chCount; i++)
                    MI_CpuFill8(&(*pBuf)[i][len], 0, leftOver); 
                */
                MI_CpuFill8(pStrmBufL + offset + len, 0, leftOver);
                //if (sInfo->chCount > 1)
                //    MI_CpuFill8(pStrmBufR + offset + len, 0, leftOver);
            }
            stop_internal_locked(0, TRUE); //Stop the music and wait for the buffer end.
        }
    }
}

//Updates the music.
static void update(StreamInfo* sInfo)
{
    //A wake-up message can arrive after the music was stopped. The file is closed by then, so do nothing.
    if (!sInfo->isPlaying)
        return;

    //Check for delayed stop. The page that holds the end of the music is still playing, so wait one more page, silenced.
    if (sInfo->stopMode == 2)
    {
        u32 stopOffset = sInfo->bufPage * STRM_BUF_PAGESIZE;
        sInfo->bufPage = !sInfo->bufPage;

        if (sInfo->stopWait == 0)
        {
            stop_internal_locked(0, FALSE);
            return;
        }

        sInfo->stopWait--;
        MI_CpuFill8(pStrmBufL + stopOffset, 0, STRM_BUF_PAGESIZE);
        DC_FlushRange(pStrmBufL + stopOffset, STRM_BUF_PAGESIZE);
        return;
    }

    // Calculate the exact byte offset for the current page
    u32 offset = sInfo->bufPage * STRM_BUF_PAGESIZE;
    sInfo->bufPage = !sInfo->bufPage;

    DC_InvalidateRange(pStrmBufL + offset, STRM_BUF_PAGESIZE);

    // Get read length.
    int len = STRM_BUF_PAGESIZE;
    int limit = sInfo->loops ? hInfo.loopEnd : sInfo->musicEnd;
    int remain = (limit - sInfo->musicCursor) * sInfo->bytesPerSample;

    if (remain < 0) remain = 0;

    if (remain < len)
        len = remain;

// Read the main block of data
    //if (len > 0) {
     //   FS_ReadFile(&file, pStrmBufL + offset, len);
    //}
    if (len > 0) {
        s32 bytesRead = FS_ReadFile(&file, pStrmBufL + offset, len);
        
        // EOF FAILSAFE: Force the loop wrap if the physical file ends early
        if (bytesRead < len && sInfo->loops) {
            sInfo->musicCursor = hInfo.loopEnd;
        }
    }
    // Read Left
    //FS_ReadFile(&file, pStrmBufL + offset, len);
    //DC_InvalidateRange(pStrmBufL + offset, len);
    //DC_FlushRange(pStrmBufL + offset, len);

    // Read Right
    /*
    if (sInfo->chCount > 1) {
        FS_ReadFile(&file, pStrmBufR + offset, len);
        DC_InvalidateRange(pStrmBufR + offset, len);
        DC_FlushRange(pStrmBufR + offset, len);
    }
    */

    // Increment the music cursor.
    //sInfo->musicCursor += sInfo->samplesPerUpdate;
    sInfo->musicCursor += (len / sInfo->bytesPerSample);

    // Pass the offset down so it knows where to append the leftover bytes
    updateCheckEnd(sInfo, len, offset);

    //DC_InvalidateRange(pStrmBufL + offset, STRM_BUF_PAGESIZE);
    DC_FlushRange(pStrmBufL + offset, STRM_BUF_PAGESIZE);

}

//The sound alarm function that unblocks the thread.
static void SoundAlarmHandler(void* arg)
{
    //Unblock updater thread.
    OS_SendMessage(&msgQ, (OSMessage)arg, OS_MESSAGE_NOBLOCK);
}

//Setups the music channels, timers and sound alarm.
static void setup(void)
{
    if (sInfo.playRate <= 0) sInfo.playRate = 32000; // Emergency default

    //Calculate timer values.
    s32 timerValue = SND_TIMER_CLOCK / sInfo.playRate;
    u32 alarmPeriod = timerValue * (STRM_BUF_PAGESIZE / sInfo.bytesPerSample) / 32;

    s32 loopLen = STRM_BUF_SIZE / sizeof(u32);

    //DC_FlushRange(pStrmBuf, STRM_BUF_SIZE * sInfo.chCount);
    DC_FlushRange(pStrmBufL, STRM_BUF_SIZE);
    /*
    if (sInfo.chCount > 1) {
        DC_FlushRange(pStrmBufR, STRM_BUF_SIZE);
    }
  

    //Setup channels.
    for (int i = 0; i < sInfo.chCount; i++)
    {
        BOOL left = i == 0;
        SND_SetupChannelPcm(
            left ? CHANNEL_L_NUM : CHANNEL_R_NUM,
            hInfo.format,
            //left ? (*pStrmBuf)[0] : (*pStrmBuf)[1],
            left ? pStrmBufL : pStrmBufR,
            SND_CHANNEL_LOOP_REPEAT,
            0,
            loopLen,
            sInfo.volume,
            SND_CHANNEL_DATASHIFT_NONE,
            timerValue,
            !hInfo.stereo ? 64 : (left ? 0 : 127)
        );
    }
    */
    // Setup Mono Channel
    SND_SetupChannelPcm(
        CHANNEL_L_NUM,
        hInfo.format,
        pStrmBufL,
        SND_CHANNEL_LOOP_REPEAT,
        0,
        loopLen,
        sInfo.volume,
        SND_CHANNEL_DATASHIFT_NONE,
        timerValue,
        64 // Hardcoded center pan
    );

    //Setup sound alarm for updater thread.
    SND_SetupAlarm(ALARM_NUM, alarmPeriod, alarmPeriod, SoundAlarmHandler, &sInfo);
    //debug_printf("Alarm %d Setup. Per: %d\n", ALARM_NUM, alarmPeriod);
}

//Reloads the current timers to apply new settings.
static void reloadTimers(void)
{
    BOOL notPaused = !sInfo.isPaused;
    if (notPaused)
        setPaused_locked(TRUE);
    setup();
    if (notPaused)
        setPaused_locked(FALSE);
}

//Gets the music speed.
fx32 NWAVPlayer_getSpeed(void) { return sInfo.speed; }

//Sets the music speed.
void NWAVPlayer_setSpeed(fx32 speed)
{
    lock();
    //Setting the speed it already has does nothing. (Reloading the timers re-primes the buffer, which skips ahead in the music.)
    if (speed != sInfo.speed)
    {
        sInfo.playRate = (hInfo.sampleRate * speed) >> FX32_SHIFT;
        sInfo.speed = speed;
        //Nothing to reload while no music is playing: the buffer does not exist yet and play() picks the new speed up.
        if (sInfo.isPlaying)
            reloadTimers();
    }
    unlock();
}

/*
//Loads the NWAV events that will be used to trigger the current callback function set.
static void loadEvents(void)
{
    //Allocate events.
    events = (EventInfo*)malloc(hInfo.numEvents * sizeof(EventInfo));

    //Read event IDs.
    for (int i = 0; i < hInfo.numEvents; i++)
    {
        int val = 0;
        FS_ReadFile(&file, &val, 1);
        events[i].eventID = val;
    }

    //Jump to "samples for event IDs" block.
    seek(HEADER_SIZE + sInfo.eventIDBlockSize, FALSE);

    //Read samples for event IDs
    for (int i = 0; i < hInfo.numEvents; i++)
    {
        int val;
        FS_ReadFile(&file, &val, 4);
        events[i].sample = val;
    }
}*/

//Plays the music. Returns FALSE (and plays nothing) if the file cannot be opened or is not a usable NWAV file.
BOOL NWAVPlayer_play(int fileID)
{
    if (strmThread == NULL)
    {
        u8* stack = (u8*)NWAV_ALLOC(THREAD_STACK_SIZE);
        OSThread* thread = (OSThread*)NWAV_ALLOC(sizeof(OSThread));
        if (stack == NULL || thread == NULL)
        {
            if (stack != NULL)
                NWAV_FREE(stack);
            if (thread != NULL)
                NWAV_FREE(thread);
            return FALSE;
        }
        strmThreadStack = stack;
        strmThread = thread;

        //Startup stream thread.
        OS_InitMessageQueue(&msgQ, &msgBuf, 1);
        OS_CreateThread(
            strmThread,
            StrmThread,
            NULL,
            &strmThreadStack[THREAD_STACK_SIZE],
            THREAD_STACK_SIZE,
            STREAM_THREAD_PRIO
        );
        OS_WakeUpThreadDirect(strmThread);
    }

    //The stream thread must not be reading the file while it is closed and opened again here.
    lock();

    //If music is already playing, stop it.
    if (sInfo.isPlaying)
        stop_internal_locked(0, FALSE);

    //Initialize file and try to open it.
    FS_InitFile(&file);

    void* romArchive = FS_FindArchive("rom", 3);
    if (romArchive == NULL || !FS_OpenFileFast(&file, romArchive, fileID))
    {
        NWAV_LOG("NWAV: could not open file %d.\n", fileID);
        unlock();
        return FALSE;
    }

    //Read the file header and make sure it really is something we can play.
    if (FS_ReadFile(&file, &hInfo, HEADER_SIZE) != (s32)HEADER_SIZE
        || hInfo.magic != NWAV
        || hInfo.sampleRate <= 0
        || (hInfo.format != SND_WAVE_FORMAT_PCM8 && hInfo.format != SND_WAVE_FORMAT_PCM16))
    {
        NWAV_LOG("NWAV: file %d is not a playable NWAV (PCM8/PCM16 only).\n", fileID);
        FS_CloseFile(&file);
        unlock();
        return FALSE;
    }

    //Allocate stream buffer (once, it is reused for every music).
    if (pStrmBufL == NULL)
    {
        //Allocate 32 extra bytes to allow for manual alignment to the 32 byte cache line.
        u8* rawMemL = (u8*)NWAV_ALLOC(STRM_BUF_SIZE + 32);
        if (rawMemL == NULL)
        {
            FS_CloseFile(&file);
            unlock();
            return FALSE;
        }
        pStrmBufL = (u8*)(((u32)rawMemL + 31) & ~31);
    }

    NNS_SndSetMasterVolume(127);

    //Reset variables
    sInfo.fadeDec = 0;
    sInfo.fadeFrame = 0;
    sInfo.stopMode = 0;
    sInfo.stopWait = 0;
    sInfo.volume = 127;
    sInfo.chCount = 1; //hInfo.stereo ? 2 : 1;
    sInfo.bytesPerSample = hInfo.format ? 2 : 1;
    sInfo.samplesPerUpdate = (STRM_BUF_PAGESIZE / sInfo.bytesPerSample);

#if NWAV_LOOP_POINTS_ARE_BYTES
    //The converter tool stores the loop points as bytes, divide them back into samples
    hInfo.loopStart /= sInfo.bytesPerSample;
    hInfo.loopEnd   /= sInfo.bytesPerSample;
#endif

    if (sInfo.bytesPerSample == 2) {
        hInfo.loopStart &= ~1;
        hInfo.loopEnd &= ~1;
    } else {
        hInfo.loopStart &= ~3;
        hInfo.loopEnd &= ~3;
    }

    sInfo.loops = hInfo.loopEnd != 0;
    NWAV_LOG("LoopEnd: %d.\n", hInfo.loopEnd);

    //The event data sits between the header and the audio. The events themselves are not used, but the audio starts after them.
    if (hInfo.numEvents)
    {
        int unalignedEvents = (hInfo.numEvents % 4);
        sInfo.eventIDBlockSize = hInfo.numEvents + (4 - unalignedEvents);
        sInfo.eventBlockSize = sInfo.eventIDBlockSize + (hInfo.numEvents * 4);
    }
    else
    {
        sInfo.eventIDBlockSize = 0;
        sInfo.eventBlockSize = 0;
    }

    //Calculate music size.
    sInfo.musicEnd = (((hInfo.fileSize - HEADER_SIZE - sInfo.eventBlockSize) / sInfo.chCount) / sInfo.bytesPerSample);

    seek(0, TRUE);

    //Apply the current speed. (The timers are stopped here, so this only needs the channel setup.)
    sInfo.playRate = (hInfo.sampleRate * sInfo.speed) >> FX32_SHIFT;
    setup();

    sInfo.isPlaying = TRUE;
    sInfo.isPaused = TRUE;
    setPaused_locked(FALSE);

    unlock();
    return TRUE;
}

//The OS thread that runs the updater.
static void StrmThread(void* arg)
{
    (void)arg;
    OSMessage message;

    //Main thread loop
    while (TRUE)
    {
        OS_ReceiveMessage(&msgQ, &message, OS_MESSAGE_BLOCK); //Block thread until message is received by the sound alarm.
        //debug_printf("T Wakeup\n");
        lock();                                 //The game thread may be opening, closing or seeking the file.
        if (sInfo.isPlaying && !sInfo.isPaused)
            update((StreamInfo*)message);       //Update the music.
        unlock();
    }
}

//Initializes the NWAV player.
void NWAVPlayer_init(void)
{
	sInfo.isPlaying = FALSE;
    sInfo.isPaused = TRUE;
    sInfo.speed = 0x1000;
    sInfo.volume = 127;
    sInfo.fadeFrame = 0;
    sInfo.stopWait = 0;

    //Create the lock that the game thread and the stream thread share. It starts out free.
    if (!lockReady)
    {
        OS_InitMessageQueue(&lockQ, &lockBuf, 1);
        OS_SendMessage(&lockQ, (OSMessage)1, OS_MESSAGE_NOBLOCK);
        lockReady = TRUE;
    }

    //Lock the channels.
    SND_LockChannel(1 << CHANNEL_L_NUM,0);// | 1 << CHANNEL_R_NUM, 0);

}
