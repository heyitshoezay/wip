#include "constants/battle_constants.h"
.include "battle_commands.inc"

.data

_000:
.if SNOW_WARNING_GENERATION >= 9
    PlayBattleAnimation BATTLER_CATEGORY_PLAYER, BATTLE_ANIMATION_WEATHER_SNOW
    Wait 
    // It started to snow!
    PrintMessage 1439, TAG_NONE
    Wait 
    WaitButtonABTime 30
    UpdateVar OPCODE_FLAG_ON, BSCRIPT_VAR_FIELD_CONDITION, FIELD_CONDITION_SNOW_PERMANENT
    End 
.else
    PlayBattleAnimation BATTLER_CATEGORY_PLAYER, BATTLE_ANIMATION_WEATHER_HAIL
    Wait 
    // It’s hailing!
    PrintMessage 810, TAG_NONE
    Wait 
    WaitButtonABTime 30
    UpdateVar OPCODE_FLAG_ON, BSCRIPT_VAR_FIELD_CONDITION, FIELD_CONDITION_HAIL_PERMANENT
    End
.endif