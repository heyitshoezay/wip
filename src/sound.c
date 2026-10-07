#include "config.h"
#include "debug.h"
#include "types.h"

#include "sound.h"
#include "NWAVPlayer.h"

// 1 = print every music request and every stop / pause call to the debugger (desmume / no$gba console).
// Off by default: each message string costs ROM space in a very small region. Turn it on only while chasing a bug.
#define NWAV_SND_DEBUG 0
#if NWAV_SND_DEBUG
#define SND_LOG(...) debug_printf(__VA_ARGS__)
#else
#define SND_LOG(...) ((void)0)
#endif

int firstWavID; //put nwav into base/root/waves folder, build hg-e, check in tinke, for now is 533
static u16 current_seq = 0xFFFF;
static BOOL current_is_nwav = FALSE;

typedef struct {
    u32 vanilla_seq;
    u32 nwav_id;
} NWAV_Override;

//Use this array to override specific sequences that cannot be reassigned via music_tables.c or DSPRE's header editor
static const NWAV_Override sNwavOverrides[] = {
    //{example_sseq, example_nwav}
    //{1008, 2},  // Title screen -> iris network
    //{1004, 31}, // Opening  -> feelings risen
    
};


void LONG_CALL NNS_SndInit_Hook(void){
    firstWavID = 536;
    NNS_SndInit_Original();
    NWAVPlayer_init();
}

// Jingles / fanfares (level up, item get, ...) play on sound player 2. The game pauses its own music for them, but only if its
// own BGM player is running - and it is not while a streamed song plays - so mirror it: pause the stream while player 2 is busy.
#define FANFARE_PLAYER_NO 2
int LONG_CALL GF_SndPlayerCountPlaying(int playerNo);
static BOOL sFanfareWasPlaying = FALSE;

void LONG_CALL NNS_SndMain_Hook(void){
    NNS_SndMain_Original();
    NWAVPlayer_updateFade();

    BOOL fanfarePlaying = GF_SndPlayerCountPlaying(FANFARE_PLAYER_NO) != 0;
    if (fanfarePlaying != sFanfareWasPlaying) {
        sFanfareWasPlaying = fanfarePlaying;
        if (current_is_nwav) {
            NWAVPlayer_setPaused(fanfarePlaying);
        }
    }
}

/*
void LONG_CALL NNS_SndPlayerSetTempoRatio_Hook(int handle, int tempo){
    NNS_SndPlayerSetTempoRatio_Original(handle, tempo);
    //debug_printf("[NNS_SndPlayerSetTempoRatio_Hook] Setting tempo ratio to %d.\n", tempo);
    //this still needs to be tested.
    //NWAVPlayer_setSpeed(tempo << 12 >> 8);
}
*/


void LONG_CALL GF_SndHandleMoveVolume_Hook(int param1, int volume, int frames)
{
    GF_SndHandleMoveVolume_Original(param1, volume, frames);
    //debug_printf("[GF_SndHandleMoveVolume_Hook] Handling move volume with params: %d, %d, %d.\n", param1, volume, frames);
    //param 1 could be the player ID? only update volume for bgm, not cries or sfx
    
    //if (param1 == 0)
    //{
    //    NWAVPlayer_setVolume(volume, frames);
    //    //debug_printf("Player is BGM (GF wrapper).\n");

    //}
}

void LONG_CALL NNS_SndPlayerPauseByPlayerNo_Hook(u8 playerID, BOOL paused)
{
    NNS_SndPlayerPauseByPlayerNo_Original(playerID, paused);
    SND_LOG("pause player %d -> %d\n", playerID, paused);
    //debug_printf("Setting pause for player %d to %d.\n", playerID, paused);
    
    if(playerID == 0 || playerID == 1 || playerID == 7){
        NWAVPlayer_setPaused(paused);
    }
    
}

