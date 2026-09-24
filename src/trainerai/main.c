#include "../../include/types.h"
#include "../../include/config.h"
#include "../../include/battle.h"
#include "../../include/mega.h"
#include "../../include/pokemon.h"
#include "../../include/item.h"
#include "../../include/trainer_ai.h"
#include "../../include/constants/ability.h"
#include "../../include/constants/species.h"
#include "../../include/constants/battle_script_constants.h"
#include "../../include/constants/battle_message_constants.h"
#include "../../include/constants/move_effects.h"
#include "../../include/constants/item.h"
#include "../../include/custom/custom_ai.h"
#include "../../include/overlay.h"
#include "../../include/constants/file.h"

#define EXPLOSION_ON_LAST_MON 0
#define SKILL_SWAP_SCORING 1
// #define ATTRACT_WORK_ON_ALL_SEXES 1

#define IMMUNE_TO_MOVE    40
#define IMPOSSIBLE_MOVE   80
#define NEVER_USE_MOVE_20 20


int LONG_CALL BasicScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai);
int LONG_CALL DamagingMoveScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai);
int LONG_CALL SetupScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai);
int LONG_CALL RecoveryScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai);
int LONG_CALL HarassmentScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai);

int LONG_CALL OffensiveSetup(struct BattleSystem *bsys UNUSED, u32 attacker UNUSED, int i UNUSED, struct AIContext *ai);
int LONG_CALL DefensiveSetup(struct BattleSystem *bsys UNUSED, u32 attacker UNUSED, int i UNUSED, struct AIContext *ai);

BOOL LONG_CALL HasMovePriority(struct BattleSystem *bsys, u8 attacker, u32 attackerMove, u32 attackerAbility, u8 defender);


enum AIActionChoice __attribute__((section(".init"))) TrainerAI_Main(struct BattleSystem *bsys, u32 attacker)
{
#ifdef DEBUG_AI_SCORING
    debug_printf("TrainerAI_Main: %d\n", attacker);
#endif // DEBUG_AI_SCORING

    int score = 0;
    struct BattleStruct *ctx = bsys->sp;
    int highestDamageHitPrct = 0;
    if (attacker >= 10) {
        return BattleAI_PostKOSwitchIn_Internal(bsys, attacker - 10, &score, FALSE, &highestDamageHitPrct);
    }

    if (BattleTypeGet(bsys) == BATTLE_TYPE_ROAMER && !CantEscape(bsys, ctx, attacker, NULL)) {
        return AI_ENEMY_ESCAPE;
    }
    if (BattleTypeGet(bsys) == BATTLE_TYPE_SAFARI) {
        return AI_ENEMY_SAFARI;
    }

    ctx->aiWorkTable.ai_dir_select_client[attacker] = ctx->aiTurnScoring.targets[attacker];

    return ctx->aiTurnScoring.choice[attacker];
}


int LONG_CALL ScoreMovesAgainstDefender(struct BattleSystem *bsys, u32 attacker, u32 target, int moveScores[4][4], struct AIContext *ai)
{
    struct BattleStruct *ctx = bsys->sp;
    int highestScoredMove = 0;
    for (int i = 0; i < GetBattlerLearnedMoveCount(bsys, ctx, attacker); i++)
    {
        u32 attackerMove = ctx->battlemon[attacker].move[i];
        if (attackerMove != MOVE_NONE)
        {
            int itemEffect = HeldItemHoldEffectGet(ctx, attacker);
            if (attackerMove == ctx->battlemon[attacker].moveeffect.encoredMove
                || (attackerMove == ctx->battlemon[attacker].moveeffect.moveNoChoice
                    && (itemEffect == HOLD_EFFECT_CHOICE_ATK || itemEffect == HOLD_EFFECT_CHOICE_SPEED || itemEffect == HOLD_EFFECT_CHOICE_SPATK))) // if the attacker has a move that is forced, use it
            {
#ifdef DEBUG_AI_SCORING
                debug_printf("Attacker has choiced move %d:%d\n", i, ctx->battlemon[attacker].moveeffect.moveNoChoice);
#endif // DEBUG_AI_SCORING
                moveScores[target][i] += 1000;
            }

             moveScores[target][i] += 1000; // don't want to get negative numbers, so start high at 1000, MOVE_NONE will stay at 0
             moveScores[target][i] += BasicScoring(bsys, attacker, i, ai);
             moveScores[target][i] += DamagingMoveScoring(bsys, attacker, i, ai);
             moveScores[target][i] += SetupScoring(bsys, attacker, i, ai);
             moveScores[target][i] += RecoveryScoring(bsys, attacker, i, ai);
             moveScores[target][i] += HarassmentScoring(bsys, attacker, i, ai);

            if (highestScoredMove < moveScores[target][i]) {
                highestScoredMove = moveScores[target][i];
            }
        }
    }

    return highestScoredMove;
}

int LONG_CALL ScoreMovesAgainstAlly(struct BattleSystem *bsys, u32 attacker, u32 target, int moveScores[4][4], struct AIContext *ai)
{
    struct BattleStruct *ctx = bsys->sp;
    if (!ai->isDoubleBattle || !ai->isAllyAlive) {
        return 0;
    }

    int highestScoredMove = 0;
    for (int i = 0; i < GetBattlerLearnedMoveCount(bsys, ctx, attacker); i++) {
        int moveScore = 0;
        u32 attackerMove = ctx->battlemon[attacker].move[i];
        if (attackerMove != MOVE_NONE) {
            switch (attackerMove) {
            case MOVE_HELPING_HAND:
            {
                if (ai->ignoreTarget || ai->partnerMoveNo == MOVE_HELPING_HAND || !ai->partnerClicksAttackingMove) {
                    moveScore -= NEVER_USE_MOVE_20;
                }
                else
                {
                    moveScore += 6;
                    if (BattleRand(bsys) % 2 == 0) {
                        moveScore += 2;
                    }
                }
                break;
            }
            case MOVE_MILK_DRINK:
            case MOVE_HEAL_PULSE:
            case MOVE_POLLEN_PUFF:
            {
                if (!IsBattleMonSlowerThanOpposition(bsys, attacker, ai->isDoubleBattle) && ai->aimonAlly.percenthp <= 50) {
                    moveScore += 7;
                }
                break;
            }
            case MOVE_SKILL_SWAP: {
                if ((ai->attackerMon.ability == ABILITY_FLASH_FIRE && ai->aimonAlly.ability != ABILITY_FLASH_FIRE && ai->aimonAlly.species == SPECIES_DURANT)
                    || (ai->attackerMon.ability == ABILITY_TECHNICIAN && ai->aimonAlly.ability != ABILITY_TECHNICIAN && ai->aimonAlly.species == SPECIES_DURANT)
                    || (ai->attackerMon.ability == ABILITY_PRANKSTER && ai->aimonAlly.ability != ABILITY_PRANKSTER && ai->aimonAlly.species == SPECIES_BRELOOM)
                    || (ai->aimonAlly.ability == ABILITY_TRUANT && ai->aimonAlly.species == SPECIES_SLAKING)
                    || (ai->aimonAlly.ability == ABILITY_SLOW_START && ai->aimonAlly.species == SPECIES_REGIGIGAS)) {
                    moveScore += 6;
                    if (BattleRand(bsys) % 2 == 0) {
                        moveScore += 3;
                    }
                }
                break;
            }
            case MOVE_DECORATE: {
                moveScore += 6;
                if (BattleRand(bsys) % 2 == 0) {
                    moveScore += 2;
                }
                if (ctx->battlemon[attacker].states[STAT_ATTACK] > 7 || ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 7) {
                    moveScore -= 2;
                }
                break;
            }
            case MOVE_COACHING:
            {
                moveScore += 6;
                if (BattleRand(bsys) % 2 == 0) {
                    moveScore += 2;
                }
                if (ctx->battlemon[attacker].states[STAT_ATTACK] > 7) {
                    moveScore -= 1;
                }
                if (ctx->battlemon[attacker].states[STAT_DEFENSE] > 7) {
                    moveScore -= 1;
                }
                break;
            }
            case MOVE_ACUPRESSURE: {
                moveScore += 6;
                if (BattleRand(bsys) % 2 == 0) {
                    moveScore += 2;
                }
                break;
            }
            case MOVE_ROLE_PLAY:
            {
                if ((ai->aimonAlly.ability == ABILITY_HUGE_POWER || ai->aimonAlly.ability == ABILITY_PURE_POWER)
                    && ai->attackerMon.ability != ABILITY_HUGE_POWER
                    && ai->attackerMon.ability != ABILITY_PURE_POWER)
                {
                    moveScore += 9;
                    if (BattleRand(bsys) % 4 == 0) {
                        moveScore += 3;
                    }
                }
                break;
            }
            case MOVE_ENTRAINMENT: {

                if ((ai->attackerMon.ability == ABILITY_NO_GUARD
                        && (BattlerKnowsMove(bsys, BATTLER_ALLY(attacker), MOVE_FISSURE, ai)
                            || BattlerKnowsMove(bsys, BATTLER_ALLY(attacker), MOVE_HORN_DRILL, ai)))
                    || (ai->aimonAlly.ability == ABILITY_TRUANT && ai->aimonAlly.species == SPECIES_SLAKING)
                    || (ai->aimonAlly.ability == ABILITY_SLOW_START && ai->aimonAlly.species == SPECIES_REGIGIGAS)) {
                    moveScore += 6;
                    if (BattleRand(bsys) % 2 == 0) {
                        moveScore += 3;
                    }
                }
                break;
            }
            case MOVE_AQUA_JET:
            case MOVE_MACH_PUNCH:
            case MOVE_BULLET_PUNCH:
            case MOVE_VACUUM_WAVE:
            case MOVE_SHADOW_SNEAK:
            case MOVE_ICE_SHARD:
            case MOVE_QUICK_ATTACK: {
                if (ai->aimonAlly.item == ITEM_WEAKNESS_POLICY) {
                    int movetype = BattleAI_GetDynamicMoveType(bsys, ctx, &ai->attackerMon, attackerMove);
                    u32 moveEffectiveness = BattleAI_GetTypeEffectiveness(bsys, ctx, attackerMove, movetype, attacker, target, &ai->attackerMon, &ai->aimonAlly);
                    if (moveEffectiveness >= TYPE_MUL_SUPER_EFFECTIVE) {
                        moveScore += 6;
                        int rand = (BattleRand(bsys) % 4);
                        if (rand == 0 || rand == 1) {
                            moveScore += 3;
                        }
                        else if (rand == 3) {
                            moveScore += 6;
                        }
                    }
                }
                break;
            }
            }
            if (moveScore > 0) {
                moveScore += 1000;
            }
            moveScores[target][i] += moveScore;
            if (highestScoredMove < moveScores[target][i]) {
                highestScoredMove = moveScores[target][i];
            }
        }
    }
    return highestScoredMove;
}


