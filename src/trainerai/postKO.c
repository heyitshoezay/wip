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


u32 calcPlayerAsDefender(struct BattleSystem *bsys, struct BattleStruct *ctx, struct PartyPokemon *mon, int defender, int attacker, struct AI_sDamageCalc *attackerMon, struct AI_sDamageCalc *defenderMon, u32 monDealsRolledDamage[6], u32 partySlot)
{
    u32 monHighestDamageMoveno = 0;
    u8 critical = 0;

    for (u8 j = 0; j < CLIENT_MAX; ++j) {
        struct AI_damage damages = { 0 };
        u32 moveno = GetMonData(mon, MON_DATA_MOVE1 + j, NULL);
        u8 pp = GetMonData(mon, MON_DATA_MOVE1PP + j, NULL);
        if (moveno != MOVE_NONE && pp) {
            struct BattleMove attackerMove = ctx->moveTbl[moveno];

            if (attackerMove.split != SPLIT_STATUS && attackerMove.power) {
                damages.damageRoll = BattleAI_CalcDamage(bsys, ctx, moveno, ctx->side_condition[BATTLER_IS_ENEMY(attacker)], ctx->field_condition, attackerMove.power, attackerMove.type, critical, attacker, defender, &damages, attackerMon, defenderMon);
                damages.damageRoll = damages.damageRange[15]; // max Damage

                damages.damageRoll = BattleAI_AdjustUnusualMoveDamage(attackerMon, defenderMon, damages.damageRoll, attackerMove.effect, moveno, damages.moveEffectiveness);
                for (int u = 0; u < 16; u++) {
                    damages.damageRange[u] = BattleAI_AdjustUnusualMoveDamage(attackerMon, defenderMon, damages.damageRange[u], attackerMove.effect, moveno, damages.moveEffectiveness);
                }

                if (damages.damageRoll > monDealsRolledDamage[partySlot]) {
                    monHighestDamageMoveno = moveno;
                    monDealsRolledDamage[partySlot] = damages.damageRoll;
                }
            }
            debug_printf("Dealing with move %d: %3d deals [%4d-%4d], roll %4d > def.HP %d\n", j, moveno, damages.damageRange[0], damages.damageRange[15], damages.damageRoll, defenderMon->hp);
        }
    }

    return monHighestDamageMoveno;
}

u32 calcPlayerAsAttacker(struct BattleSystem *bsys, struct BattleStruct *ctx, int defender, int attacker, struct AI_sDamageCalc *attackerMon, struct AI_sDamageCalc *defenderMon, u32 monReceivesDamage[6], u32 partySlot)
{
    u32 monReceivingHighestDamageMoveno = 0;
    u8 critical = 0;

    for (int k = 0; k < GetBattlerLearnedMoveCount(bsys, ctx, defender); ++k) {
        struct AI_damage damages = { 0 };
        u32 defenderMoveno = ctx->battlemon[defender].move[k];
        struct BattleMove defenderMove = ctx->moveTbl[defenderMoveno];

        if (defenderMove.split != SPLIT_STATUS && defenderMove.power && ctx->battlemon[defender].pp[k]) {
            damages.damageRoll = BattleAI_CalcDamage(bsys, ctx, defenderMoveno, ctx->side_condition[BATTLER_IS_ENEMY(defender)], ctx->field_condition, defenderMove.power, defenderMove.type, critical, defender, attacker, &damages, defenderMon, attackerMon);
            damages.damageRoll = damages.damageRange[15]; // max Damage

            damages.damageRoll = BattleAI_AdjustUnusualMoveDamage(defenderMon, attackerMon, damages.damageRoll, defenderMove.effect, defenderMoveno, damages.moveEffectiveness);
            for (int u = 0; u < 16; u++) {
                damages.damageRange[u] = BattleAI_AdjustUnusualMoveDamage(defenderMon, attackerMon, damages.damageRange[u], defenderMove.effect, defenderMoveno, damages.moveEffectiveness);
            }

            if (damages.damageRoll > monReceivesDamage[partySlot]) {
                monReceivingHighestDamageMoveno = defenderMoveno;
                monReceivesDamage[partySlot] = damages.damageRoll;
            }
        }
        debug_printf("Receiving from move %d: %3d is [%4d-%4d], roll %4d > att.HP %d\n", k, defenderMoveno, damages.damageRange[0], damages.damageRange[15], damages.damageRoll, attackerMon->hp);
    }

    return monReceivingHighestDamageMoveno;
}