void LONG_CALL NNS_SndPlayerStopSeqByPlayerNo_Hook(u8 playerID, int fadeFrame)
{
    NNS_SndPlayerStopSeqByPlayerNo_Original(playerID, fadeFrame);
    SND_LOG("stop player %d, fade %d (stream playing %d)\n", playerID, fadeFrame, NWAVPlayer_isPlaying());
    //debug_printf("Stop seq for p %d with fframe %d.\n", playerID, fadeFrame);
    if(playerID == 9 && fadeFrame > 0){
        NWAVPlayer_stop(fadeFrame);
        current_seq = 0xFFFF;
        current_is_nwav = FALSE;
    }
}


// Every background music of the game (SEQ_GS_*) is numbered 1004 or higher, and the sound effects are 1372 and higher, so streamed songs
// live in the numbers BELOW 1000 (index 0, 1, 2 ... = the first, second ... file of the waves folder). A request for 1000 or higher is
// always a normal song: answering that here keeps the whole overworld and everything else untouched by this feature, and saves
// opening a ROM file for every music change.
#define NWAV_FIRST_NON_STREAM_SEQ 1000

static BOOL GetIfSequenced(int seqID)
{
    if (seqID >= NWAV_FIRST_NON_STREAM_SEQ)
        return TRUE;

    int wavID = firstWavID + seqID; //firstWavID is the index in NWAVPlayer.h
    FSFile file;
    FS_InitFile(&file);

    void* romArchive = FS_FindArchive("rom", 3);

    if (FS_OpenFileFast(&file, romArchive, wavID))
    {
        int magic;
        int readSize = FS_ReadFile(&file, &magic, 4);
        if(readSize == 4 && magic == NWAV)
        {
            FS_CloseFile(&file);
            return FALSE;

        }
        FS_CloseFile(&file);
    }
    return TRUE;
}


//Stops the vanilla BGM players and starts a streamed music. Returns FALSE if the stream could not start.
static BOOL StartStreamed(int wavID)
{
    NNS_SndPlayerStopSeqByPlayerNo_Original(0, 30); // Kills vanilla BGM
    NNS_SndPlayerStopSeqByPlayerNo_Original(1, 30); // Kills Eye Music
    NNS_SndPlayerStopSeqByPlayerNo_Original(9, 30);

    // NWAVPlayer_play already starts at the right volume and speed. Calling NWAVPlayer_setSpeed / setVolume
    // again right after it used to restart the timers and skip the first moments of every music.
    return NWAVPlayer_play(wavID);
}

//replace the play function
void LONG_CALL PlayBGM_Hook(u16 seqno)
{
    SND_LOG("PlayBGM %d (last request %d, streaming %d, stream really playing %d)\n", seqno, current_seq, current_is_nwav, NWAVPlayer_isPlaying());

    // Only ignore a request for a stream that is REALLY still playing (one that ran out, or was stopped, must be allowed to start again).
    // Vanilla music is not ignored here: the game's own PlayBGM already does its "already playing" check, with the real state.
    // This hook only knows about the requests that pass through it. When the game stops or replaces the music some other way,
    // current_seq goes stale, and ignoring the next request for that number leaves the game silent.
    if (current_is_nwav && current_seq == seqno && NWAVPlayer_isPlaying()) {
        SND_LOG("  ignored: that stream is already playing\n");
        return;
    }

    if (seqno == 0xFFFF) {
        if (current_is_nwav) {
            NWAVPlayer_stop(30);
            current_is_nwav = FALSE;
        } else {
            PlayBGM_Original(0xFFFF);
        }
        current_seq = 0xFFFF;
        return;
    }

    BOOL next_is_seq = GetIfSequenced(seqno);
    int wavID = firstWavID + seqno;

    int num_overrides = sizeof(sNwavOverrides) / sizeof(sNwavOverrides[0]);
    for (int i = 0; i < num_overrides; i++) {
        if(seqno == sNwavOverrides[i].vanilla_seq) {
            next_is_seq = FALSE;
            wavID = firstWavID + sNwavOverrides[i].nwav_id;
            break;
        }
    }

    // Was a stream REALLY playing? (If the game ended it by itself, current_is_nwav is stale and the vanilla players may be running.)
    BOOL streamReallyPlaying = current_is_nwav && NWAVPlayer_isPlaying();

    if (current_is_nwav) {
        NWAVPlayer_stop(30);
    }

    if (next_is_seq) {
        if (current_is_nwav) {
            NNS_SndPlayerStopSeqByPlayerNo_Original(0, 30); // Kills vanilla BGM
            NNS_SndPlayerStopSeqByPlayerNo_Original(1, 30); // Kills Eye Music
            NNS_SndPlayerStopSeqByPlayerNo_Original(9, 30);
        }
        PlayBGM_Original(seqno);
        current_is_nwav = FALSE;
    } else if (streamReallyPlaying) {
        // the vanilla players were silenced when this stream started, so only the stream changes
        current_is_nwav = NWAVPlayer_play(wavID);
        if (!current_is_nwav) {
            PlayBGM_Original(seqno); // the file was not usable, fall back to the vanilla music
        }
    } else {
        // no stream is playing: make sure the vanilla music is silenced first, then start the stream
        current_is_nwav = StartStreamed(wavID);
        if (!current_is_nwav) {
            PlayBGM_Original(seqno); // the file was not usable, fall back to the vanilla music
        }
    }
    current_seq = seqno;
}

