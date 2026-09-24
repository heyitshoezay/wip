#include "../../include/battle.h"
#include "../../include/config.h"
#include "../../include/constants/ability.h"
#include "../../include/constants/file.h"
#include "../../include/constants/hold_item_effects.h"
#include "../../include/constants/item.h"
#include "../../include/constants/move_effects.h"
#include "../../include/constants/moves.h"
#include "../../include/constants/species.h"
#include "../../include/custom/custom_ai.h"
#include "../../include/debug.h"
#include "../../include/overlay.h"
#include "../../include/pokemon.h"
#include "../../include/q412.h"
#include "../../include/types.h"

void LONG_CALL SetupStateVariables(struct BattleSystem *bsys, u32 attacker, u32 defender, struct AIContext *ai)
{
    struct BattleStruct *ctx = bsys->sp;
    u8 critical = 0;
    u8 speedCalc;

    FillDamageStructFromBattleMon(bsys, ctx, &ai->attackerMon, attacker);
    FillDamageStructFromBattleMon(bsys, ctx, &ai->defenderMon, defender);
    ai->isDoubleBattle = FALSE;
    if (BattleTypeGet(bsys) & (BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG)) {
        ai->isDoubleBattle = TRUE;
    }
    ai->isAllyAlive = FALSE;
    if (ai->isDoubleBattle && ctx->battlemon[BATTLER_ALLY(attacker)].hp) {
        ai->isAllyAlive = TRUE;
        FillDamageStructFromBattleMon(bsys, ctx, &ai->aimonAlly, BATTLER_ALLY(attacker));
    }

    ai->attacker = attacker;
    ai->defender = defender;
    ai->attackerSide = BATTLER_IS_ENEMY(ai->attacker);
    ai->defenderSide = BATTLER_IS_ENEMY(ai->defender);

    ai->attackerLastUsedMove = ctx->waza_no_old[ai->attacker];
    ai->defenderLastUsedMove = ctx->waza_no_old[ai->defender];
    ai->defenderLastUsedMoveEffect = ctx->moveTbl[ai->defenderLastUsedMove].effect;
    ai->defenderTurnsOnField = ctx->total_turn - ctx->battlemon[ai->defender].moveeffect.fakeOutCount;
    ai->attackerTurnsOnField = ctx->total_turn - ctx->battlemon[attacker].moveeffect.fakeOutCount;

    ai->playerMovesFirst = 0;
    ai->aiMovesFirst = 0;
    ai->isSpeedTie = 0;
    ai->maxDamageReceived = 0;
    ai->attackerRolledMaxDamage = 0;

    ai->partnerMoveNo = 0;
    ai->partnerClicksAttackingMove = FALSE;
    ai->ignoreTarget = FALSE;

    ai->defenderKnowsThawingMove = BattlerKnowsThawingMove(bsys, ai->defender, ai);

    ai->isPartnerGrounded = FALSE;
    if (ai->isDoubleBattle && ctx->battlemon[BATTLER_ALLY(attacker)].hp) {
        ai->isPartnerGrounded = IsClientGrounded(ctx, BATTLER_ALLY(attacker));
    }

    ai->defenderHasAtleastOneUsefulSoundMove = FALSE;
    ai->defenderCanForceSwitching = FALSE;

    ai->defenderHasSturdyOrFocusSashActive = FALSE;
    if (ai->defenderMon.hp == ai->defenderMon.maxhp) {
        if (ai->defenderMon.item == ITEM_FOCUS_SASH || (ai->defenderMon.ability == ABILITY_STURDY && !ai->attackerMon.hasMoldBreaker)) {
            ai->defenderHasSturdyOrFocusSashActive = TRUE;
        }
    }

    ai->attackerHasSturdyOrFocusSashActive = FALSE;
    if (ai->attackerMon.hp == ai->attackerMon.maxhp) {
        if (ai->attackerMon.item == ITEM_FOCUS_SASH || (ai->attackerMon.ability == ABILITY_STURDY && !ai->defenderMon.hasMoldBreaker)) {
            ai->attackerHasSturdyOrFocusSashActive = TRUE;
        }
    }

    if (ai->highestPostKoScoreFromParty == 0) {
        int score = 0;
        int highestDamageHitPrct = 0;
        ai->postKoScoringPosition = BattleAI_PostKOSwitchIn_Internal(bsys, attacker, &score, TRUE, &highestDamageHitPrct);
        ai->highestPostKoScoreFromParty = score;
        ai->highestDamageHitPrct = highestDamageHitPrct;
    }
#ifdef DEBUG_AI_SCORING
    debug_printf("PostKo: position %d with score %d and highest damage hit percent %d\n", ai->postKoScoringPosition, ai->highestPostKoScoreFromParty, ai->highestDamageHitPrct);
#endif // DEBUG_AI_SCORING

    speedCalc = CalcSpeed(bsys, ctx, defender, attacker, CALCSPEED_FLAG_NO_PRIORITY); // checks actual turn order with field state considered
    // evaluates to 0 if ai->defender > attacker (false)
    // and 1 if ai->defender < attacker (true)
    // if speed tie, then 2.

    if (speedCalc == 0) {
        ai->playerMovesFirst = 1;
    } else {
        ai->aiMovesFirst = 1;
        if (ctx->effectiveSpeed[attacker] == ctx->effectiveSpeed[defender]) {
            ai->isSpeedTie = 1;
        }
    }
    ai->attackerMon.speed = ctx->effectiveSpeed[attacker];
    ai->defenderMon.speed = ctx->effectiveSpeed[defender];
#ifdef DEBUG_AI_SCORING
    debug_printf("SpeedCalc %d, defMovesFirst %d, atkSpeed %d, defSpeed %d\n", speedCalc, ai->playerMovesFirst, ai->attackerMon.speed, ai->defenderMon.speed);
#endif
    ai->isDefenderIncapacitated = FALSE;
    if ((ai->defenderMon.condition & STATUS_SLEEP)
        || ((ai->defenderMon.condition & STATUS_FREEZE) && !ai->defenderKnowsThawingMove)
        || (ai->defenderMon.condition2 & STATUS2_RECHARGE)
        || ST_CheckIfInTruant(ctx, defender)) {
        ai->isDefenderIncapacitated = TRUE;
    }

    ai->defenderAllyHasMagicBounce = FALSE;
    if (ai->isDoubleBattle && ctx->battlemon[BATTLER_ALLY(defender)].hp 
        && ctx->battlemon[BATTLER_ALLY(defender)].ability == ABILITY_MAGIC_BOUNCE && !ai->attackerMon.hasMoldBreaker) {
        ai->defenderAllyHasMagicBounce = FALSE;
    }

    ai->defenderHasMagicBounce = FALSE;
    if (ai->defenderMon.ability == ABILITY_MAGIC_BOUNCE && !ai->attackerMon.hasMoldBreaker) {
        ai->defenderHasMagicBounce = TRUE;
    }

    BOOL isDefenderImmuneToAnyStatus = FALSE;
    if ((ai->defenderMon.condition & STATUS_ALL)
        || ai->defenderHasMagicBounce
        || (!ai->attackerMon.hasMoldBreaker
            && (ai->defenderMon.ability == ABILITY_GOOD_AS_GOLD
                || ai->defenderMon.ability == ABILITY_PURIFYING_SALT
                || (ai->defenderMon.ability == ABILITY_SHIELDS_DOWN && ai->defenderMon.percenthp > 50)
                || (ai->defenderMon.ability == ABILITY_LEAF_GUARD && ctx->field_condition & FIELD_CONDITION_SUN_ALL)))
        || (ai->defenderMon.ability == ABILITY_HYDRATION && ctx->field_condition & FIELD_CONDITION_RAIN_ALL)
        || (ai->defenderMon.ability == ABILITY_COMATOSE)
        || (ctx->side_condition[ai->defenderSide] & SIDE_STATUS_SAFEGUARD)) {
        isDefenderImmuneToAnyStatus = TRUE;
    }

    ai->defenderImmuneToPoison = FALSE;
    if (isDefenderImmuneToAnyStatus
        || ai->defenderMon.ability == ABILITY_MAGIC_GUARD || ai->defenderMon.ability == ABILITY_IMMUNITY || ai->defenderMon.ability == ABILITY_POISON_HEAL
        || (ai->defenderMon.isGrounded && ctx->terrainOverlay.type == MISTY_TERRAIN)
        || ((HasType(ctx, ai->defender, TYPE_POISON) || HasType(ctx, ai->defender, TYPE_STEEL)) && ai->attackerMon.ability != ABILITY_CORROSION)) {
        ai->defenderImmuneToPoison = TRUE;
    }

    ai->defenderImmuneToParalysis = FALSE;
    if (isDefenderImmuneToAnyStatus || ai->defenderMon.ability == ABILITY_LIMBER || (ai->defenderMon.isGrounded && ctx->terrainOverlay.type == MISTY_TERRAIN) || HasType(ctx, ai->defender, TYPE_ELECTRIC)) {
        ai->defenderImmuneToParalysis = TRUE;
    }

    ai->defenderImmuneToBurn = FALSE;
    if (isDefenderImmuneToAnyStatus
        || ai->defenderMon.ability == ABILITY_WATER_VEIL || ai->defenderMon.ability == ABILITY_THERMAL_EXCHANGE
        || ai->defenderMon.ability == ABILITY_MAGIC_GUARD || ai->defenderMon.ability == ABILITY_WATER_BUBBLE
        || (ai->defenderMon.isGrounded && ctx->terrainOverlay.type == MISTY_TERRAIN)
        || HasType(ctx, ai->defender, TYPE_FIRE)) {
        ai->defenderImmuneToBurn = TRUE;
    }

    ai->defenderImmuneToSleep = FALSE;
    if (isDefenderImmuneToAnyStatus
        || ai->defenderMon.ability == ABILITY_VITAL_SPIRIT || ai->defenderMon.ability == ABILITY_INSOMNIA
        || (ai->defenderMon.isGrounded && (ctx->terrainOverlay.type == MISTY_TERRAIN || ctx->terrainOverlay.type == ELECTRIC_TERRAIN))) {
        ai->defenderImmuneToSleep = TRUE;
    }

    ai->defenderImmuneToStatDrop = FALSE;
    if (ai->defenderMon.ability == ABILITY_FULL_METAL_BODY
        || ai->defenderMon.item_held_effect == HOLD_EFFECT_PREVENT_STAT_DROPS
        || (!ai->attackerMon.hasMoldBreaker
            && (ai->defenderMon.ability == ABILITY_CLEAR_BODY
                || ai->defenderMon.ability == ABILITY_CONTRARY
                || ai->defenderMon.ability == ABILITY_WHITE_SMOKE))) {
        ai->defenderImmuneToStatDrop = TRUE;
    }

    ai->partySizeAttacker = Battle_GetClientPartySize(bsys, attacker);
    ai->livingMembersAttacker = 0;
    ai->partySizeDefender = Battle_GetClientPartySize(bsys, ai->defender);
    ai->livingMembersDefender = 0;

    ai->monWithMegaInParty = FALSE;
    for (int i = 0; i < ai->partySizeAttacker; i++) {
        struct PartyPokemon *current = Battle_GetClientPartyMon(bsys, attacker, i);
        if (!(GetMonData(current, MON_DATA_HP, 0) == 0 || GetMonData(current, MON_DATA_SPECIES_OR_EGG, 0) == 0 || GetMonData(current, MON_DATA_SPECIES_OR_EGG, 0) == SPECIES_EGG)) {
            u32 item = GetMonData(current, MON_DATA_HELD_ITEM, 0);
            if (IS_ITEM_MEGA_STONE(item)) {
                ai->monWithMegaInParty = TRUE;
            }
            ai->livingMembersAttacker++;
        }
    }

    for (int i = 0; i < ai->partySizeDefender; i++) {
        struct PartyPokemon *current = Battle_GetClientPartyMon(bsys, ai->defender, i);
        if (!(GetMonData(current, MON_DATA_HP, 0) == 0 || GetMonData(current, MON_DATA_SPECIES_OR_EGG, 0) == 0 || GetMonData(current, MON_DATA_SPECIES_OR_EGG, 0) == SPECIES_EGG)) {
            ai->livingMembersDefender++;
        }
    }

    ai->attackerPositiveStatChangesSum = BattlerPositiveStatChangesSum(bsys, ai->attacker);

    ai->attackerMovesKnown = GetBattlerLearnedMoveCount(bsys, ctx, attacker);

    int highestDamageMoveIndex = 0;
    ai->playerCanOneShotMonWithAnyMove = FALSE;
    for (int k = 0; k < GetBattlerLearnedMoveCount(bsys, ctx, ai->defender); k++) {
        struct AI_damage damages = { 0 };
        u32 defenderMoveno = ctx->battlemon[defender].move[k];
        struct BattleMove defenderMove = ctx->moveTbl[defenderMoveno];

        if (defenderMove.split != SPLIT_STATUS && defenderMove.power && IsMoveUsable(ctx, defender, defenderMoveno, ai->defenderLastUsedMove, defenderMove.split, k)) {
            damages.damageRoll = BattleAI_CalcDamage(bsys, ctx, defenderMoveno, ctx->side_condition[BATTLER_IS_ENEMY(defender)], ctx->field_condition, defenderMove.power, defenderMove.type, critical, defender, attacker, &damages, &ai->defenderMon, &ai->attackerMon);
            damages.damageRoll = damages.damageRange[15]; // max Damage

            damages.damageRoll = BattleAI_AdjustUnusualMoveDamage(&ai->defenderMon, &ai->attackerMon, damages.damageRoll, defenderMove.effect, defenderMoveno, damages.moveEffectiveness);
            for (int u = 0; u < 16; u++) {
                damages.damageRange[u] = BattleAI_AdjustUnusualMoveDamage(&ai->defenderMon, &ai->attackerMon, damages.damageRange[u], defenderMove.effect, defenderMoveno, damages.moveEffectiveness);
            }

            BOOL playerCanOneShotAiMon = CanAttackerOneShotDefender(damages.damageRoll, defenderMove.split, defenderMoveno, &ai->defenderMon, &ai->attackerMon);
            if (playerCanOneShotAiMon) {
                ai->playerCanOneShotMonWithAnyMove = TRUE;
                ai->playerCanOneShotMonWithMove[k] = TRUE;
            }

            if (damages.damageRoll > ai->maxDamageReceived) {
                ai->maxDamageReceived = damages.damageRoll;
                highestDamageMoveIndex = k;
            }
            if (damages.moveEffectiveness >= TYPE_MUL_NORMAL) {
                if (defenderMove.split == SPLIT_SPECIAL) {
                    ai->defenderHasAtleastOneSpecialMove = 1;
                } else {
                    ai->defenderHasAtleastOnePhysicalMove = 1;
                }
            }
            if (IsMoveUsefulSoundMove(defenderMoveno) && (defenderMove.split == SPLIT_STATUS || damages.moveEffectiveness >= TYPE_MUL_NORMAL)) {
                ai->defenderHasAtleastOneUsefulSoundMove = TRUE;
            }
            if (IsMoveForceSwitching(defenderMoveno)) {
                ai->defenderCanForceSwitching = TRUE;
            }

#ifdef DEBUG_AI_SCORING
            debug_printf("Receiving from move %d: %3d is [%4d-%4d], roll %4d > att.HP %d\n", k, defenderMoveno, damages.damageRange[0], damages.damageRange[15], damages.damageRoll, ai->attackerMon.hp);
#endif  
        } else {
            ai->defenderHasAtleastOneStatusMove = TRUE;
        }
    }

#ifdef DEBUG_AI_SCORING
    debug_printf("Overall Max damage received from %i:%i is %d > %d att.HP\n", highestDamageMoveIndex, ctx->battlemon[ai->defender].move[highestDamageMoveIndex], ai->maxDamageReceived, ai->attackerMon.hp);
#endif // DEBUG_AI_SCORING

    ai->attackerHasAttackingMoves = FALSE;
    ai->monCanOneShotPlayerWithAnyMove = FALSE;
    for (int j = 0; j < ai->attackerMovesKnown; j++) {
        struct AI_damage damages = { 0 };
        u32 attackerMoveno = ctx->battlemon[attacker].move[j];
        struct BattleMove attackerMove = ctx->moveTbl[attackerMoveno];
        if (attackerMove.split == SPLIT_STATUS && IsMoveUsable(ctx, attacker, attackerMoveno, ai->attackerLastUsedMove, attackerMove.split, j)) {
            u8 movetype = GetAdjustedMoveTypeBasics(ctx, attackerMoveno, ai->attackerMon.ability, attackerMove.type);
            ai->effectivenessOnPlayer[j] = BattleAI_GetTypeEffectiveness(bsys, ctx, attackerMoveno, movetype, attacker, defender, &ai->attackerMon, &ai->defenderMon);
        } else if (attackerMove.power && IsMoveUsable(ctx, attacker, attackerMoveno, ai->attackerLastUsedMove, attackerMove.split, j)) {
            ai->attackerHasAttackingMoves = TRUE;
            damages.damageRoll = BattleAI_CalcDamage(bsys, ctx, attackerMoveno, ctx->side_condition[BATTLER_IS_ENEMY(attacker)], ctx->field_condition, attackerMove.power, attackerMove.type, critical, attacker, defender, &damages, &ai->attackerMon, &ai->defenderMon);
            ai->effectivenessOnPlayer[j] = damages.moveEffectiveness;

            damages.damageRoll = BattleAI_AdjustUnusualMoveDamage(&ai->attackerMon, &ai->defenderMon, damages.damageRoll, attackerMove.effect, attackerMoveno, ai->effectivenessOnPlayer[j]);
            for (int u = 0; u < 16; u++) {
                damages.damageRange[u] = BattleAI_AdjustUnusualMoveDamage(&ai->attackerMon, &ai->defenderMon, damages.damageRange[u], attackerMove.effect, attackerMoveno, ai->effectivenessOnPlayer[j]);
            }

            BOOL aiMonCanOneshotPlayer = CanAttackerOneShotDefender(damages.damageRoll, attackerMove.split, attackerMoveno, &ai->attackerMon, &ai->defenderMon);

            ai->attackerRolledMoveDamages[j] = damages.damageRoll;
            if (aiMonCanOneshotPlayer) {
                ai->monCanOneShotPlayerWithAnyMove = TRUE;
                ai->monCanOneShotPlayerWithMove[j] = TRUE;
                ai->attackerRolledMoveDamages[j] = ai->defenderMon.hp; // cap killing move's damage at defender HP, so that all killing moves are treated equally as "highest damage"
            }
#ifdef DEBUG_AI_SCORING
            debug_printf("Dealing with move %d: %3d (%2dpp) is [%4d-%4d], roll %4d > def.HP %d\n", j, attackerMoveno, ctx->battlemon[attacker].pp[j], damages.damageRange[0], damages.damageRange[15], damages.damageRoll, ai->defenderMon.hp);
#endif
            if (ai->attackerRolledMoveDamages[j] > ai->attackerRolledMaxDamage) {
                ai->attackerRolledMaxDamage = ai->attackerRolledMoveDamages[j];
            }

            if (ai->effectivenessOnPlayer[j] >= TYPE_MUL_NORMAL)
            {
                ai->attackerHasValidDamagingMove = TRUE;
            }
        }
        if (IsMoveValidSwitchingMove(attackerMoveno) && ai->effectivenessOnPlayer[j] > TYPE_MUL_NO_EFFECT) {
            ai->attackerHasValidSwitchingMove = TRUE;
        }
    }
}