s16 getNormalScore(int speedCalc, u8 aiMonCanOneshotPlayer, u8 playerCanOneShotAiMon, u32 partyMonPercentDamageDealt, u32 partyMonPercentDamageReceived)
{
    s16 score = 0;
    if (speedCalc > 0) {
        if (aiMonCanOneshotPlayer) {
            score += 5;
        } else if (partyMonPercentDamageDealt >= partyMonPercentDamageReceived) {
            score += 3;
        } else {
            score += 1;
        }
    } else {
        if (aiMonCanOneshotPlayer && !playerCanOneShotAiMon) {
            score += 4;
        } else if (partyMonPercentDamageDealt > partyMonPercentDamageReceived) {
            score += 2;
        } else if (playerCanOneShotAiMon) {
            score -= 1;
        }
    }
    return score;
}

int LONG_CALL BattleAI_PostKOSwitchIn_Internal(struct BattleSystem *bsys, int attacker, int *score, BOOL calcWithHighestDamageHit)
{
    debug_printf("BattleAI_PostKOSwitchIn_Internal %d\n", attacker);

    struct BattleStruct *ctx = bsys->sp;
    int battleType = BattleTypeGet(bsys);

    struct AI_sDamageCalc attackerMon = { 0 };
    struct AI_sDamageCalc defenderMon = { 0 };
    struct AI_sDamageCalc defenderMonAcross = { 0 };

    u8 calcOpp = TRUE;
    u8 calcAcross = FALSE;



    u8 speedCalc = 0;
    u8 speedCalcAcross = 0;
    u32 defender = BATTLER_OPPONENT(attacker); // default for singles -- updated in the doubles section
    u32 defenderAcross = BATTLER_ACROSS(attacker);
    u8 slot1 = attacker;
    u8 slot2 = slot1;

    u32 monDealsRolledDamage[6] = { 0 };
    u32 monDealsRolledDamageAcross[6] = { 0 };
    u32 monHighestDamageMoveno = 0;
    u32 monHighestDamageMovenoAcross = 0;
    u32 monReceivingHighestDamageMoveno = 0;
    u32 monReceivingHighestDamageMovenoAcross = 0;
    u32 monReceivesDamage[6] = { 0 };
    u32 monReceivesDamageAcross[6] = { 0 };
    u16 switchInScore[6] = { 0 };
    int partySize = 0;
    int picked = 6; // in Order

    debug_printf("Targeting defender in slot %d(%d) with hp %d\n", defender, ctx->battlemon[defender].species, ctx->battlemon[defender].hp);

    if (battleType & (BATTLE_TYPE_TAG | BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLE)) {
        calcAcross = TRUE;
        if (!DoesSideHave2Battlers(bsys, slot1)) { // 2vs2 or 1vs2
            slot2 = BATTLER_ALLY(attacker);
        }
        if (ctx->battlemon[defender].hp == 0 && ctx->battlemon[defenderAcross].hp == 0) {
            debug_printf("No target\n");
            return 6;
        }
        if (ctx->battlemon[defender].hp == 0)
        {
            calcOpp = FALSE;
        }
        if (ctx->battlemon[defenderAcross].hp == 0) {
            calcAcross = FALSE;
        }
    } else if (ctx->battlemon[defender].hp == 0) {
        debug_printf("No target\n");
        return 6;
    }

    if (calcOpp) {
        FillDamageStructFromBattleMon(bsys, ctx, &defenderMon, defender);
    }
    if (calcAcross) {
        FillDamageStructFromBattleMon(bsys, ctx, &defenderMonAcross, defenderAcross);
    }
    partySize = Battle_GetClientPartySize(bsys, attacker);
    for (int i = 0; i < partySize; i++) {
        struct PartyPokemon *mon = Battle_GetClientPartyMon(bsys, attacker, i);
        attackerMon.species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG, 0);
        debug_printf("Slot %d:%d hp:%d,\n", i, attackerMon.species, GetMonData(mon, MON_DATA_HP, 0));
        debug_printf("sel_m1 %d, sel_m2 %d, switchSl1 %d, switchSl1 %d\n", ctx->sel_mons_no[slot1], ctx->sel_mons_no[slot2], ctx->aiSwitchedPartySlot[slot1], ctx->aiSwitchedPartySlot[slot2]);

        if (attackerMon.species != SPECIES_NONE && attackerMon.species != SPECIES_EGG && GetMonData(mon, MON_DATA_HP, 0)
            && i != ctx->sel_mons_no[slot1]
            && i != ctx->sel_mons_no[slot2]
            && i != ctx->aiSwitchedPartySlot[slot1]
            && i != ctx->aiSwitchedPartySlot[slot2]) {
            switchInScore[i] = 100;

            FillDamageStructFromPartyMon(bsys, ctx, &attackerMon, mon, attacker, i);

            if (calcOpp) {
                debug_printf("Opponent\n");
                speedCalc = BattleAI_CalcSpeed(bsys, ctx, defender, mon, CALCSPEED_FLAG_NO_PRIORITY);
                monHighestDamageMoveno = calcPlayerAsDefender(bsys, ctx, mon, defender, attacker, &attackerMon, &defenderMon, monDealsRolledDamage, i);
                monReceivingHighestDamageMoveno = calcPlayerAsAttacker(bsys, ctx, defender, attacker, &attackerMon, &defenderMon, monReceivesDamage, i);
            }


            if (calcAcross) {
                debug_printf("Across\n");
                speedCalcAcross = BattleAI_CalcSpeed(bsys, ctx, defenderAcross, mon, CALCSPEED_FLAG_NO_PRIORITY);
                monHighestDamageMovenoAcross = calcPlayerAsDefender(bsys, ctx, mon, defenderAcross, attacker, &attackerMon, &defenderMonAcross, monDealsRolledDamageAcross, i);
                monReceivingHighestDamageMovenoAcross = calcPlayerAsAttacker(bsys, ctx, defenderAcross, attacker, &attackerMon, &defenderMonAcross, monReceivesDamageAcross, i);
            }



            u8 aiMonCanOneshotPlayer = FALSE;
            u8 playerCanOneShotAiMon = FALSE;
            u16 partyMonPercentDamageDealt = 1;
            u16 partyMonPercentDamageReceived = 1;
            // TODO stealth rocks, spikes, toxic spikes, etc...
            if (calcOpp) {
                aiMonCanOneshotPlayer = CanAttackerOneShotDefender(monDealsRolledDamage[i], ctx->moveTbl[monHighestDamageMoveno].split, monHighestDamageMoveno, &attackerMon, &defenderMon);
                playerCanOneShotAiMon = CanAttackerOneShotDefender(monReceivesDamage[i], ctx->moveTbl[monReceivingHighestDamageMoveno].split, monReceivingHighestDamageMoveno, &defenderMon, &attackerMon);
                partyMonPercentDamageDealt = (100 * monDealsRolledDamage[i] / defenderMon.hp);
                partyMonPercentDamageReceived = (100 * monReceivesDamage[i] / attackerMon.hp);
                debug_printf("SwitchScore: SpeedCalc %d. Attacker %d deals %dpct to defender %d. Receives %dpct:\n", speedCalc, attacker, (defenderMon.hp > 0 ? (100 * monDealsRolledDamage[i] / defenderMon.hp) : 0), defender, (100 * monReceivesDamage[i] / attackerMon.hp));
            }

            u8 aiMonCanOneshotPlayerAcross = FALSE;
            u8 playerCanOneShotAiMonAcross = FALSE;
            u16 partyMonPercentDamageDealtAcross = 1;
            u16 partyMonPercentDamageReceivedAcross = 1;

             if (calcAcross) {
                aiMonCanOneshotPlayerAcross = CanAttackerOneShotDefender(monDealsRolledDamageAcross[i], ctx->moveTbl[monHighestDamageMovenoAcross].split, monHighestDamageMovenoAcross, &attackerMon, &defenderMonAcross);
                playerCanOneShotAiMonAcross = CanAttackerOneShotDefender(monReceivesDamageAcross[i], ctx->moveTbl[monReceivingHighestDamageMovenoAcross].split, monReceivingHighestDamageMovenoAcross, &defenderMonAcross, &attackerMon);
                partyMonPercentDamageDealtAcross = (100 * monDealsRolledDamageAcross[i] / defenderMonAcross.hp);
                partyMonPercentDamageReceivedAcross = (100 * monReceivesDamageAcross[i] / attackerMon.hp);
                debug_printf("SwitchScore: SpeedCalc %d. Attacker %d deals %dpct to defender %d. Receives %dpct :\n", speedCalcAcross, attacker, (defenderMonAcross.hp > 0 ? (100 * monDealsRolledDamageAcross[i] / defenderMonAcross.hp) : 0), defenderAcross, (100 * monReceivesDamageAcross[i] / attackerMon.hp));
            }


            if (calcWithHighestDamageHit) {
                if (partyMonPercentDamageReceived >= 100) {
                    switchInScore[i] -= 10;
                } else if (speedCalc == 0 && (2 * partyMonPercentDamageReceived >= 100)) {
                    switchInScore[i] -= 10;
                }
                if (IS_ITEM_MEGA_STONE(attackerMon.item)) {
                    switchInScore[i] -= 10;
                }
            }

            if (calcOpp) {
                switchInScore[i] += getNormalScore(speedCalc, aiMonCanOneshotPlayer, playerCanOneShotAiMon, partyMonPercentDamageDealt, partyMonPercentDamageReceived);
            }

            if (calcAcross) {
                switchInScore[i] += getNormalScore(speedCalcAcross, aiMonCanOneshotPlayerAcross, playerCanOneShotAiMonAcross, partyMonPercentDamageDealtAcross, partyMonPercentDamageReceivedAcross);
            }

            if (!calcWithHighestDamageHit)
            {
                if (!playerCanOneShotAiMon && (attackerMon.species == SPECIES_WYNAUT || attackerMon.species == SPECIES_WOBBUFFET)) {
                    switchInScore[i] += 2;
                }

                if (speedCalc > 0 || speedCalcAcross > 0) {

                    if (attackerMon.species == SPECIES_DITTO) {
                        switchInScore[i] += 2;
                    }
                    if (attackerMon.species == SPECIES_ZOROARK) {
                        switchInScore[i] += (BattleRand(bsys) % 3);
                    }
                }
            }
            debug_printf("%d\n", switchInScore[i]);
            // default += 0;
        }
    }

    u16 currentScore = switchInScore[0];
    for (int i = 0; i < partySize; i++) {
        if (switchInScore[i] > currentScore) {
            picked = i;
            *score = switchInScore[i];
            currentScore = switchInScore[i];
        }
    }
    for (int i = 0; i < partySize; i++) {
        debug_printf("%i ", switchInScore[i]);
    }
    debug_printf("-> picked %i\n", picked);

    return picked;
}