BOOL LONG_CALL GF_Snd_LoadSeq(int seqNo)
{
    BOOL ret;
    struct SND_WORK *work;

    work = GetSoundDataPointer();
    ret = NNS_SndArcLoadSeq(seqNo, work->heap);
    GF_SndHeapGetFreeSize();

#ifdef DEBUG_SOUND_SSEQ_LOADS
    if (!ret) {
        u8 buf[200];
        sprintf(buf, "[GF_Snd_LoadSeq] Failed to load song %d.  There are 0x%x bytes left in the sound heap.\n", seqNo, SoundHeapFreeSize);
        debugsyscall(buf);
    } else {
        u8 buf[200];
        sprintf(buf, "[GF_Snd_LoadSeq] Loaded song %d.  There are 0x%x bytes left in the sound heap.\n", seqNo, SoundHeapFreeSize);
        debugsyscall(buf);
    }
#endif // DEBUG_SOUND_SSEQ_LOADS

    return ret;
}

BOOL GF_Snd_LoadSeqEx(int seqNo, u32 loadFlag)
{
    BOOL ret;
    struct SND_WORK *work;

    work = GetSoundDataPointer();
    ret = NNS_SndArcLoadSeqEx(seqNo, loadFlag, work->heap);
    GF_SndHeapGetFreeSize();

#ifdef DEBUG_SOUND_SSEQ_LOADS
    if (!ret) {
        u8 buf[200];
        sprintf(buf, "[GF_Snd_LoadSeqEx] Failed to load song %d.  There are 0x%x bytes left in the sound heap.\n", seqNo, SoundHeapFreeSize);
        debugsyscall(buf);
    } else {
        u8 buf[200];
        sprintf(buf, "[GF_Snd_LoadSeqEx] Loaded song %d.  There are 0x%x bytes left in the sound heap (EX).\n", seqNo, SoundHeapFreeSize);
        debugsyscall(buf);
    }
#endif // DEBUG_SOUND_SSEQ_LOADS

    return ret;
}

#ifdef DEBUG_SOUND_SBNK_LOADS

