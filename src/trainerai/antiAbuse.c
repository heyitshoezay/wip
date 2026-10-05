#include "../../include/battle.h"
#include "../../include/config.h"
#include "../../include/constants/move_effects.h"
#include "../../include/constants/moves.h"
#include "../../include/custom/custom_ai.h"
#include "../../include/debug.h"
#include "../../include/pokemon.h"
#include "../../include/types.h"

#ifdef IMPLEMENT_AI_ANTI_ABUSE

/**
 *  @brief decide whether a move is a "stalling" move: it protects the user or heals the user without pressuring the AI
 *
 *  @param ctx battle structure
 *  @param move move to check
 *  @return TRUE if the move counts as stalling
 */
static BOOL IsStallingMove(struct BattleStruct *ctx, u32 move)
{
    if (move == MOVE_NONE) {
        return FALSE;
    }
    switch (ctx->moveTbl[move].effect) {
    case MOVE_EFFECT_PROTECT:
    case MOVE_EFFECT_PROTECT_USER_SIDE:
    case MOVE_EFFECT_SURVIVE_WITH_1_HP: // endure
    case MOVE_EFFECT_RESTORE_HALF_HP:
    case MOVE_EFFECT_HEAL_HALF_DIFFERENT_IN_WEATHER:
    case MOVE_EFFECT_HEAL_HALF_REMOVE_FLYING_TYPE:
    case MOVE_EFFECT_RECOVER_HEALTH_AND_SLEEP:
    case MOVE_EFFECT_SWALLOW:
        return TRUE;
    default:
        return FALSE;
    }
}

/**
 *  @brief record what one player battler did on the last turn
 *         pure bookkeeping on the tracker, so it can be tested without a running battle
 *
 *  @param tracker anti-abuse tracker
 *  @param side 0 for the player's first battler, 1 for the second
 *  @param partySlot party slot the battler is in now
 *  @param previousMonAlive whether the Pokémon that was in the battler last turn is still alive
 *  @param aiForcedSwitch the AI used a move that drags Pokémon out (Roar, Whirlwind, Dragon Tail...)
 *  @param playerPivoted the player's last move was a switching move (U-turn, Volt Switch...)
 *  @param playerStalled the player's last move was a stalling move
 */
void LONG_CALL AI_AntiAbuseObserve(AI_antiAbuse *tracker, u32 side, u32 partySlot, BOOL previousMonAlive, BOOL aiForcedSwitch, BOOL playerPivoted, BOOL playerStalled)
{
    if (!tracker->started[side]) {
        tracker->started[side] = TRUE;
        tracker->lastPartySlot[side] = (u8)partySlot;
        return;
    }

    if (partySlot != tracker->lastPartySlot[side]) {
        if (previousMonAlive && !aiForcedSwitch && !playerPivoted) {
            // the player chose to swap a healthy Pokémon out
            if (tracker->switchStreak[side] < 255) {
                tracker->switchStreak[side]++;
            }
            if (tracker->stallStreak[side] < 255) {
                tracker->stallStreak[side]++;
            }
            if (tracker->totalSwitches[side] < 255) {
                tracker->totalSwitches[side]++;
            }
        } else {
            // knocked out, dragged out or pivoted: not abuse, the situation just changed
            tracker->switchStreak[side] = 0;
            tracker->stallStreak[side] = 0;
        }
        tracker->lastPartySlot[side] = (u8)partySlot;
    } else {
        tracker->switchStreak[side] = 0;
        if (playerStalled) {
            if (tracker->stallStreak[side] < 255) {
                tracker->stallStreak[side]++;
            }
        } else {
            tracker->stallStreak[side] = 0;
        }
    }
}

/**
 *  @brief update the anti-abuse tracker from the live battle; runs once per turn
 *
 *  @param bsys battle system
 */
