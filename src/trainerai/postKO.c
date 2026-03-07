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

int LONG_CALL BattleAI_PostKOSwitchIn_Internal(struct BattleSystem *bsys, int attacker, int *score, BOOL calcWithHighestDamageHit)
{
    debug_printf("BattleAI_PostKOSwitchIn_Internal %d\n", attacker);

    struct BattleStruct *ctx = bsys->sp;
    int battleType = BattleTypeGet(bsys);

    struct AI_sDamageCalc attackerMon = { 0 };
    struct AI_sDamageCalc defenderMon = { 0 };

    u8 critical = 0;

    u8 speedCalc;
    u32 defender = BATTLER_OPPONENT(attacker); // default for singles -- updated in the doubles section
    u8 slot1, slot2;
    u16 moveno = 0;
    u32 monDealsRolledDamage[6] = { 0 };
    u32 monHighestDamageMoveno = 0;
    u32 monReceivingHighestDamageMoveno = 0;
    u32 monReceivesDamage[6] = { 0 };
    u16 switchInScore[6] = { 0 };
    int partySize = 0;
    int picked = 6; // in Order

    slot1 = attacker;
    slot2 = slot1;
    debug_printf("Targeting defender in slot %d(%d) with hp %d\n", defender, ctx->battlemon[defender].species, ctx->battlemon[defender].hp);

    if (battleType & (BATTLE_TYPE_TAG | BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLE)) {
        slot2 = BATTLER_ALLY(attacker);
        if (ctx->battlemon[defender].hp == 0) {
            debug_printf("No target\n");
            return 6;
        }
    }

    FillDamageStructFromBattleMon(bsys, ctx, &defenderMon, defender);

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

            speedCalc = BattleAI_CalcSpeed(bsys, ctx, defender, mon, CALCSPEED_FLAG_NO_PRIORITY); // checks actual turn order with field state considered

            for (u8 j = 0; j < CLIENT_MAX; ++j) {
                struct AI_damage damages = { 0 };
                moveno = GetMonData(mon, MON_DATA_MOVE1 + j, NULL);
                u8 pp = GetMonData(mon, MON_DATA_MOVE1PP + j, NULL);
                if (moveno != MOVE_NONE && pp) {
                    struct BattleMove attackerMove = ctx->moveTbl[moveno];

                    if (attackerMove.split != SPLIT_STATUS && attackerMove.power) {
                        damages.damageRoll = BattleAI_CalcDamage(bsys, ctx, moveno, ctx->side_condition[BATTLER_IS_ENEMY(attacker)], ctx->field_condition, attackerMove.power, attackerMove.type, critical, attacker, defender, &damages, &attackerMon, &defenderMon);
                        damages.damageRoll = damages.damageRange[15]; // max Damage

                        damages.damageRoll = BattleAI_AdjustUnusualMoveDamage(&attackerMon, &defenderMon, damages.damageRoll, attackerMove.effect, moveno, damages.moveEffectiveness);
                        for (int u = 0; u < 16; u++) {
                            damages.damageRange[u] = BattleAI_AdjustUnusualMoveDamage(&attackerMon, &defenderMon, damages.damageRange[u], attackerMove.effect, moveno, damages.moveEffectiveness);
                        }

                        if (damages.damageRoll > monDealsRolledDamage[i]) {
                            monHighestDamageMoveno = moveno;
                            monDealsRolledDamage[i] = damages.damageRoll;
                        }
                    }
                    debug_printf("Dealing with move %d: %3d deals [%4d-%4d], roll %4d > def.HP %d\n", j, moveno, damages.damageRange[0], damages.damageRange[15], damages.damageRoll, defenderMon.hp);
                }
            }

            for (int k = 0; k < GetBattlerLearnedMoveCount(bsys, ctx, defender); ++k) {
                struct AI_damage damages = { 0 };
                u32 defenderMoveno = ctx->battlemon[defender].move[k];
                struct BattleMove defenderMove = ctx->moveTbl[defenderMoveno];

                if (defenderMove.split != SPLIT_STATUS && defenderMove.power && ctx->battlemon[defender].pp[k]) {
                    damages.damageRoll = BattleAI_CalcDamage(bsys, ctx, defenderMoveno, ctx->side_condition[BATTLER_IS_ENEMY(defender)], ctx->field_condition, defenderMove.power, defenderMove.type, critical, defender, attacker, &damages, &defenderMon, &attackerMon);
                    damages.damageRoll = damages.damageRange[15]; // max Damage

                    damages.damageRoll = BattleAI_AdjustUnusualMoveDamage(&defenderMon, &attackerMon, damages.damageRoll, defenderMove.effect, defenderMoveno, damages.moveEffectiveness);
                    for (int u = 0; u < 16; u++) {
                        damages.damageRange[u] = BattleAI_AdjustUnusualMoveDamage(&defenderMon, &attackerMon, damages.damageRange[u], defenderMove.effect, defenderMoveno, damages.moveEffectiveness);
                    }

                    if (damages.damageRoll > monReceivesDamage[i]) {
                        monReceivingHighestDamageMoveno = defenderMoveno;
                        monReceivesDamage[i] = damages.damageRoll;
                    }
                }
                debug_printf("Receiving from move %d: %3d is [%4d-%4d], roll %4d > att.HP %d\n", k, defenderMoveno, damages.damageRange[0], damages.damageRange[15], damages.damageRoll, attackerMon.hp);
            }

            // TODO stealth rocks, spikes, toxic spikes, etc...
            u8 aiMonCanOneshotPlayer = CanAttackerOneShotDefender(monDealsRolledDamage[i], ctx->moveTbl[monHighestDamageMoveno].split, monHighestDamageMoveno, &attackerMon, &defenderMon);
            u8 playerCanOneShotAiMon = CanAttackerOneShotDefender(monReceivesDamage[i], ctx->moveTbl[monReceivingHighestDamageMoveno].split, monReceivingHighestDamageMoveno, &defenderMon, &attackerMon);
            u16 partyMonPercentDamageDealt = (100 * monDealsRolledDamage[i] / defenderMon.hp);
            u16 partyMonPercentDamageReceived = (100 * monReceivesDamage[i] / attackerMon.hp);

            debug_printf("SwitchScore: SpeedCalc %d. Attacker %d deals %dpct to defender %d. Receives %dpct", speedCalc, attacker, (defenderMon.hp > 0 ? (100 * monDealsRolledDamage[i] / defenderMon.hp) : 0), defender, (100 * monReceivesDamage[i] / attackerMon.hp));
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

            if (!playerCanOneShotAiMon && (attackerMon.species == SPECIES_WYNAUT || attackerMon.species == SPECIES_WOBBUFFET)) {
                switchInScore[i] += 2;
            }

            if (speedCalc > 0) {
                if (aiMonCanOneshotPlayer) {
                    switchInScore[i] += 5;
                } else if (partyMonPercentDamageDealt >= partyMonPercentDamageReceived) {
                    switchInScore[i] += 3;
                } else {
                    switchInScore[i] += 1;
                }

                if (attackerMon.species == SPECIES_DITTO) {
                    switchInScore[i] += 2;
                }
            } else {
                if (aiMonCanOneshotPlayer && !playerCanOneShotAiMon) {
                    switchInScore[i] += 4;
                } else if (partyMonPercentDamageDealt > partyMonPercentDamageReceived) {
                    switchInScore[i] += 2;
                } else if (playerCanOneShotAiMon) {
                    switchInScore[i] -= 1;
                }
            }
            debug_printf(": %d\n", switchInScore[i]);
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