const u8 *NNS_SND_ARC_LOAD_ERROR_STRINGS[] = {
    "NNS_SND_ARC_LOAD_SUCCESS",
    "NNS_SND_ARC_LOAD_ERROR_INVALID_GROUP_NO",
    "NNS_SND_ARC_LOAD_ERROR_INVALID_SEQ_NO",
    "NNS_SND_ARC_LOAD_ERROR_INVALID_SEQARC_NO",
    "NNS_SND_ARC_LOAD_ERROR_INVALID_BANK_NO",
    "NNS_SND_ARC_LOAD_ERROR_INVALID_WAVEARC_NO",
    "NNS_SND_ARC_LOAD_ERROR_FAILED_LOAD_SEQ",
    "NNS_SND_ARC_LOAD_ERROR_FAILED_LOAD_SEQARC",
    "NNS_SND_ARC_LOAD_ERROR_FAILED_LOAD_BANK",
    "NNS_SND_ARC_LOAD_ERROR_FAILED_LOAD_WAVE"
};

#endif // DEBUG_SOUND_SBNK_LOADS

int LONG_CALL NNSi_SndArcLoadBank(int bankNo, u32 loadFlag, void *heap, BOOL bSetAddr, struct SNDBankData **pData)
{
    const NNSSndArcBankInfo *bankInfo;
    const NNSSndArcWaveArcInfo *waveArcInfo;
    SNDBankData *bank = NULL;
    SNDWaveArc *waveArc = NULL;
    int result;
    int i;
    BOOL loadingNewCry = 0, hasLoadedCry = 0;

    // Get bank information
    if (bankNo >= CRY_PSEUDOBANK_START || (bankNo < 495 && bankNo > 1)) // assume all cry banks are loading cries
    {
        bankInfo = NNS_SndArcGetBankInfo(1);
        loadingNewCry = 1;
#ifdef DEBUG_SOUND_SBNK_LOADS
        u8 buf[200];
        sprintf(buf, "[NNSi_SndArcLoadBank] Cry load detected for bank %d (Index %d).\n", bankNo, (bankNo >= CRY_PSEUDOBANK_START) ? (bankNo - (CRY_PSEUDOBANK_START - 544)) : bankNo);
        debugsyscall(buf);
#endif // DEBUG_SOUND_SBNK_LOADS
    } else {
        bankInfo = NNS_SndArcGetBankInfo(bankNo);
    }

#ifdef DEBUG_SOUND_SBNK_LOADS
    if (bankInfo == NULL) {
        u8 buf[200];
        GF_SndHeapGetFreeSize();
        sprintf(buf, "[NNSi_SndArcLoadBank] Failed to load bank %d.  There are 0x%x bytes left in the sound heap.\n", bankNo, SoundHeapFreeSize);
        debugsyscall(buf);
    }
#endif // DEBUG_SOUND_SBNK_LOADS

    if (bankInfo == NULL) {
        return NNS_SND_ARC_LOAD_ERROR_INVALID_BANK_NO;
    }

    // If necessary to load
    if (loadFlag & NNS_SND_ARC_LOAD_BANK) {
        bank = LoadBank(bankInfo->fileId, heap, bSetAddr);
        if (bank == NULL) {
            return NNS_SND_ARC_LOAD_ERROR_FAILED_LOAD_BANK;
        }
    } else {
        bank = (SNDBankData *)NNS_SndArcGetFileAddress(bankInfo->fileId);
    }

    // Load waveform data
    for (i = 0; i < NNS_SND_ARC_BANK_TO_WAVEARC_NUM; i++) {
        u32 waveArcIndex = bankInfo->waveArcNo[i];
        if (loadingNewCry && !hasLoadedCry) {
            waveArcIndex = bankNo;
            hasLoadedCry = 1;
        }

        if (waveArcIndex == NNS_SND_ARC_INVALID_WAVEARC_NO) {
            continue;
        }

        // Get waveform archive information
        waveArcInfo = NNS_SndArcGetWaveArcInfo(waveArcIndex);

        if (waveArcInfo == NULL) {
#ifdef DEBUG_SOUND_SBNK_LOADS
            u8 buf[200];
            GF_SndHeapGetFreeSize();
            sprintf(buf, "[NNSi_SndArcLoadBank] Failed to load waveArc %d using NNS_SndArcGetWaveArcInfo.  There are 0x%x bytes left in the sound heap.\n", waveArcIndex, SoundHeapFreeSize);
            debugsyscall(buf);
#endif // DEBUG_SOUND_SBNK_LOADS

            return NNS_SND_ARC_LOAD_ERROR_INVALID_WAVEARC_NO;
        }

        // Loading waveform archives
        result = NNSi_SndArcLoadWaveArc(waveArcIndex, loadFlag, heap, bSetAddr, &waveArc);

#ifdef DEBUG_SOUND_SBNK_LOADS

        if (result != NNS_SND_ARC_LOAD_SUCCESS) {
            u8 buf[200];
            GF_SndHeapGetFreeSize();
            if (loadingNewCry) {
                sprintf(buf, "[NNSi_SndArcLoadBank] Failure to load waveArc %d using NNSi_SndArcLoadWaveArc (%s) ignored because cry detected and debugging is on.  There are 0x%x bytes left in the sound heap.\n", waveArcIndex, NNS_SND_ARC_LOAD_ERROR_STRINGS[result], SoundHeapFreeSize);
                debugsyscall(buf);
            } else {
                sprintf(buf, "[NNSi_SndArcLoadBank] Failed to load waveArc %d using NNSi_SndArcLoadWaveArc (%s).  There are 0x%x bytes left in the sound heap.\n", waveArcIndex, NNS_SND_ARC_LOAD_ERROR_STRINGS[result], SoundHeapFreeSize);
                debugsyscall(buf);
                return result;
            }
        }

#else

        if (result != NNS_SND_ARC_LOAD_SUCCESS) {
            return result;
        }

#endif // DEBUG_SOUND_SBNK_LOADS

        if (waveArcInfo->flags & NNS_SND_ARC_WAVEARC_SINGLE_LOAD) {
            // Individual waveform loading
            if (loadFlag & NNS_SND_ARC_LOAD_WAVE) {
                if (!LoadSingleWaves(waveArc, bank, i, waveArcInfo->fileId, heap)) {
#ifdef DEBUG_SOUND_SBNK_LOADS
                    {
                        u8 buf[200];
                        GF_SndHeapGetFreeSize();
                        sprintf(buf, "[NNSi_SndArcLoadBank] Failed to load waves for waveArc id %d using LoadSingleWaves.  There are 0x%x bytes left in the sound heap.\n", waveArcIndex, SoundHeapFreeSize);
                        debugsyscall(buf);
                    }
#endif // DEBUG_SOUND_SBNK_LOADS

                    return NNS_SND_ARC_LOAD_ERROR_FAILED_LOAD_WAVE;
                }
            }
        }

        // Associate waveforms with banks
        if (bank != NULL && waveArc != NULL) {
            SND_AssignWaveArc(bank, i, waveArc);

#ifdef DEBUG_SOUND_SBNK_LOADS
            {
                u8 buf[200];
                GF_SndHeapGetFreeSize();
                sprintf(buf, "[NNSi_SndArcLoadBank] Loaded waveArc id %d fully and assigned it to in-progress loaded bank %d.  There are 0x%x bytes left in the sound heap.\n", waveArcIndex, bankNo, SoundHeapFreeSize);
                debugsyscall(buf);
            }
#endif // DEBUG_SOUND_SBNK_LOADS
        }
    }

    if (pData != NULL) {
        *pData = bank;
    }

#ifdef DEBUG_SOUND_SBNK_LOADS
    {
        u8 buf[200];
        GF_SndHeapGetFreeSize();
        sprintf(buf, "[NNSi_SndArcLoadBank] Loaded bank %d.  There are 0x%x bytes left in the sound heap.\n", bankNo, SoundHeapFreeSize);
        debugsyscall(buf);
    }
#endif // DEBUG_SOUND_SBNK_LOADS

    return NNS_SND_ARC_LOAD_SUCCESS;
}