/*Heavily penalize stupid decisions that would fail, do nothing, or objectively hurt the user.*/
int LONG_CALL BasicScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai)
{
    int moveScore = 0;
    struct BattleStruct *ctx = bsys->sp;

    ai->attackerMove = ctx->battlemon[attacker].move[i];
    ai->attackerMoveEffect = ctx->moveTbl[ai->attackerMove].effect;

    if (ctx->battlemon[ai->attacker].pp[i] == 0) {
        moveScore -= IMPOSSIBLE_MOVE;
    }
    if (ctx->battlemon[ai->attacker].moveeffect.disabledTurns && ai->attackerMove == ctx->battlemon[ai->attacker].moveeffect.disabledMove) {
        moveScore -= IMPOSSIBLE_MOVE;
    }
    if (ctx->battlemon[ai->attacker].condition2 & STATUS2_TORMENT && ai->attackerMove == ai->attackerLastUsedMove) {
        moveScore -= IMPOSSIBLE_MOVE;
    }
    if (ctx->battlemon[ai->attacker].moveeffect.tauntTurns > 0 && ctx->moveTbl[ai->attackerMove].split == SPLIT_STATUS) {
        moveScore -= IMPOSSIBLE_MOVE; // taunted, so no status moves
    }
    if ((IsPowderMove(ai->attackerMove) || ai->attackerMove == MOVE_LEECH_SEED) && HasType(ctx, ai->defender, TYPE_GRASS)) {
        moveScore -= IMMUNE_TO_MOVE;
    }

    // if (ctx->clientPriority[ctx->attack_client] > 0 && GetBattlerAbility(ctx, ctx->attack_client) == ABILITY_PRANKSTER && HasType(ctx, defender, TYPE_DARK) && (ctx->attack_client & 1) != (defender & 1)) // used on an enemy
    if (HasMovePranksterPriority(bsys, ai->attacker, ai->attackerMove, ai->attackerMon.ability, ai->defender) && HasType(ctx, ai->defender, TYPE_DARK))
    {
        moveScore -= IMPOSSIBLE_MOVE; // TODO check
    }

    if (ai->effectivenessOnPlayer[i] == TYPE_MUL_NO_EFFECT && ctx->moveTbl[ai->attackerMove].split != SPLIT_STATUS) // immunity
    {
        moveScore -= IMMUNE_TO_MOVE;
    }

    if (ctx->moveConditionsFlags[ai->attacker].throatChopTimer && IsMoveSoundBased(ai->attackerMove)) {
        moveScore -= IMPOSSIBLE_MOVE;
    }

    if ((ctx->terrainOverlay.type == PSYCHIC_TERRAIN && ctx->terrainOverlay.numberOfTurnsLeft > 0)
        && ai->defenderMon.isGrounded
        && HasMovePriority(bsys, ai->attacker, ai->attackerMove, ai->attackerMon.ability, ai->defender)) {
        moveScore -= IMPOSSIBLE_MOVE;
    }

    switch (ai->attackerMoveEffect) {
    case MOVE_EFFECT_FLING:
        if (ai->attackerMon.item == ITEM_NONE) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        break;
    case MOVE_EFFECT_BELCH:
        if (ai->attackerMon.canBelch == FALSE) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        break;
    case MOVE_EFFECT_STEALTH_ROCK:
        if (ctx->side_condition[ai->defenderSide] & SIDE_STATUS_STEALTH_ROCK || ai->livingMembersDefender == 1 || ai->defenderHasMagicBounce || ai->defenderAllyHasMagicBounce) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_SET_SPIKES:
        if (ctx->scw[ai->defenderSide].spikesLayers >= 3 || ai->livingMembersDefender == 1 || ai->defenderHasMagicBounce || ai->defenderAllyHasMagicBounce) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_TOXIC_SPIKES:
        if (ctx->scw[ai->defenderSide].toxicSpikesLayers >= 2 || ai->livingMembersDefender == 1 || ai->defenderHasMagicBounce || ai->defenderAllyHasMagicBounce) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_STICKY_WEB:
        if (ctx->side_condition[ai->defenderSide] & SIDE_STATUS_STICKY_WEB || ai->livingMembersDefender == 1 || ai->defenderHasMagicBounce || ai->defenderAllyHasMagicBounce) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_APPLY_TERRAINS: {
        switch (ai->attackerMove) {
        case MOVE_GRASSY_TERRAIN:
            if (ctx->terrainOverlay.type == GRASSY_TERRAIN && ctx->terrainOverlay.numberOfTurnsLeft > 0) {
                moveScore -= NEVER_USE_MOVE_20;
            }
            break;
        case MOVE_MISTY_TERRAIN:
            if (ctx->terrainOverlay.type == MISTY_TERRAIN && ctx->terrainOverlay.numberOfTurnsLeft > 0) {
                moveScore -= NEVER_USE_MOVE_20;
            }
            break;
        case MOVE_ELECTRIC_TERRAIN:
            if (ctx->terrainOverlay.type == ELECTRIC_TERRAIN && ctx->terrainOverlay.numberOfTurnsLeft > 0) {
                moveScore -= NEVER_USE_MOVE_20;
            }
            break;
        case MOVE_PSYCHIC_TERRAIN:
            if (ctx->terrainOverlay.type == PSYCHIC_TERRAIN && ctx->terrainOverlay.numberOfTurnsLeft > 0) {
                moveScore -= NEVER_USE_MOVE_20;
            }
            break;
        }
        break;
    }
    case MOVE_EFFECT_PARALYZE_HIT:
        if (ctx->moveTbl[ai->attackerMove].secondaryEffectChance == 100) // nuzzle
        {
            if (ai->defenderImmuneToParalysis || ai->effectivenessOnPlayer[i] == TYPE_MUL_NO_EFFECT) {
                moveScore -= NEVER_USE_MOVE_20;
            }
        }
        break;
    case MOVE_EFFECT_STATUS_PARALYZE:
        if (ai->defenderImmuneToParalysis || ai->effectivenessOnPlayer[i] == TYPE_MUL_NO_EFFECT) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_STATUS_BURN:
        if (ai->defenderImmuneToBurn || ai->effectivenessOnPlayer[i] == TYPE_MUL_NO_EFFECT) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_SWITCH_HELD_ITEMS:
        if (ai->attackerMon.item == ITEM_NONE && ai->defenderMon.item == ITEM_NONE) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_STATUS_SLEEP:
    case MOVE_EFFECT_STATUS_SLEEP_NEXT_TURN:
        if (ai->defenderImmuneToSleep || (ai->defenderMon.effect_of_moves & MOVE_EFFECT_FLAG_YAWN)) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_STATUS_POISON:
    case MOVE_EFFECT_STATUS_BADLY_POISON:
        if (ai->defenderImmuneToPoison) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_STATUS_NIGHTMARE:
        if (ctx->battlemon[ai->defender].condition2 & STATUS2_NIGHTMARE || ai->defenderMon.ability == ABILITY_MAGIC_GUARD) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_FAIL_IF_NOT_USED_ALL_OTHER_MOVES:
        if (ctx->battlemon[attacker].moveeffect.lastResortCount < ai->attackerMovesKnown - 1) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_WEATHER_SANDSTORM:
        if (ctx->field_condition & FIELD_CONDITION_SANDSTORM_ALL) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_WEATHER_HAIL:
    case MOVE_EFFECT_WEATHER_SNOW:
        if (ctx->field_condition & (FIELD_CONDITION_SNOW_ALL | FIELD_CONDITION_HAIL_ALL)) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_WEATHER_RAIN:
        if (ctx->field_condition & FIELD_CONDITION_RAIN_ALL) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_WEATHER_SUN:
        if (ctx->field_condition & FIELD_CONDITION_SUN_ALL) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    default:
        break;
    }

    if (ai->attackerMove == MOVE_STEEL_ROLLER && ctx->terrainOverlay.type == TERRAIN_NONE) {
        moveScore -= IMPOSSIBLE_MOVE;
    }

    if (ai->attackerMove == MOVE_GIGATON_HAMMER && ai->attackerLastUsedMove == MOVE_GIGATON_HAMMER) {
        moveScore -= IMPOSSIBLE_MOVE;
    }

    if (ai->attackerMove == MOVE_SOLAR_BEAM){
        if (ai->attackerMon.ability != ABILITY_MEGA_SOL && ((ctx->field_condition & FIELD_CONDITION_SUN_ALL) == 0) && ai->attackerMon.item != ITEM_POWER_HERB) {
            moveScore -= NEVER_USE_MOVE_20;
        }
    }

    return moveScore;
}