void LONG_CALL AI_UpdateAntiAbuse(struct BattleSystem *bsys)
{
    struct BattleStruct *ctx = bsys->sp;
    AI_antiAbuse *tracker = &ctx->aiAntiAbuse;
    int stamp = ctx->total_turn + 1;
    u32 sides = 1;

    if (tracker->turnStamp == stamp) {
        return; // already updated this turn
    }
    tracker->turnStamp = stamp;

    if (BattleTypeGet(bsys) & (BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG)) {
        sides = 2;
    }

    for (u32 side = 0; side < sides; side++) {
        u32 client = side * 2; // the player's battlers are clients 0 and 2
        u8 slot = ctx->sel_mons_no[client];
        BOOL previousAlive = FALSE;
        if (tracker->started[side]) {
            struct PartyPokemon *previous = BattleWorkPokemonParamGet(bsys, (int)client, tracker->lastPartySlot[side]);
            previousAlive = (previous != NULL && GetMonData(previous, MON_DATA_HP, NULL) != 0);
        }
        u32 playerLastMove = ctx->waza_no_old[client];
        u32 aiLastMove = ctx->waza_no_old[BATTLER_OPPONENT(client)];
        AI_AntiAbuseObserve(tracker, side, slot, previousAlive,
            IsMoveForceSwitching(aiLastMove),
            IsMoveValidSwitchingMove(playerLastMove),
            IsStallingMove(ctx, playerLastMove));
#ifdef DEBUG_AI_SCORING
        debug_printf("anti-abuse side %d: slot %d, switch streak %d, stall streak %d, total switches %d\n", side, slot,
            tracker->switchStreak[side], tracker->stallStreak[side], tracker->totalSwitches[side]);
#endif // DEBUG_AI_SCORING
    }
}

/**
 *  @brief how far past a threshold the player is
 *
 *  @param switchStreak consecutive voluntary switches
 *  @param stallStreak consecutive stalling turns
 *  @param switchingIsTheReason set to TRUE when the switching streak is the bigger reason
 *  @return 0 if the player is under both thresholds; otherwise 1 for the first turn past a threshold, 2 for the next, and so on
 */
int LONG_CALL AI_AntiAbuseBonusLevel(u32 switchStreak, u32 stallStreak, BOOL *switchingIsTheReason)
{
    int switchLevel = 0;
    int stallLevel = 0;
    if (switchStreak >= ANTI_ABUSE_SWITCH_THRESHOLD) {
        switchLevel = (int)(switchStreak - ANTI_ABUSE_SWITCH_THRESHOLD) + 1;
    }
    if (stallStreak >= ANTI_ABUSE_STALL_THRESHOLD) {
        stallLevel = (int)(stallStreak - ANTI_ABUSE_STALL_THRESHOLD) + 1;
    }
    if (switchingIsTheReason != NULL) {
        *switchingIsTheReason = (switchLevel >= stallLevel && switchLevel > 0);
    }
    return switchLevel > stallLevel ? switchLevel : stallLevel;
}

static int LimitBonus(int level)
{
    int bonus = level * ANTI_ABUSE_BONUS_PER_LEVEL;
    return bonus > ANTI_ABUSE_MAX_BONUS ? ANTI_ABUSE_MAX_BONUS : bonus;
}

/**
 *  @brief score added to a setup move that is already worth using, when the player is switching or stalling
 *
 *  @param ai AI context
 *  @return bonus to add to the setup move's score
 */
int LONG_CALL AI_AntiAbuseSetupBonus(struct AIContext *ai)
{
    return LimitBonus(AI_AntiAbuseBonusLevel(ai->playerSwitchStreak, ai->playerStallStreak, NULL));
}

/**
 *  @brief score added to a harassing move that is already worth using, when the player is switching or stalling
 *         hazards punish switching; Taunt, Encore, poison and Leech Seed punish stalling
 *
 *  @param ai AI context
 *  @param moveEffect effect of the move being scored
 *  @return bonus to add to the move's score
 */
int LONG_CALL AI_AntiAbuseHarassBonus(struct AIContext *ai, u32 moveEffect)
{
    BOOL switching = FALSE;
    int level = AI_AntiAbuseBonusLevel(ai->playerSwitchStreak, ai->playerStallStreak, &switching);
    if (level == 0) {
        return 0;
    }
    switch (moveEffect) {
    case MOVE_EFFECT_SET_SPIKES:
    case MOVE_EFFECT_STEALTH_ROCK:
    case MOVE_EFFECT_TOXIC_SPIKES:
    case MOVE_EFFECT_STICKY_WEB:
        return switching ? LimitBonus(level) : 0;
    case MOVE_EFFECT_TAUNT:
    case MOVE_EFFECT_ENCORE:
    case MOVE_EFFECT_STATUS_POISON:
    case MOVE_EFFECT_STATUS_BADLY_POISON:
    case MOVE_EFFECT_STATUS_LEECH_SEED:
        return switching ? 0 : LimitBonus(level);
    default:
        return 0;
    }
}

#endif // IMPLEMENT_AI_ANTI_ABUSE
