#include "constants/battle_constants.h"
.include "battle_commands.inc"

.data

_000:
    // {0} used {1}!
    PrintAttackMessage
    Wait
    WaitButtonABTime 30
    UpdateVar OPCODE_FLAG_ON, BSCRIPT_VAR_MOVE_STATUS_FLAGS, MOVE_STATUS_FAILED
    AbilityPopUp BATTLER_CATEGORY_MSG_BATTLER_TEMP
    // let the ability popup show and go away before the next message.  Raise the 30 if it still overlaps.
    WaitButtonABTime 30

    // {0} cannot use {1}!
    PrintMessage 1474, TAG_NICKNAME_MOVE, BATTLER_CATEGORY_ATTACKER, BATTLER_CATEGORY_ATTACKER
    Wait 
    WaitButtonABTime 30
    End