int LONG_CALL DamagingMoveScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai)
{
    int moveScore = 0;
    struct BattleStruct *ctx = bsys->sp;
    BOOL isMoveHighestDamage = FALSE;

    if (ctx->moveTbl[ai->attackerMove].split == SPLIT_STATUS) {
        return 0;
    }

    ai->attackerMove = ctx->battlemon[attacker].move[i];
    ai->attackerMoveEffect = ctx->moveTbl[ai->attackerMove].effect;


    switch (ai->attackerMove) {
    case MOVE_SELF_DESTRUCT:
    case MOVE_EXPLOSION:
    case MOVE_MISTY_EXPLOSION:
        if ((ai->defenderMon.ability != ABILITY_DAMP || ai->attackerMon.hasMoldBreaker) && ai->effectivenessOnPlayer[i] > TYPE_MUL_NO_EFFECT) // no immunity
        {
            if (ai->attackerMon.percenthp < 10) {
                moveScore += 10;
            } else if (ai->attackerMon.percenthp < 33) {
                if (BattleRand(bsys) % 10 < 7) { // 70%
                    moveScore += 8;
                }
            } else if (ai->attackerMon.percenthp < 66) {
                if (BattleRand(bsys) % 2) { // 50%
                    moveScore += 7;
                }
            } else {
                if (BattleRand(bsys) % 20 == 0) { // 5%
                    moveScore += 7;
                }
            }
#if EXPLOSION_ON_LAST_MON
            if (ai->livingMembersAttacker == 1 && ai->livingMembersDefender > 1) {
                moveScore -= NEVER_USE_MOVE_20;
            }
            if (ai->livingMembersAttacker == 1 && ai->livingMembersDefender == 1) {
                moveScore -= 1;
            }
#endif
        }
        break;
    case MOVE_FINAL_GAMBIT:
        if (ai->effectivenessOnPlayer[i] > TYPE_MUL_NO_EFFECT) {
            if (ai->aiMovesFirst && ai->attackerMon.hp >= ai->defenderMon.hp) {
                moveScore += 8;
            } else if (ai->aiMovesFirst && ai->playerCanOneShotMonWithAnyMove) {
                moveScore += 7;
            } else {
                moveScore += 6;
            }
        }
        break;
    case MOVE_ICE_BALL:
    case MOVE_ROLLOUT:
        moveScore += 7;
        break;
    case MOVE_METEOR_BEAM:
        if (ai->attackerMon.item == ITEM_POWER_HERB) {
            moveScore += 9;
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_ELECTRO_SHOT:
        if (ai->attackerMon.item == ITEM_POWER_HERB || ctx->field_condition & FIELD_CONDITION_RAIN_ALL) {
            moveScore += 9;
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_SAND_TOMB:
        if (!ai->defenderMon.isGrounded) {
            break;
        }
        FALLTHROUGH;
    case MOVE_BIND:
    case MOVE_CLAMP:
    case MOVE_FIRE_SPIN:
    case MOVE_WRAP:
    case MOVE_WHIRLPOOL:
    case MOVE_INFESTATION:
        if (ctx->binding_turns[ai->defender] != 0 || HasType(ctx, ai->defender, TYPE_GHOST) || ai->attackerMon.item == ITEM_SHED_SHELL) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
            if (BattleRand(bsys) % 10 < 2) {
                moveScore += 2;
            }
        }
        break;
    case MOVE_RELIC_SONG: // TODO
        break;
    case MOVE_DOOM_DESIRE:
    case MOVE_FUTURE_SIGHT:
        if (ctx->fcc.future_prediction_count[ai->defender] != 0) {
            moveScore -= IMPOSSIBLE_MOVE;
            break;
        }
        if (ai->aiMovesFirst && ai->playerCanOneShotMonWithAnyMove) {
            moveScore += 8;
        } else {
            moveScore += 6;
        }
        break;
    default:
        if (ai->attackerRolledMaxDamage == ai->attackerRolledMoveDamages[i]) {
            isMoveHighestDamage = TRUE;
            moveScore += 6;
            if ((ai->attackerMoveEffect != MOVE_EFFECT_ONE_HIT_KO || ai->attackerMon.ability == ABILITY_NO_GUARD)
                && ((BattleRand(bsys) % 10) < 2)) { // OHKO moves dont the random +2 unless No Guard
                moveScore += 2;
            }
        }
        break;
    }

#ifdef DEBUG_AI_SCORING
    debug_printf("move %d is %s damage %d == %d\n", i, ((isMoveHighestDamage == TRUE) ? "highest" : "not highest"), ai->attackerRolledMaxDamage, ai->attackerRolledMoveDamages[i]);
#endif
    if (ai->monCanOneShotPlayerWithMove[i]) // if ai sees kill with this move
    {
        if (ai->aiMovesFirst || (ai->playerMovesFirst && ctx->moveTbl[ai->attackerMove].priority > 0)) {
            moveScore += 6; // and is faster/has priority
        } else if (ai->playerMovesFirst) {
            moveScore += 3;
        }
        if (ai->attackerMon.ability == ABILITY_MOXIE || ai->attackerMon.ability == ABILITY_BEAST_BOOST 
            || ai->attackerMon.ability == ABILITY_CHILLING_NEIGH || ai->attackerMon.ability == ABILITY_GRIM_NEIGH
            || ai->attackerMon.ability == ABILITY_EELEVATE) {
            moveScore += 1;
        }
    }

    if (ai->attackerMoveEffect == MOVE_EFFECT_HIGH_CRITICAL 
        && (ai->effectivenessOnPlayer[i] == TYPE_MUL_SUPER_EFFECTIVE || ai->effectivenessOnPlayer[i] == TYPE_MUL_DOUBLE_SUPER_EFFECTIVE || ai->effectivenessOnPlayer[i] == TYPE_MUL_TRIPLE_SUPER_EFFECTIVE)) {
        if (BattleRand(bsys) % 2 == 0) {
            moveScore += 2;
        }
    }

    if (ai->playerMovesFirst && ai->playerCanOneShotMonWithAnyMove)
    {
        if (HasMovePriority(bsys, ai->attacker, ai->attackerMove, ai->attackerMon.ability, ai->defender)) {
            moveScore += 11;
        }
    }
    if (!isMoveHighestDamage) {
        switch (ai->attackerMoveEffect) {
        case MOVE_EFFECT_LOWER_SPEED_HIT:
            if (ctx->moveTbl[ai->attackerMove].secondaryEffectChance == 100 && !ai->defenderImmuneToStatDrop)
            {
                if (ai->playerMovesFirst) {
                    moveScore += 6;
                } else {
                    moveScore += 5;
                }
                if (ai->isDoubleBattle) {
                    if (ai->attackerMove == MOVE_ICY_WIND || ai->attackerMove == MOVE_ELECTROWEB) {
                        moveScore += 1;
                    }
                }
            }
            break;
        case MOVE_EFFECT_LOWER_ATTACK_HIT:
            if (ctx->moveTbl[ai->attackerMove].secondaryEffectChance == 100 && !ai->defenderImmuneToStatDrop) {
                if (ai->defenderHasAtleastOnePhysicalMove) {
                    moveScore += 6;
                } else {
                    moveScore += 5;
                }

                if (ai->isDoubleBattle) {
                    if (ctx->moveTbl[ai->attackerMove].target == RANGE_ADJACENT_OPPONENTS) {
                        moveScore += 1;
                    }
                }
            }
            break;
        case MOVE_EFFECT_LOWER_SP_ATK_HIT:
            if (ctx->moveTbl[ai->attackerMove].secondaryEffectChance == 100 && !ai->defenderImmuneToStatDrop) {
                if (ai->defenderHasAtleastOneSpecialMove) {
                    moveScore += 6;
                } else {
                    moveScore += 5;
                }

                if (ai->isDoubleBattle) {
                    if (ctx->moveTbl[ai->attackerMove].target == RANGE_ADJACENT_OPPONENTS) {
                        moveScore += 1;
                    }
                }
            }
            break;
        case MOVE_EFFECT_USER_DEF_DOWN_HIT:
        case MOVE_EFFECT_USER_SPEED_DOWN_2_HIT:
        case MOVE_EFFECT_USER_SPEED_DOWN_HIT:
        case MOVE_EFFECT_USER_SP_ATK_DOWN_2:
        case MOVE_EFFECT_MAKE_IT_RAIN:
        case MOVE_EFFECT_USER_DEF_SP_DEF_DOWN_HIT:
        case MOVE_EFFECT_USER_ATK_DEF_DOWN_HIT:
        case MOVE_EFFECT_USER_DEF_SP_DEF_SPEED_DOWN_HIT:
            if (ai->attackerMon.ability == ABILITY_CONTRARY) { // no Unaware check
                if (ai->isDefenderIncapacitated) {
                    moveScore += 3;
                }
                if (ai->aiMovesFirst) {
                    moveScore += 3;
                }
                if (3 * ai->maxDamageReceived < ai->attackerMon.hp) {
                    moveScore += 3;
                    if (ai->aiMovesFirst) {
                        moveScore += 1;
                    }
                }
            }
            break;
        default:
            break;
        }
    }

    switch (ai->attackerMove) {
    case MOVE_U_TURN:
    case MOVE_FLIP_TURN:
    case MOVE_VOLT_SWITCH:
        if (ai->effectivenessOnPlayer[i] > TYPE_MUL_NO_EFFECT) {
            if (!isMoveHighestDamage && ai->livingMembersAttacker > (1+ai->isDoubleBattle)) {
                moveScore += 6;
            }
            if (ai->shouldSwitch) {
                moveScore += 3;
            }
            if (ai->attackerMon.ability == ABILITY_REGENERATOR && ai->attackerMon.percenthp < 67) {
                moveScore += 1;
            }
        }
        break;
    case MOVE_ACID_SPRAY: {
        if (!ai->defenderImmuneToStatDrop) {
            if (ai->defenderMon.ability != ABILITY_BULLETPROOF || ai->attackerMon.hasMoldBreaker) {
                moveScore += 6;
            }
        }
        break;
    }
    case MOVE_SUCKER_PUNCH: {
        if (ai->attackerLastUsedMove == MOVE_SUCKER_PUNCH) {
            if (BattleRand(bsys) % 2 == 0) {
                moveScore -= NEVER_USE_MOVE_20;
            }
        }
        break;
    }
    case MOVE_FIRST_IMPRESSION: {
        if (ai->attackerTurnsOnField == 0) {
            moveScore += 9;
        } else {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        break;
    }
    case MOVE_FAKE_OUT: {
        if (ai->attackerTurnsOnField == 0 && ai->defenderMon.item != ITEM_COVERT_CLOAK) {
            if (ai->attackerMon.hasMoldBreaker || (ai->defenderMon.ability != ABILITY_SHIELD_DUST && ai->defenderMon.ability != ABILITY_INNER_FOCUS)) {
                moveScore += 9;
            } else {
                    moveScore -= IMPOSSIBLE_MOVE;
            }
        } else {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        break;
    }
    case MOVE_PURSUIT: {
        if (ai->aiMovesFirst) {
            moveScore += 3;
        }

        if (ai->monCanOneShotPlayerWithMove[i]) {
            moveScore += 10;
        } else if (ai->defenderMon.percenthp < 20) {
            moveScore += 10;
        } else if (ai->defenderMon.percenthp < 40) {
            if (BattleRand(bsys) % 2 == 0) {
                moveScore += 8;
            }
        }
        break;
    }
    case MOVE_FELL_STINGER: {
        if (ctx->battlemon[ai->attacker].states[STAT_ATTACK] < 12 && ai->monCanOneShotPlayerWithMove[i]) {
            if (ai->aiMovesFirst) {
                moveScore += 12;
            } else {
                moveScore += 6;
            }
        }
        break;
    }
    case MOVE_FOCUS_PUNCH:
    {
        if (ai->isDefenderIncapacitated || ctx->battlemon[ai->attacker].condition2 & STATUS2_SUBSTITUTE) {
            moveScore += 2;
        } else {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        break;
    }
    case MOVE_DRAGON_TAIL:
    case MOVE_CIRCLE_THROW:
        if (ai->aiMovesFirst)
        {
            moveScore -= 2;
        }
        break;
    case MOVE_EARTHQUAKE:
    case MOVE_MAGNITUDE: {
        if (ai->isDoubleBattle) {
            if (ai->isPartnerGrounded && ai->aimonAlly.ability != ABILITY_TELEPATHY) // TODO: or about to use magnetrise && faster
            {
                if (ctx->battlemon[BATTLER_ALLY(ai->attacker)].hp) {
                    if (HasType(ctx, BATTLER_ALLY(ai->attacker), TYPE_POISON) || HasType(ctx, BATTLER_ALLY(ai->attacker), TYPE_STEEL) || HasType(ctx, BATTLER_ALLY(ai->attacker), TYPE_FIRE) || HasType(ctx, BATTLER_ALLY(ai->attacker), TYPE_ROCK)) {
                        moveScore -= 10;
                    } else {
                        moveScore -= 3;
                    }
                }
            } else {
                moveScore += 2;
            }
        }
    }
    default:
        break;
    }

    return moveScore;
}


int LONG_CALL OffensiveSetup(struct BattleSystem *bsys UNUSED, u32 attacker UNUSED, int i UNUSED, struct AIContext *ai)
{
#ifdef DEBUG_AI_SCORING
    debug_printf("Off: incap %d, movesFirst %d, is1H %d, maxRec %d, hp %d", ai->isDefenderIncapacitated, ai->aiMovesFirst, ai->playerCanOneShotMonWithAnyMove, ai->maxDamageReceived, ai->attackerMon.hp);
#endif
    int moveScore = 0;
    if (ai->isDefenderIncapacitated) {
        moveScore += 3;
    }

    BOOL shouldSetup = TRUE;
    if (ai->defenderHasSturdyOrFocusSashActive) {
        shouldSetup = FALSE;
    }

    if (shouldSetup) {
        if (ai->aiMovesFirst) {
            if (!ai->playerCanOneShotMonWithAnyMove && !ai->isDoubleBattle) {
                moveScore += 2;
            }
        }

        if (3 * ai->maxDamageReceived < ai->attackerMon.hp) {
            moveScore += 3;
            if (ai->aiMovesFirst) {
                moveScore += 1;
            }
        }
    }

    if (!ai->aiMovesFirst && (2 * ai->maxDamageReceived >= ai->attackerMon.hp)) {
        moveScore -= 5;
    }
    if ((2 * ai->maxDamageReceived >= ai->attackerMon.hp) && (BattleRand(bsys) % 2 == 0))
    {
        moveScore -= 2;
    }

#ifdef DEBUG_AI_SCORING
    debug_printf(", score %d\n", moveScore);
#endif
    return moveScore;
}
int LONG_CALL DefensiveSetup(struct BattleSystem *bsys UNUSED, u32 attacker UNUSED, int i UNUSED, struct AIContext *ai)
{
#ifdef DEBUG_AI_SCORING
    debug_printf("Def: incap %d, movesFirst %d, is1H %d, maxRec %d, hp %d", ai->isDefenderIncapacitated, ai->aiMovesFirst, ai->playerCanOneShotMonWithAnyMove, ai->maxDamageReceived, ai->attackerMon.hp);
#endif
    int moveScore = 0;
    if (ai->isDefenderIncapacitated) {
        moveScore += 2;
    }
    if (ai->playerMovesFirst && 2 * ai->maxDamageReceived >= ai->attackerMon.hp) {
        moveScore -= 5;
    }
    if (3 * ai->maxDamageReceived < ai->attackerMon.hp) {
        moveScore += 3;
        if (ai->aiMovesFirst) {
            moveScore += 1;
        }
    }
#ifdef DEBUG_AI_SCORING
    debug_printf(", score %d\n", moveScore);
#endif
    return moveScore;
}

int LONG_CALL SetupScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai)
{
    int moveScore = 0;
    struct BattleStruct *ctx = bsys->sp;
    BOOL hasSitrus = (ai->attackerMon.item == ITEM_SITRUS_BERRY);
    BOOL shouldSetup = TRUE;
    BOOL isSetupMove = TRUE;

    ai->attackerMove = ctx->battlemon[attacker].move[i];
    ai->attackerMoveEffect = ctx->moveTbl[ai->attackerMove].effect;

    if (ai->playerCanOneShotMonWithAnyMove || ai->defenderCanForceSwitching) {
        return 0;
    }
    if (ai->defenderMon.ability == ABILITY_UNAWARE && (BattleRand(bsys) % 4 > 0)) {
        return 0;
    }

    switch (ai->attackerMoveEffect) {
    case MOVE_EFFECT_WEATHER_RAIN:
        if (ctx->field_condition & FIELD_CONDITION_RAIN_ALL) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            if (ai->attackerMon.ability == ABILITY_SWIFT_SWIM
                || ai->attackerMon.ability == ABILITY_RAIN_DISH
                || ai->attackerMon.ability == ABILITY_HYDRATION
                || ai->attackerMon.ability == ABILITY_DRY_SKIN) {
                moveScore += 9;
            }
            if (ai->attackerMon.item == ITEM_DAMP_ROCK) {
                moveScore += 1;
            }
        }
        break;
    case MOVE_EFFECT_WEATHER_HAIL:
    case MOVE_EFFECT_WEATHER_SNOW:
        // case MOVE_EFFECT_SNOWSCAPE:
        if (ctx->field_condition & (FIELD_CONDITION_SNOW_ALL|FIELD_CONDITION_HAIL_ALL)) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            if (HasType(ctx, attacker, TYPE_ICE)
                || ai->attackerMon.ability == ABILITY_ICE_BODY || ai->attackerMon.ability == ABILITY_SNOW_CLOAK
                || ai->attackerMon.ability == ABILITY_SLUSH_RUSH) {
                moveScore += 9;
            }
            if (ai->attackerMon.item == ITEM_ICY_ROCK) {
                moveScore += 1;
            }
        }
        break;
    case MOVE_EFFECT_WEATHER_SUN:
        if (ctx->field_condition & FIELD_CONDITION_SUN_ALL) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            if (HasType(ctx, attacker, TYPE_FIRE) || ai->attackerMon.ability == ABILITY_SOLAR_POWER) { //|| ai->attackerMon.ability == ABILITY_CHLOROPHYL
                moveScore += 9;
            }
            if (ai->attackerMon.item == ITEM_HEAT_ROCK) {
                moveScore += 1;
            }
        }
        break;
    case MOVE_EFFECT_WEATHER_SANDSTORM:
        if (ctx->field_condition & FIELD_CONDITION_SANDSTORM_ALL) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            if (HasType(ctx, attacker, TYPE_ROCK) || HasType(ctx, attacker, TYPE_GROUND) || HasType(ctx, attacker, TYPE_STEEL)
                || ai->attackerMon.ability == ABILITY_SAND_FORCE || ai->attackerMon.ability == ABILITY_SAND_RUSH || ai->attackerMon.ability == ABILITY_SAND_VEIL) {
                moveScore += 9;
            }
            if (ai->attackerMon.item == ITEM_SMOOTH_ROCK) {
                moveScore += 1;
            }
        }
        break;
    case MOVE_EFFECT_COPY_STAT_CHANGES: {
        u8 sumStatChange = BattlerPositiveStatChangesSum(bsys, ai->defender);
        if (sumStatChange > 0) {
            moveScore += 5;
        }
        if (sumStatChange > 1) {
            moveScore += sumStatChange;
        }
        break;
    }
    case MOVE_EFFECT_EVA_UP:
    case MOVE_EFFECT_EVA_UP_2:
    case MOVE_EFFECT_EVA_UP_3:
    case MOVE_EFFECT_EVA_UP_2_MINIMIZE:
        moveScore += 6;
        if (ai->defenderMon.ability == ABILITY_NO_GUARD || ctx->battlemon[attacker].states[STAT_EVASION] >= 12) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        if (ctx->battlemon[attacker].states[STAT_EVASION] >= 7) {
            moveScore -= 1;
        } else if (2 * ai->maxDamageReceived < ai->attackerMon.hp) {
            moveScore += 2;
        }
        if (ai->isDefenderIncapacitated) {
            moveScore += 2;
        }
        break;
    case MOVE_EFFECT_REMOVE_HAZARDS_AND_BINDING: // rapid_spin
    case MOVE_EFFECT_RAISE_SPEED_HIT: // trailblaze
        if (ctx->moveTbl[ai->attackerMove].secondaryEffectChance < 100 || ai->attackerRolledMaxDamage == ai->attackerRolledMoveDamages[i]) // if other move (like ...), or highest damage
        {
            isSetupMove = FALSE;
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_SPEED_UP_2: //agility, rock polish
    case MOVE_EFFECT_AUTOTOMIZE:
        if (ai->playerMovesFirst) {
            moveScore += 7;
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_CRIT_UP_2:
        if (ai->defenderMon.ability == ABILITY_SHELL_ARMOR || ai->defenderMon.ability == ABILITY_BATTLE_ARMOR || ctx->battlemon[ctx->defence_client].condition2 & STATUS2_FOCUS_ENERGY) {
            moveScore -= NEVER_USE_MOVE_20;
        } else if (ai->attackerMon.ability == ABILITY_SUPER_LUCK || ai->attackerMon.ability == ABILITY_SNIPER || ai->attackerMon.item == ITEM_SCOPE_LENS) { // or hight crit moves
            moveScore += 7;
        } else {
            moveScore += 5;
        }
        break;
    // case MOVE_EFFECT_ATK_SP_ATK_SPEED_UP_2_LOSE_HALF_MAX_HP: //filet away
    case MOVE_EFFECT_MAX_ATK_LOSE_HALF_MAX_HP: // belly drum
        moveScore += 4;
        if (ai->isDefenderIncapacitated) {
            moveScore += 5;
        } else if (2 * ai->attackerMon.hp > ai->attackerMon.maxhp) {
            if (hasSitrus) {
                if (2 * ai->maxDamageReceived < (u32)(25 * ai->attackerMon.hp / 10 - ai->attackerMon.maxhp)) { // D < 1.125*Hp - 0.5maxHp
                    moveScore += 4;
                }
            } else if (2 * ai->maxDamageReceived < (u32)(2 * ai->attackerMon.hp - ai->attackerMon.maxhp)) { // D < Hp - 0.5maxHp
                moveScore += 4;
            }
        }
        break;
    case MOVE_EFFECT_RAISE_ALL_STATS_LOSE_THIRD_MAX_HP: // clangorous soul
    case MOVE_EFFECT_ATK_SP_ATK_SPEED_UP_2_DEF_SP_DEF_DOWN: // shell smash
        moveScore += 6;
        if (ai->isDefenderIncapacitated) {
            moveScore += 3;
        }
        if (ai->aiMovesFirst) // if ai is slower, no defense drop for hit
        {
            if (ai->attackerMon.item == ITEM_WHITE_HERB || 2 * ai->maxDamageReceived < ai->attackerMon.hp) { // simple heuristic to not calc exact dmg.
                moveScore += 2;
            } else {
                moveScore -= 2;
            }
        }
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 7 || ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 7) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_RAISE_SP_ATK_HIT: // charge beam
    case MOVE_EFFECT_RAISE_ATTACK_HIT: // PuP
        if (ctx->moveTbl[ai->attackerMove].secondaryEffectChance < 100 || ai->attackerRolledMaxDamage == ai->attackerRolledMoveDamages[i]) // if other move (like Metal Claw), or highest damage
        {
            isSetupMove = FALSE;
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_SP_ATK_UP: // growth
    case MOVE_EFFECT_SP_ATK_UP_2: // nasty plot
    case MOVE_EFFECT_SP_ATK_UP_3: // tail glow
    case MOVE_EFFECT_ATK_SP_ATK_UP: // work up
    case MOVE_EFFECT_ATK_ACC_UP: // hone claws
    case MOVE_EFFECT_ATK_UP: // howl
    case MOVE_EFFECT_ATK_UP_2: // swords dance
    case MOVE_EFFECT_ATK_UP_3:
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 11 || ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 11) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 7 || ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 7) {
            moveScore -= 2;
        }
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 8 || ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 8) {
            moveScore -= 1;
        }
        moveScore += 6;
        moveScore += OffensiveSetup(bsys, attacker, i, ai);
        break;
    case MOVE_EFFECT_ATK_SPEED_UP: // dragon dance
    case MOVE_EFFECT_SPEED_UP_2_ATK_UP: // shift gear
    case MOVE_EFFECT_TIDY_UP: // tidy up is basically ddance
    case MOVE_EFFECT_ATK_DEF_SPEED_UP: // victory dance
    case MOVE_EFFECT_RANDOM_STAT_UP_2: // accupressure
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 11 || ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 11) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 7 || ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 7 || ai->aiMovesFirst) {
            moveScore -= 2;
        }
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 8 || ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 8 || ai->aiMovesFirst) {
            moveScore -= 1;
        }
        moveScore += 6;
        moveScore += OffensiveSetup(bsys, attacker, i, ai);
        break;
    case MOVE_EFFECT_GROUND_TRAP_USER_CONTINUOUS_HEAL: // Ingrain
        moveScore += 6;
        if (ctx->battlemon[attacker].effect_of_moves & MOVE_EFFECT_FLAG_INGRAIN) {
            moveScore -= IMPOSSIBLE_MOVE;
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_RESTORE_HP_EVERY_TURN: // Aqua Ring
        moveScore += 6;
        if (ctx->battlemon[attacker].effect_of_moves & MOVE_EFFECT_FLAG_AQUA_RING) {
            moveScore -= IMPOSSIBLE_MOVE;
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_DEF_UP: // harden
    case MOVE_EFFECT_DEF_UP_2: // iron defense
    case MOVE_EFFECT_DEF_UP_3: // cotton guard
        if (ai->defenderHasAtleastOnePhysicalMove == FALSE) {
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_SP_DEF_UP:
    case MOVE_EFFECT_SP_DEF_UP_2: // amnesia
    case MOVE_EFFECT_SP_DEF_UP_3:
        if (ai->attackerMoveEffect == MOVE_EFFECT_SP_DEF_UP || ai->attackerMoveEffect == MOVE_EFFECT_SP_DEF_UP_2 || ai->attackerMoveEffect == MOVE_EFFECT_SP_DEF_UP_3) {
            if (ai->defenderHasAtleastOneSpecialMove == FALSE) {
                break;
            }
        }
        FALLTHROUGH;
    case MOVE_EFFECT_DEF_UP_DOUBLE_ROLLOUT_POWER: // defense curl
    case MOVE_EFFECT_SP_DEF_UP_DOUBLE_ELECTRIC_POWER: // Charge
    case MOVE_EFFECT_DEF_SP_DEF_UP: // cosmic power
    case MOVE_EFFECT_STUFF_CHEEKS:
        if (ctx->battlemon[attacker].states[STAT_DEFENSE] > 11 || ctx->battlemon[attacker].states[STAT_SPECIAL_DEFENSE] > 11) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        if (ctx->battlemon[attacker].states[STAT_DEFENSE] > 7 || ctx->battlemon[attacker].states[STAT_SPECIAL_DEFENSE] > 7) {
            moveScore -= 3;
        }
        if (ctx->battlemon[attacker].states[STAT_DEFENSE] > 8 || ctx->battlemon[attacker].states[STAT_SPECIAL_DEFENSE] > 8) {
            moveScore -= 1;
        }
        if (ai->attackerMoveEffect == MOVE_EFFECT_STUFF_CHEEKS && !IS_ITEM_BERRY(ai->attackerMon.item)) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        moveScore += 6;
        moveScore += DefensiveSetup(bsys, attacker, i, ai);
        break;
    case MOVE_EFFECT_STOCKPILE:
        if (ctx->battlemon[attacker].states[STAT_DEFENSE] > 8 || ctx->battlemon[attacker].states[STAT_SPECIAL_DEFENSE] > 8) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        if (ctx->battlemon[attacker].states[STAT_DEFENSE] > 7 || ctx->battlemon[attacker].states[STAT_SPECIAL_DEFENSE] > 7) {
            moveScore -= 3;
        }
        moveScore += 6;
        moveScore += DefensiveSetup(bsys, attacker, i, ai);
        break;
    case MOVE_EFFECT_SP_ATK_SP_DEF_SPEED_UP: // quiver dance
    case MOVE_EFFECT_SP_ATK_SP_DEF_UP: // Calm Mind
        moveScore += 6;
        if (ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 11 || ctx->battlemon[attacker].states[STAT_SPECIAL_DEFENSE] > 11) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        if (ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 7 || ctx->battlemon[attacker].states[STAT_SPECIAL_DEFENSE] > 7) {
            moveScore -= 2;
        }
        if (ctx->battlemon[attacker].states[STAT_SPECIAL_ATTACK] > 8 || ctx->battlemon[attacker].states[STAT_SPECIAL_DEFENSE] > 8) {
            moveScore -= 1;
        }
        if (ai->defenderHasAtleastOnePhysicalMove == FALSE && ai->defenderHasAtleastOneSpecialMove) {
            moveScore += DefensiveSetup(bsys, attacker, i, ai);
        } else {
            moveScore += OffensiveSetup(bsys, attacker, i, ai);
        }
        break;
    case MOVE_EFFECT_CURSE:
        if (HasType(ctx, attacker, TYPE_GHOST)) {
            isSetupMove = FALSE;
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_ATK_DEF_UP: // Bulk Up
    case MOVE_EFFECT_ATK_DEF_ACC_UP: // coil
        // case MOVE_EFFECT_NO_RETREAT: // No Retreat
        moveScore += 6;
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 11 || ctx->battlemon[attacker].states[STAT_DEFENSE] > 11) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 7 || ctx->battlemon[attacker].states[STAT_DEFENSE] > 7) {
            moveScore -= 2;
        }
        if (ctx->battlemon[attacker].states[STAT_ATTACK] > 8 || ctx->battlemon[attacker].states[STAT_DEFENSE] > 8) {
            moveScore -= 1;
        }
        if (ai->defenderHasAtleastOnePhysicalMove && ai->defenderHasAtleastOneSpecialMove == FALSE) {
            moveScore += DefensiveSetup(bsys, attacker, i, ai);
        } else {
            moveScore += OffensiveSetup(bsys, attacker, i, ai);
        }
        break;
    case MOVE_EFFECT_CHARGE_TURN_ATK_SP_ATK_SPEED_UP_2: // geomancy
        if (ai->attackerMon.item == ITEM_POWER_HERB) {
            moveScore += 9;
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_NEXT_ATTACK_ALWAYS_HITS: // mind reader, lock on
        if (ctx->field_condition & FIELD_CONDITION_FOG
            && (BattlerKnowsMove(bsys, ai->defender, MOVE_DEFOG, ai) == FALSE)
            && (ctx->battlemon[ai->defender].effect_of_moves & MOVE_EFFECT_FLAG_LOCK_ON) == 0) {

            moveScore += 6;
            if (ai->aiMovesFirst) {
                if (!ai->playerCanOneShotMonWithAnyMove) {
                    moveScore += 3;
                }
            }
        }
        else
        {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    default:
        isSetupMove = FALSE;
        break;
    }
    if (shouldSetup == FALSE) {
        moveScore = 0;
    }
    if (isSetupMove == FALSE) {
        moveScore = 0;
    }

    return moveScore;
}

int LONG_CALL HarassmentScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai)
{
    int moveScore = 0;
    BOOL sharesMoves = FALSE;
    struct BattleStruct *ctx = bsys->sp;

    ai->attackerMove = ctx->battlemon[attacker].move[i];
    ai->attackerMoveEffect = ctx->moveTbl[ai->attackerMove].effect;

    switch (ai->attackerMoveEffect) {
    case MOVE_EFFECT_PARTING_SHOT:
    {
        if (ai->defenderImmuneToStatDrop)
        {
            moveScore -= NEVER_USE_MOVE_20;
            break;
        }

        if (ai->effectivenessOnPlayer[i] > TYPE_MUL_NO_EFFECT) {
            if (ai->livingMembersAttacker > (1 + ai->isDoubleBattle)) {
                moveScore += 6;
            }
            if (ai->shouldSwitch) {
                moveScore += 3;
            }
            if (ai->attackerMon.ability == ABILITY_REGENERATOR && ai->attackerMon.percenthp < 67) {
                moveScore += 1;
            }
        }
        break;
    }
    case MOVE_EFFECT_TAUNT: //TODO
        if (ctx->battlemon[ai->defender].moveeffect.tauntTurns > 0)
        {
            moveScore -= NEVER_USE_MOVE_20;
        }
        else if (ai->defenderMon.ability == ABILITY_OBLIVIOUS || ai->defenderMon.ability == ABILITY_AROMA_VEIL
            || (ai->isDoubleBattle && ctx->battlemon[BATTLER_ALLY(attacker)].hp > 0 && ctx->battlemon[BATTLER_ALLY(attacker)].ability == ABILITY_AROMA_VEIL))
        {
            moveScore -= NEVER_USE_MOVE_20;
        }
        else if (BattlerKnowsMove(bsys, ai->defender, MOVE_DEFOG, ai) == TRUE && ai->aiMovesFirst)
        {
            moveScore += 9;
        }
        else
        {
            moveScore += 5;
        }
        break;
    case MOVE_EFFECT_RECYCLE: {
        if (ai->attackerMon.item == ITEM_NONE && ctx->recycle_item[attacker] == ITEM_BERRY_JUICE) {
            moveScore += 6;
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    }
    case MOVE_EFFECT_GRAVITY:
    {
        if (ctx->field_condition & FIELD_CONDITION_GRAVITY) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
        }
        break;
    }
    case MOVE_EFFECT_STEALTH_ROCK:
    case MOVE_EFFECT_SET_SPIKES:
    case MOVE_EFFECT_TOXIC_SPIKES:
        if (ai->attackerTurnsOnField == 0) {
            moveScore += 8;
        } else {
            moveScore += 6;
        }
        if (BattleRand(bsys) % 4 != 0) {
            moveScore += 1;
        }
        if (ctx->scw[ai->defenderSide].spikesLayers > 0 && ai->attackerMoveEffect == MOVE_EFFECT_SET_SPIKES) {
            moveScore -= 1;
        }
        if (ctx->scw[ai->defenderSide].toxicSpikesLayers > 0 && ai->attackerMoveEffect == MOVE_EFFECT_TOXIC_SPIKES) {
            moveScore -= 1;
        }
        break;
    case MOVE_EFFECT_STICKY_WEB:
        if (ai->attackerTurnsOnField == 0) {
            moveScore += 9;
        } else {
            moveScore += 6;
        }
        if (BattleRand(bsys) % 4 != 0) {
            moveScore += 3;
        }
        break;
    case MOVE_EFFECT_SURVIVE_WITH_1_HP: // endure
    {
        BOOL monDiesEndTurnEndure = MonDiesFromResidualDamage(ctx, ai->attacker, ai->attackerMon.condition, (ctx->battlemon[ai->attacker].effect_of_moves & MOVE_EFFECT_FLAG_LEECH_SEED));

        if (IsMonInflictedWithAnyNegativeStatus(ctx, attacker)) {
            moveScore -= 1;
        }
        if (ai->playerMovesFirst && ai->playerCanOneShotMonWithAnyMove) {
            moveScore += 6;
            if (IS_ITEM_BERRY(ai->attackerMon.item)) {
                moveScore += 2;
            }
        }
        if (ctx->protectSuccessTurns[ai->attacker] == 1) {
            if (BattleRand(bsys) % 2 == 0) {
                moveScore -= NEVER_USE_MOVE_20;
            }
        }
        if (ctx->protectSuccessTurns[ai->attacker] > 1 || monDiesEndTurnEndure) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        break;
    }
    // case MOVE_EFFECT_KINGS_SHIELD:
    // case MOVE_EFFECT_OBSTRUCT:
    // case MOVE_EFFECT_SPIKEY_SHIELD:
    case MOVE_EFFECT_PROTECT: // TODO
    {
        BOOL monDiesEndTurn = MonDiesFromResidualDamage(ctx, ai->attacker, ai->attackerMon.condition, (ctx->battlemon[ai->attacker].effect_of_moves & MOVE_EFFECT_FLAG_LEECH_SEED));
        moveScore += 6;
        if (IsMonInflictedWithAnyNegativeStatus(ctx, attacker)) {
            moveScore -= 1;
        }
        if (IsMonInflictedWithAnyNegativeStatus(ctx, ai->defender)) {
            moveScore += 1;
        }
        if (ai->attackerTurnsOnField == 0 && ai->isDoubleBattle) {
            moveScore -= 1;
        }
        if (ai->attackerTurnsOnField == 0 && BattlerKnowsMoveWithEffect(bsys, ai->attacker, MOVE_EFFECT_PROTECT, ai) 
            && ai->attackerMon.item == ITEM_TOXIC_ORB && ai->attackerMon.ability == ABILITY_POISON_HEAL) {
            moveScore += 1;
        }
        if (ctx->protectSuccessTurns[ai->attacker] == 1) {
            if (BattleRand(bsys) % 2 == 0) {
                moveScore -= NEVER_USE_MOVE_20;
            }
        } else if (ctx->protectSuccessTurns[ai->attacker] > 1) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        if (monDiesEndTurn || ai->defenderMon.ability == ABILITY_UNSEEN_FIST || ai->defenderMon.ability == ABILITY_PIERCING_DRILL
            || ai->attackerMon.ability == ABILITY_TRUANT) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        break;
    }
    case MOVE_EFFECT_MAKE_SHARED_MOVES_UNUSEABLE:
        if (ctx->battlemon[ai->defender].effect_of_moves & MOVE_EFFECT_FLAG_IMPRISONED) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            for (int j = 0; j < CLIENT_MAX; ++j) {
                for (int k = 0; k < CLIENT_MAX; ++k) {
                    if (ctx->battlemon[ai->attacker].move[k] == ctx->battlemon[ai->defender].move[j] && ctx->battlemon[ai->defender].move[j] != MOVE_NONE) {
                        sharesMoves = TRUE;
                        break;
                    }
                }
                if (sharesMoves == TRUE) {
                    break;
                }
            }
            if (sharesMoves == TRUE) {
                moveScore += 9;
            }
        }
        break;
    case MOVE_EFFECT_PASS_STATS_AND_STATUS:
        if (ai->livingMembersAttacker > (1 + ai->isDoubleBattle)) {
            if ((ai->attackerPositiveStatChangesSum >= 1) || ctx->battlemon[ai->attacker].condition2 & STATUS2_SUBSTITUTE) {
                moveScore += 14;
            }
        }
        if (ai->livingMembersAttacker == 1) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_DOUBLE_SPEED_3_TURNS:
        if (IsBattleMonSlowerThanOpposition(bsys, attacker, ai->isDoubleBattle)
            || (ai->isDoubleBattle && ctx->battlemon[BATTLER_ALLY(attacker)].hp > 0 && IsBattleMonSlowerThanOpposition(bsys, BATTLER_ALLY(attacker), ai->isDoubleBattle))) { // or partner is slower
            moveScore += 9;
        } else {
            moveScore += 5;
        }
        if (ctx->tailwindCount[ai->attackerSide])
        {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_TRICK_ROOM:
        if (IsBattleMonSlowerThanOpposition(bsys, attacker, ai->isDoubleBattle)
            || (ai->isDoubleBattle && ctx->battlemon[BATTLER_ALLY(attacker)].hp > 0 && IsBattleMonSlowerThanOpposition(bsys, BATTLER_ALLY(attacker), ai->isDoubleBattle))) { // or partner is slower
            moveScore += 10;
        } else {
            moveScore += 5;
        }

        if (ctx->field_condition & FIELD_CONDITION_TRICK_ROOM) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_APPLY_TERRAINS:
        if (ai->attackerMon.item == ITEM_TERRAIN_EXTENDER) {
            moveScore += 9;
        } else {
            moveScore += 8;
        }
        break;
    case MOVE_EFFECT_SET_REFLECT:
        if (ctx->side_condition[ai->attackerSide] & SIDE_STATUS_REFLECT) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
            if (ai->defenderHasAtleastOnePhysicalMove) {
                if (ai->attackerMon.item == ITEM_LIGHT_CLAY) {
                    moveScore += 1;
                }
                if (BattleRand(bsys) % 2 == 0) {
                    moveScore += 1;
                }
            }
        }
        break;
    case MOVE_EFFECT_SET_LIGHT_SCREEN:
        if (ctx->side_condition[ai->attackerSide] & SIDE_STATUS_LIGHT_SCREEN) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
            if (ai->defenderHasAtleastOneSpecialMove) {
                if (ai->attackerMon.item == ITEM_LIGHT_CLAY) {
                    moveScore += 1;
                }
                if (BattleRand(bsys) % 2 == 0) {
                    moveScore += 1;
                }
            }
        }
        break;
    case MOVE_EFFECT_SET_AURORA_VEIL:
        if (ctx->side_condition[ai->attackerSide] & SIDE_STATUS_AURORA_VEIL || (ctx->field_condition & FIELD_CONDITION_SNOW_ALL) == 0) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
            if (ai->attackerMon.item == ITEM_LIGHT_CLAY) {
                moveScore += 1;
            }
            if (BattleRand(bsys) % 2 == 0) {
                moveScore += 2;
            }
        }
        break;
    //case MOVE_EFFECT_BOOST_ALLY_POWER_BY_50_PERCENT: 
    case MOVE_EFFECT_MAKE_GLOBAL_TARGET:// TODO
        if (ai->isDoubleBattle && ai->isAllyAlive) {
            moveScore += 6;
            if (BattleRand(bsys) % 10 < 2) {
                moveScore += 2;
            }
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_SHED_TAIL:
    case MOVE_EFFECT_SET_SUBSTITUTE:
        moveScore += 6;
        if (ctx->battlemon[ai->attacker].condition2 & STATUS2_SUBSTITUTE) {
            moveScore -= IMPOSSIBLE_MOVE;
        }
        if (ai->aiMovesFirst && (ctx->battlemon[ai->defender].effect_of_moves & MOVE_EFFECT_FLAG_LEECH_SEED)) {
            moveScore += 2;
        }
        if (ai->defenderMon.condition & STATUS_SLEEP) {
            moveScore += 2;
        }
        if (BattleRand(bsys) % 2 == 0) {
            moveScore -= 1;
        }
        if (ai->defenderMon.ability == ABILITY_INFILTRATOR || ai->attackerMon.percenthp < 50) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        if (ai->defenderHasAtleastOneUsefulSoundMove && (ai->defenderMon.hasMoldBreaker || ai->attackerMon.ability != ABILITY_SOUNDPROOF)) {
            moveScore -= 8;
        }
        break;
    case MOVE_EFFECT_FAINT_AND_ATK_SP_ATK_DOWN_2:
        moveScore += 6;
        if (ai->attackerMon.percenthp < 10) {
            moveScore += 10;
        } else if (ai->attackerMon.percenthp < 33) {
            if (BattleRand(bsys) % 10 < 7) { // 70%
                moveScore += 8;
            }
        } else if (ai->attackerMon.percenthp < 66) {
            if (BattleRand(bsys) % 2) { // 50%
                moveScore += 7;
            }
        } else {
            if (BattleRand(bsys) % 20 == 0) { // 5%
                moveScore += 7;
            }
        }

        if (ai->livingMembersAttacker == 1) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_PARALYZE_HIT:
        if (ctx->moveTbl[ai->attackerMove].secondaryEffectChance < 100 || ai->effectivenessOnPlayer[i] == TYPE_MUL_NO_EFFECT /*has immunity*/) { // nuzzle
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_STATUS_PARALYZE:
        if (ai->playerMovesFirst || // and slower after para
            (BattlerKnowsMove(bsys, attacker, MOVE_HEX, ai) == TRUE) || (BattlerKnowsFlinchingMove(bsys, attacker, ai) == TRUE) || ctx->battlemon[ai->defender].condition2 & STATUS2_ATTRACT || ctx->battlemon[ai->defender].condition2 & STATUS2_CONFUSION) {
            moveScore += 8;
        } else {
            moveScore += 7;
        }
        if (BattleRand(bsys) % 2) { // 50%
            moveScore -= 1;
        }
        break;
    case MOVE_EFFECT_STATUS_BURN:
        moveScore += 6;
        if (BattleRand(bsys) % 3 == 0) {
            if ((BattlerKnowsMove(bsys, attacker, MOVE_HEX, ai) == TRUE)
                || (ai->isDoubleBattle && BattlerKnowsMove(bsys, BATTLER_ALLY(attacker), MOVE_HEX, ai))) { // or partner
                moveScore += 1;
            }
            if (ai->defenderHasAtleastOnePhysicalMove) {
                moveScore += 1;
            }
        }
        break;
    case MOVE_EFFECT_STATUS_SLEEP:
#ifdef SKILL_SWAP_SCORING
        if (ai->attackerMon.ability == ABILITY_PRANKSTER) {
            moveScore += 2;
        }
        FALLTHROUGH;
#endif // SKILL_SWAP_SCORING
    case MOVE_EFFECT_STATUS_SLEEP_NEXT_TURN:
        moveScore += 6;
        if (!ai->monCanOneShotPlayerWithAnyMove && (BattleRand(bsys) % 4 == 0)) // no kill
        {
            if (!ai->defenderImmuneToSleep) {
                moveScore += 1;
            }
            if (((BattlerKnowsMove(bsys, attacker, MOVE_DREAM_EATER, ai) == TRUE) || (BattlerKnowsMove(bsys, attacker, MOVE_NIGHTMARE, ai) == TRUE))
                && (BattlerKnowsMove(bsys, ai->defender, MOVE_SNORE, ai) == FALSE) && (BattlerKnowsMove(bsys, ai->defender, MOVE_SLEEP_TALK, ai) == FALSE)) {
                moveScore += 1;
            }
            if ((BattlerKnowsMove(bsys, attacker, MOVE_HEX, ai) == TRUE)
                || (ai->isDoubleBattle && BattlerKnowsMove(bsys, BATTLER_ALLY(attacker), MOVE_HEX, ai))) // or partner
            {
                moveScore += 1;
            }
        }
        break;
    case MOVE_EFFECT_STATUS_POISON:
    case MOVE_EFFECT_STATUS_BADLY_POISON:
        moveScore += 6;
        if (!ai->monCanOneShotPlayerWithAnyMove && (BattleRand(bsys) % 3 == 0)) // no kill
        {
            if (!ai->defenderImmuneToPoison && ai->defenderMon.percenthp > 20) {
                if (ai->maxDamageReceived < ai->attackerMon.hp / 2 && (BattlerKnowsMove(bsys, attacker, MOVE_HEX, ai) || BattlerKnowsMove(bsys, attacker, MOVE_VENOM_DRENCH, ai) || BattlerKnowsMove(bsys, attacker, MOVE_VENOSHOCK, ai) || ai->attackerMon.ability == ABILITY_MERCILESS)) {
                    moveScore += 2;
                }
            }
        }
        break;
    case MOVE_EFFECT_SWITCH_HELD_ITEMS:
        if ((ai->attackerMon.item == ITEM_TOXIC_ORB && !ai->defenderImmuneToPoison) || (ai->attackerMon.item == ITEM_FLAME_ORB && !ai->defenderImmuneToBurn) || (ai->attackerMon.item == ITEM_BLACK_SLUDGE && !HasType(ctx, ai->defender, TYPE_POISON))) {
            moveScore += 6;
            if (BattleRand(bsys) % 2) { // 50%
                moveScore += 1;
            }
        } else if (ai->attackerMon.item == ITEM_IRON_BALL || ai->attackerMon.item == ITEM_LAGGING_TAIL || ai->attackerMon.item == ITEM_STICKY_BARB) {
            moveScore += 7;
        }
        break;
    case MOVE_EFFECT_SP_ATK_UP_CAUSE_CONFUSION:
    case MOVE_EFFECT_ATK_UP_2_STATUS_CONFUSION:
    case MOVE_EFFECT_STATUS_CONFUSE:
        if ((ctx->battlemon[ai->defender].condition2 & STATUS2_CONFUSION)
            || (ctx->side_condition[ai->defenderSide] & SIDE_STATUS_SAFEGUARD)
            || (!ai->attackerMon.hasMoldBreaker && ai->defenderMon.ability == ABILITY_OWN_TEMPO)) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
            if (ai->defenderMon.condition & STATUS_PARALYSIS || ctx->battlemon[ai->defender].condition2 & STATUS2_ATTRACT) {
                moveScore += 2;
            }
        }
        break;
    case MOVE_EFFECT_INFATUATE:
        if (ctx->battlemon[ai->defender].condition2 & STATUS2_ATTRACT
            || (!ai->attackerMon.hasMoldBreaker && ai->defenderMon.ability == ABILITY_OBLIVIOUS)
#ifndef ATTRACT_WORK_ON_ALL_SEXES
            || ai->attackerMon.sex != POKEMON_GENDER_UNKNOWN
            || ai->attackerMon.sex == ai->defenderMon.sex
#endif // !ATTRACT_WORK_ON_ALL_SEXES
        )

        {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
            if (ai->defenderMon.condition & STATUS_PARALYSIS || ctx->battlemon[ai->defender].condition2 & STATUS2_CONFUSION) {
                moveScore += 2;
            }
        }
        break;
    case MOVE_EFFECT_STATUS_LEECH_SEED:
        if ((ctx->battlemon[ai->defender].effect_of_moves & MOVE_EFFECT_FLAG_LEECH_SEED) || HasType(ctx, ai->defender, TYPE_GRASS) || ai->defenderMon.ability == ABILITY_MAGIC_GUARD) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
            if (ai->playerCanOneShotMonWithAnyMove == FALSE && ai->defenderMon.percenthp > 20) {
                moveScore += 1;
                if (BattleRand(bsys) % 2) { // 50%
                    moveScore += 1;
                }
            }
            if (ctx->battlemon[ai->defender].condition2 & STATUS2_ATTRACT || ctx->battlemon[ai->defender].condition2 & STATUS2_CONFUSION) {
                moveScore += 1;
            }
            if (ai->defenderMon.condition & (STATUS_POISON_ALL | STATUS_BURN | STATUS_PARALYSIS)) {
                moveScore += 1;
            }
        }
        break;
    case MOVE_EFFECT_USE_RANDOM_LEARNED_MOVE_SLEEP:
    case MOVE_EFFECT_DAMAGE_WHILE_ASLEEP:
        if (ctx->battlemon[attacker].condition & STATUS_SLEEP) {
            moveScore += 6;
            if (ai->attackerLastUsedMove == MOVE_REST) {
                moveScore += 10;
            }
            if (BattleRand(bsys) % 2 == 0) {
                moveScore += 1;
            }
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_CURSE:
        if (!HasType(ctx, attacker, TYPE_GHOST)) {
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_KO_MON_THAT_DEFEATED_USER:
        if (ai->playerCanOneShotMonWithAnyMove && ai->aiMovesFirst) {
            moveScore += 6;
            if (BattleRand(bsys) % 5 == 0) {
                moveScore += 1;
            }
        } else if (ai->playerMovesFirst) {
            moveScore += 5;
            if (BattleRand(bsys) % 2) {
                moveScore += 1;
            }
        }
        break;
    case MOVE_EFFECT_ION_DELUGE:
        if (ai->isDoubleBattle)
        {   
            if (HasType(ctx, ai->defender, TYPE_GROUND) 
                || ai->defenderMon.ability == ABILITY_VOLT_ABSORB) {
                moveScore -= NEVER_USE_MOVE_20;
            } else {
                moveScore += 6;
            }
        }
        break;
    case MOVE_EFFECT_CHANGE_TO_WATER_TYPE:
        if (HasType(ctx, ai->defender, TYPE_WATER)) {
            moveScore -= NEVER_USE_MOVE_20;
        } else {
            moveScore += 6;
        }
        break;
    case MOVE_EFFECT_ATK_DOWN:
    case MOVE_EFFECT_ATK_DOWN_2:
    case MOVE_EFFECT_ATK_DOWN_3:
    case MOVE_EFFECT_ATK_DEF_DOWN:
        if (ai->defenderHasAtleastOnePhysicalMove)
        {
            moveScore += 5;
            if (2 * ai->maxDamageReceived < ai->attackerMon.hp && 2 * ai->attackerRolledMaxDamage < ai->defenderMon.hp)
            {
                if (!ai->defenderImmuneToStatDrop)
                {
                    if (ai->attackerMon.hasMoldBreaker || ai->defenderMon.ability != ABILITY_HYPER_CUTTER)
                    {
                        moveScore += 1;
                    }
                }
                if (ai->playerMovesFirst)
                {
                    moveScore -= 1;
                }
            }
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_SP_ATK_DOWN:
    case MOVE_EFFECT_SP_ATK_DOWN_2:
    case MOVE_EFFECT_SP_ATK_DOWN_3:
    case MOVE_EFFECT_SP_ATK_DOWN_2_OPPOSITE_GENDER:
    case MOVE_EFFECT_ATK_SP_ATK_DOWN:
        if (ai->defenderHasAtleastOneSpecialMove) {
            moveScore += 5;
            if (2 * ai->maxDamageReceived < ai->attackerMon.hp && 2 * ai->attackerRolledMaxDamage < ai->defenderMon.hp)
            {
                if (!ai->defenderImmuneToStatDrop) {
                    if (ai->defenderMon.ability != ABILITY_BULLETPROOF || ai->attackerMon.hasMoldBreaker)
                    {
                        moveScore += 1;
                    }
                }
                if (ai->playerMovesFirst)
                {
                    moveScore -= 1;
                }
            }
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_DEF_DOWN:
    case MOVE_EFFECT_DEF_DOWN_2:
    case MOVE_EFFECT_DEF_DOWN_3:
        if (2 * ai->maxDamageReceived < ai->attackerMon.hp && 2 * ai->attackerRolledMaxDamage < ai->defenderMon.hp) {
            if (ai->attackerMon.hasMoldBreaker || (ai->defenderMon.ability != ABILITY_CLEAR_BODY && ai->defenderMon.ability != ABILITY_WHITE_SMOKE && ai->defenderMon.ability != ABILITY_BIG_PECKS && ai->defenderMon.ability != ABILITY_CONTRARY)) {
                moveScore += 7;
            }
        }
        break;
    case MOVE_EFFECT_SP_DEF_DOWN:
    case MOVE_EFFECT_SP_DEF_DOWN_2:
    case MOVE_EFFECT_SP_DEF_DOWN_3:
        if (2 * ai->maxDamageReceived < ai->attackerMon.hp && 2 * ai->attackerRolledMaxDamage < ai->defenderMon.hp) {
            if (!ai->defenderImmuneToStatDrop) {
                moveScore += 7;
            }
        }
        break;
    case MOVE_EFFECT_EVA_DOWN:
    case MOVE_EFFECT_EVA_DOWN_2:
    case MOVE_EFFECT_EVA_DOWN_3:
    case MOVE_EFFECT_ACC_DOWN:
    case MOVE_EFFECT_ACC_DOWN_2:
    case MOVE_EFFECT_ACC_DOWN_3:
        moveScore += 5;
        if (!ai->defenderImmuneToStatDrop) {
            if (ai->attackerMon.hasMoldBreaker || ai->defenderMon.ability != ABILITY_KEEN_EYE) {
                moveScore += 1;
            }
        }
        if (ai->playerMovesFirst) {
            moveScore -= 1;
        }
        break;
    case MOVE_EFFECT_VENOM_DRENCH:
        if (ai->defenderMon.condition & STATUS_POISON_ALL) {
            if ((ai->defenderHasAtleastOnePhysicalMove && ctx->battlemon[ai->defender].states[STAT_ATTACK] >= 6) || (ai->defenderHasAtleastOneSpecialMove && ctx->battlemon[ai->defender].states[STAT_SPECIAL_ATTACK] >= 6)) {
                moveScore += 5;
                if (!ai->defenderImmuneToStatDrop) {
                    moveScore += 1;
                }
                if (ai->playerMovesFirst) {
                    moveScore -= 1;
                }
            } else {
                moveScore -= NEVER_USE_MOVE_20;
            }
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_METAL_BURST:
        moveScore += 6;
        if (ai->attackerHasSturdyOrFocusSashActive) {
            moveScore += 2;
        }
        if (ai->aiMovesFirst && BattleRand(bsys) % 4 == 0) {
            moveScore -= 1;
        }
        if (ai->defenderHasAtleastOneStatusMove && BattleRand(bsys) % 4 == 0) {
            moveScore -= 1;
        }
        break;
    case MOVE_EFFECT_MIRROR_COAT:
    case MOVE_EFFECT_COUNTER:
        moveScore += 6;
        if (ai->playerCanOneShotMonWithAnyMove && !ai->attackerHasSturdyOrFocusSashActive) {
            moveScore -= NEVER_USE_MOVE_20;
        }
        
        if (ai->attackerMoveEffect == MOVE_EFFECT_COUNTER  && !ai->defenderHasAtleastOneSpecialMove) {
            moveScore += 2;
            if (ai->attackerHasSturdyOrFocusSashActive) {
                moveScore += 2;
            }
        }

        if (ai->attackerMoveEffect == MOVE_EFFECT_MIRROR_COAT && !ai->defenderHasAtleastOnePhysicalMove) {
            moveScore += 2;
            if (ai->attackerHasSturdyOrFocusSashActive) {
                moveScore += 2;
            }
        }

        if (ai->aiMovesFirst && BattleRand(bsys) % 4 == 0) {
            moveScore -= 1;
        }
        if (ai->defenderHasAtleastOneStatusMove && BattleRand(bsys) % 4 == 0) {
            moveScore -= 1;
        }
        break;
    case MOVE_EFFECT_FORCE_SWITCH:
        if (ai->livingMembersDefender > 1) {
            moveScore += 6;
            if (BattleRand(bsys) % 2 == 0) {
                moveScore += 1;
            }
        } else {
            moveScore -= NEVER_USE_MOVE_20;
        }
        break;
    case MOVE_EFFECT_ENCORE:
        if (ai->defenderTurnsOnField == 0
            || ctx->battlemon[ai->defender].moveeffect.encoredTurns > 0
            || ctx->battlemon[ai->defender].moveeffect.moveNoChoice != MOVE_NONE)
        {
            moveScore -= NEVER_USE_MOVE_20;
        }
        else
        {
            BOOL isEncouraged = IsUsedMoveEncouragedToEncore(bsys, ai->defenderLastUsedMove, ai->defenderLastUsedMoveEffect);
            if (ai->attackerMon.species == SPECIES_WOBBUFFET && !isEncouraged) {
                moveScore += 7;
            }
            if (ai->attackerMon.species != SPECIES_WOBBUFFET) {
                if (ai->aiMovesFirst && isEncouraged) {
                    moveScore += 7;
                } else {
                    moveScore += 5;
                    if (BattleRand(bsys) % 2 == 0) {
                        moveScore += 1;
                    }
                }
            }
        }
    default:
        break;
    }

    return moveScore;
}

u32 LONG_CALL GetRecoverAmountPercent(struct BattleSystem *bsys, u32 attackerMove, u32 attackerMoveEffect)
{
    struct BattleStruct *ctx = bsys->sp;
    u32 recoverAmountPercent = 50;
    switch (attackerMoveEffect) {
    case MOVE_EFFECT_RECOVER_HEALTH_AND_SLEEP:
        recoverAmountPercent = 100;
        break;
    case MOVE_EFFECT_HEAL_HALF_DIFFERENT_IN_WEATHER:
        if ((attackerMove == MOVE_SHORE_UP && (ctx->field_condition & FIELD_CONDITION_SANDSTORM_ALL))
            || (attackerMove != MOVE_SHORE_UP && (ctx->field_condition & FIELD_CONDITION_SUN_ALL))) {
            recoverAmountPercent = 67;
        } else if (ctx->field_condition & FIELD_CONDITION_RAIN_ALL) {
            recoverAmountPercent = 25;
        }
        break;
    default:
        break;
    }
    return recoverAmountPercent;
}

BOOL LONG_CALL shouldRecover(struct BattleSystem *bsys, u32 attacker UNUSED, u32 attackerMoveEffect, struct AIContext *ai)
{
    u32 recoverAmountPercent = GetRecoverAmountPercent(bsys, ai->attackerMove, attackerMoveEffect);

    u32 recoverAmountHP = recoverAmountPercent * ai->attackerMon.maxhp / 100;
    if ((recoverAmountHP + ai->attackerMon.hp) > ai->attackerMon.maxhp) {
        recoverAmountHP = ai->attackerMon.maxhp - ai->attackerMon.hp; // prevent overheal
    }

    if (ai->attackerMon.condition & STATUS_BAD_POISON && attackerMoveEffect != MOVE_EFFECT_RECOVER_HEALTH_AND_SLEEP) {
        return FALSE;
    }
    if (ai->maxDamageReceived > recoverAmountHP) {
        return FALSE;
    }
    if (ai->aiMovesFirst) {
        if (ai->playerCanOneShotMonWithAnyMove && ai->maxDamageReceived < (ai->attackerMon.hp + recoverAmountHP)) { // cannot kill after heal
            return TRUE;
        }
        if (!ai->playerCanOneShotMonWithAnyMove) {
            if (ai->attackerMon.percenthp < 40) {
                return TRUE;
            } else if (ai->attackerMon.percenthp < 66) {
                return FALSE + (BattleRand(bsys) % 2);
            }
        }
    } else {
        if (ai->attackerMon.percenthp < 50) {
            return TRUE;
        } else if (ai->attackerMon.percenthp < 70 && (BattleRand(bsys) % 4) != 0) { // 75%
            return TRUE;
        }
    }

    return FALSE;
}

int LONG_CALL RecoveryScoring(struct BattleSystem *bsys, u32 attacker, int i, struct AIContext *ai)
{
    int moveScore = 0;
    struct BattleStruct *ctx = bsys->sp;
    BOOL isHealingMove = TRUE;

    if (ctx->battlemon[attacker].moveeffect.healBlockTurns) {
        return 0;
    }

    ai->attackerMove = ctx->battlemon[attacker].move[i];
    ai->attackerMoveEffect = ctx->moveTbl[ai->attackerMove].effect;

    BOOL aiShouldRecover = shouldRecover(bsys, attacker, ai->attackerMoveEffect, ai);
    if (ai->attackerMon.percenthp >= 85) {
        moveScore -= 6;
    }

    switch (ai->attackerMoveEffect) {
    case MOVE_EFFECT_STRENGTH_SAP: //TODO
        if (aiShouldRecover) {
            moveScore += 7;
        } else {
            moveScore += 5;
        }
        break;
    case MOVE_EFFECT_HEAL_IN_3_TURNS:
        if (ctx->fcc.future_prediction_count[ai->attacker] != 0) {
            moveScore -= IMPOSSIBLE_MOVE;
            break;
        }
        FALLTHROUGH;
    case MOVE_EFFECT_HEAL_HALF_REMOVE_FLYING_TYPE:
    case MOVE_EFFECT_RESTORE_HALF_HP:
        if (aiShouldRecover) {
            moveScore += 7;
        } else {
            moveScore += 5;
        }
        break;
    case MOVE_EFFECT_HEAL_HALF_DIFFERENT_IN_WEATHER: {
        u32 recoverAmount = GetRecoverAmountPercent(bsys, ai->attackerMove, MOVE_EFFECT_HEAL_HALF_DIFFERENT_IN_WEATHER);
        if (aiShouldRecover && recoverAmount > 50) {
            moveScore += 7;
        } else if (recoverAmount == 50 && shouldRecover(bsys, attacker, MOVE_EFFECT_RESTORE_HALF_HP, ai)) {
            moveScore += 7;
        } else {
            moveScore += 5;
        }
        break;
    }
    case MOVE_EFFECT_RECOVER_HEALTH_AND_SLEEP:
        if (aiShouldRecover) {
            if (ai->attackerMon.item == ITEM_CHESTO_BERRY || ai->attackerMon.item == ITEM_LUM_BERRY
                || ai->attackerMon.ability == ABILITY_EARLY_BIRD || ai->attackerMon.ability == ABILITY_SHED_SKIN
                || (ai->isDoubleBattle && ai->aimonAlly.ability == ABILITY_HEALER)
                || BattlerKnowsMove(bsys, attacker, MOVE_SLEEP_TALK, ai) || BattlerKnowsMove(bsys, attacker, MOVE_SNORE, ai)
                || (ai->attackerMon.ability == ABILITY_HYDRATION && (ctx->field_condition & FIELD_CONDITION_RAIN_ALL))) {
                moveScore += 8;
            } else {
                moveScore += 7;
            }
        } else {
            moveScore += 5;
        }
        break;
    default:
        isHealingMove = FALSE;
        break;
    }

    if (!isHealingMove) {
        moveScore = 0;
    } else if (ai->attackerMon.percenthp == 100) {
        moveScore = 0 - NEVER_USE_MOVE_20;
    }

    return moveScore;
}



BOOL LONG_CALL HasMovePriority(struct BattleSystem *bsys, u8 attacker, u32 attackerMove, u32 attackerAbility, u8 defender)
{
    struct BattleStruct *ctx = bsys->sp;
    struct BattleMove attackerMoveStruct = ctx->moveTbl[attackerMove];
    BOOL hasPriority = FALSE;
    if (attackerMove == MOVE_GRASSY_GLIDE 
        && ctx->terrainOverlay.type == GRASSY_TERRAIN 
        && ctx->terrainOverlay.numberOfTurnsLeft > 0)
    {
        hasPriority = TRUE;
    } 
    else if (HasMovePranksterPriority(bsys, attacker, attackerMove, attackerAbility, defender))
    {
        hasPriority = TRUE;
    } 
    else if (attackerMoveStruct.priority > 0)
    {
        hasPriority = TRUE;
    }
    return hasPriority;
}
