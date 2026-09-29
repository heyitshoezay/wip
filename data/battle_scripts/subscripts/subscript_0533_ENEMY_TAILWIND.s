#include "constants/battle_constants.h"
.include "battle_commands.inc"

.data

_000:
    // Play the move we stored in waza_work (Tailwind)
    PlayMoveAnimation 255
    Wait
    
    // Use the exact vanilla text command now that we have an "attacker"
    PrintMessage 1230, TAG_NONE_SIDE, BATTLER_CATEGORY_ATTACKER
    Wait 
    WaitButtonABTime 30
    
    End