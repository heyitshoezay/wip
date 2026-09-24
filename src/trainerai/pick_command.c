#include "../../include/types.h"
#include "../../include/config.h"
#include "../../include/battle.h"
#include "../../include/mega.h"
#include "../../include/pokemon.h"
#include "../../include/trainer_ai.h"
#include "../../include/constants/ability.h"
#include "../../include/constants/species.h"
#include "../../include/constants/battle_script_constants.h"
#include "../../include/constants/battle_message_constants.h"
#include "../../include/custom/custom_ai.h"


u8 LONG_CALL FindTargets(struct BattleStruct *ctx, u8 attacker, int targets[4], int moveScores[4][4], int damages[4][4], int highestScoredMove);
u8 LONG_CALL ChooseMove(struct BattleSystem *bsys, int target, int moveScores[4][4], int highestScoredMove);
BOOL LONG_CALL CalculateSwitch(struct BattleSystem *bsys, u32 attacker, u32 defender, struct AIContext *ai);
void LONG_CALL CalcTurnStateDamagesAndScores(struct BattleSystem *bsys, u32 attacker, u32 defender, struct AIContext *aiOp1, struct AIContext *aiOp2, struct AI_turnState *turnState);

int TrainerAI_PickCommand(struct BattleSystem *bsys, int attacker)
{
#ifdef DEBUG_AI_SCORING
    debug_printf("TrainerAI_PickCommand: %d\n", attacker);
#endif // DEBUG_AI_SCORING
    struct BattleStruct *ctx = bsys->sp;
    BOOL isTagPartner = FALSE;
    if (attacker == 2) {
        isTagPartner = TRUE;
    }

    if (BattleTypeGet(bsys) == BATTLE_TYPE_SAFARI || BattleTypeGet(bsys) == BATTLE_TYPE_ROAMER) {
        return PLAYER_INPUT_FIGHT;
    }
    if ((BattleTypeGet(bsys) & BATTLE_TYPE_TRAINER) == 0) {
        return PLAYER_INPUT_FIGHT;
    }
    if (ctx->aiTurnScoring.calcState == CalcedEnemy_1_and_3 && !isTagPartner) {
        return PLAYER_INPUT_FIGHT;
    }

    int targets[4] = { 0 };
    int targetsAlly[4] = { 0 };
    enum AIActionChoice result = AI_ENEMY_ATTACK_1;

    struct AIContext aiContext[4][2] = { 0 };
    struct AI_turnState turnStateStruct[4] = { 0 };

    u8 ally = BATTLER_ALLY(attacker);
    u8 defender = BATTLER_OPPONENT(attacker);


#ifdef DEBUG_AI_SCORING
    if (BattleTypeGet(bsys) & (BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG)) {

        debug_printf("att %d(%d), ally %d(%d), defendOp %d(%d), defendCross %d(%d)\n",
            attacker, ctx->battlemon[attacker].species,
            ally, ctx->battlemon[ally].species,
            defender, ctx->battlemon[defender].species,
            BATTLER_ACROSS(attacker), ctx->battlemon[BATTLER_ACROSS(attacker)].species);
    }
    else
    {
        debug_printf("att %d(%d), defendOp %d(%d)\n", attacker, ctx->battlemon[attacker].species, defender, ctx->battlemon[defender].species);
    }
#endif // DEBUG_AI_SCORING

    BOOL canDivert = FALSE;
    if ((BattleTypeGet(bsys) & (BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG))
        && ctx->battlemon[attacker].hp
        && ctx->battlemon[ally].hp
        && !isTagPartner)
    {
        canDivert = TRUE;
        int speedCalc = CalcSpeed(bsys, ctx, attacker, ally, CALCSPEED_FLAG_NO_PRIORITY);
        if (speedCalc != 0) {
            int swap = ally;
            ally = attacker;
            attacker = swap;
            defender = BATTLER_OPPONENT(attacker);
#ifdef DEBUG_AI_SCORING
            debug_printf("swapping attacker(%d) and ally(%d): %d < %d\n", attacker, ally, ctx->effectiveSpeed[ally], ctx->effectiveSpeed[attacker]);
#endif // DEBUG_AI_SCORING
        }
    }

    struct AIContext *ai1 = &aiContext[attacker][0];
    struct AIContext *ai2 = &aiContext[attacker][1];
    struct AIContext *aiAlly1 = &aiContext[ally][0];
    struct AIContext *aiAlly2 = &aiContext[ally][1];
    struct AI_turnState *turnState = &turnStateStruct[attacker];

    CalcTurnStateDamagesAndScores(bsys, attacker, defender, ai1, ai2, turnState);

    u8 attackerTargetSize = FindTargets(ctx, attacker, targets, turnState->moveScores, turnState->damages, turnState->highestScoredMove);
    u8 target = targets[(BattleRand(bsys) % attackerTargetSize)];
    result = ChooseMove(bsys, target, turnState->moveScores, turnState->highestScoredMove);

    u32 attackerMoveno = ctx->battlemon[attacker].move[result];
    struct BattleMove attackerMove = ctx->moveTbl[attackerMoveno];
    aiAlly1->partnerMoveNo = attackerMoveno;
    aiAlly2->partnerMoveNo = attackerMoveno;
    if (attackerMove.split != SPLIT_STATUS && canDivert)
    {
        aiAlly1->partnerClicksAttackingMove = TRUE;
        aiAlly2->partnerClicksAttackingMove = TRUE;
        if (ai1->monCanOneShotPlayerWithAnyMove && ai1->aiMovesFirst) {
#ifdef DEBUG_AI_SCORING
            debug_printf("attacker(%d) goes for fast kill on defender %d. Partner ignores target.\n", attacker, ai1->defender);
#endif // DEBUG_AI_SCORING
            aiAlly2->ignoreTarget = TRUE;
        } else if (ai2->monCanOneShotPlayerWithAnyMove && ai2->aiMovesFirst) {
#ifdef DEBUG_AI_SCORING
            debug_printf("attacker(%d) goes for fast kill on defender %d. Partner ignores target.\n", attacker, ai2->defender);
#endif // DEBUG_AI_SCORING
            aiAlly1->ignoreTarget = TRUE;
        }
    }

    u8 allyTarget = 0;
    enum AIActionChoice resultAlly = AI_ENEMY_ATTACK_1;
    if (ai1->isDoubleBattle && ai1->isAllyAlive && (attacker == 1 || attacker == 3)) { // only enemy side
        u8 defenderAlly = BATTLER_OPPONENT(ally);
       
        struct AI_turnState *turnStateAlly = &turnStateStruct[ally];
        CalcTurnStateDamagesAndScores(bsys, ally, defenderAlly, aiAlly1, aiAlly2, turnStateAlly);

        u8 allyTargetSize = FindTargets(ctx, ally, targetsAlly, turnStateAlly->moveScores, turnStateAlly->damages, turnStateAlly->highestScoredMove);
        allyTarget = targetsAlly[(BattleRand(bsys) % allyTargetSize)];
        resultAlly = ChooseMove(bsys, allyTarget, turnStateAlly->moveScores, turnStateAlly->highestScoredMove);
    }

    if (BattleTypeGet(bsys) & BATTLE_TYPE_TRAINER && ai1->isDoubleBattle == FALSE) {
        if (ai1->shouldSwitch 
            && (!ai1->attackerHasValidSwitchingMove || (ai1->playerMovesFirst && ai1->playerCanOneShotMonWithAnyMove)))
        {
            ctx->aiSwitchedPartySlot[attacker] = ai1->postKoScoringPosition;
            return PLAYER_INPUT_PARTY;
        }
    }

    ctx->aiTurnScoring.targets[attacker] = target;
    ctx->aiTurnScoring.choice[attacker] = result;
    if (ctx->battlemon[attacker].move[result] == MOVE_MILK_DRINK && target != BATTLER_ALLY(attacker)) {
        ctx->aiTurnScoring.targets[attacker] = attacker;
    }

    if (ai1->isDoubleBattle && ai1->isAllyAlive && (attacker == 1 || attacker == 3)) {
        ctx->aiTurnScoring.targets[ally] = allyTarget;
        ctx->aiTurnScoring.choice[ally] = resultAlly;
        ctx->aiTurnScoring.calcState = CalcedEnemy_1_and_3;

        if (ctx->battlemon[ally].move[resultAlly] == MOVE_MILK_DRINK && allyTarget != BATTLER_ALLY(ally)) {
            ctx->aiTurnScoring.targets[ally] = ally;
        }
    }

    return PLAYER_INPUT_FIGHT;
}

u8 LONG_CALL FindTargets(struct BattleStruct *ctx, u8 attacker, int targets[4], int moveScores[4][4], int damages[4][4], int highestScoredMove)
{
    int targetsSize = 0;
    for (u8 k = 0; k < 4; k++) //targets
    {
        for (u8 i = 0; i < 4; i++) // movesScore
        {
            if (moveScores[k][i] == highestScoredMove) {
#ifdef DEBUG_AI_SCORING
                debug_printf("found target %d with score %d, dmg %d\n", k, highestScoredMove, damages[k][i]);
#endif // DEBUG_AI_SCORING
                targets[targetsSize] = k;
                targetsSize++;
                break;
            }
        }
    }
#ifdef DEBUG_AI_SCORING
    debug_printf("attacker %d (%d):\n", attacker, ctx->battlemon[attacker].species);
    u8 j = 0;
    for (int k = 0; k < 4; k++) {
        for (u8 i = 0; i < 4; i++) // movesScore
        {
            debug_printf("%4d/%4d  ", moveScores[k][i], damages[k][i]);
        }

        if (targets[j] == k) {
            j++;
            debug_printf("x");
        }
        debug_printf("\n");
    }
#endif // DEBUG_AI_SCORING

    return targetsSize;
}

u8 LONG_CALL ChooseMove(struct BattleSystem *bsys, int target, int moveScores[4][4], int highestScoredMove)
{
    int tieMoveCount = 0;
    int tiedMoveIndices[4] = { 0 };
    for (u8 i = 0; i < 4; i++) { // check for ties
        if (moveScores[target][i] == highestScoredMove) {
            tiedMoveIndices[tieMoveCount] = i;
            tieMoveCount++;
        }
    }
    int rand = BattleRand(bsys);
    u8 tieMoveIndex = (rand % tieMoveCount);
    u8 result = tiedMoveIndices[tieMoveIndex]; // % 4]; // randomly pick a move among the tie
#ifdef DEBUG_AI_SCORING
    debug_printf("got index %d: %d mod %d -> Resulting move: %d\n", tieMoveIndex, rand, tieMoveCount, result);
#endif // DEBUG_AI_SCORING
    return result;
}

int getVarianceFromDamage(struct AIContext *ai)
{
    if (ai->highestDamageHitPrct <= 0) {
        return 30;
    }
    if (ai->highestDamageHitPrct >= 50) {
        return 0;
    }

    return (50 - ai->highestDamageHitPrct) * 3 / 5;
}

BOOL LONG_CALL CalculateSwitch(struct BattleSystem *bsys, u32 attacker, u32 defender, struct AIContext *ai)
{
    struct BattleStruct *ctx = bsys->sp;
    if (CantEscape(bsys, ctx, attacker, NULL)) {
        return FALSE;
    }

    if (ai->livingMembersAttacker < 2) {
        return FALSE;
    }

    if (ai->monCanOneShotPlayerWithAnyMove && ai->aiMovesFirst) {
        return FALSE;
    }

    if (ai->monCanOneShotPlayerWithAnyMove && ai->playerMovesFirst && !ai->playerCanOneShotMonWithAnyMove) {
        return FALSE;
    }

    if (ai->attackerPositiveStatChangesSum > 0) {
        return FALSE;
    }

    if (ai->attackerMon.percenthp < 67) {
        return FALSE;
    }

    if ((100*ai->maxDamageReceived/ai->attackerMon.hp) < (100*ai->attackerRolledMaxDamage/ai->defenderMon.hp)) {
        return FALSE;
    }

    BOOL hasPerishSong = FALSE;
    if (ctx->battlemon[attacker].effect_of_moves & MOVE_EFFECT_FLAG_PERISH_SONG) {
        hasPerishSong = TRUE;
    }
    int doublesAddon = 0;
    if ((BattleTypeGet(bsys) & (BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG)) 
        && ctx->battlemon[defender].hp 
        && ctx->battlemon[BATTLER_ALLY(defender)].hp) {
        doublesAddon = 3;
    }

    if (ai->highestPostKoScoreFromParty < 103) {
        return FALSE;
    }

    int switchScore = 0;
    if (hasPerishSong) {
        if (ai->highestPostKoScoreFromParty >= (103 + doublesAddon)) {
            switchScore = 33 * (ai->highestPostKoScoreFromParty - (102 + doublesAddon));
        }
    } else {
        if (ai->highestPostKoScoreFromParty == (104 + doublesAddon)) {
            switchScore = getVarianceFromDamage(ai)/2;
        }
        if (ai->highestPostKoScoreFromParty >= (104 + doublesAddon + 1)) {
            switchScore = 5 + getVarianceFromDamage(ai);
        }
    }

    int rand = BattleRand(bsys) % 100;
#ifdef DEBUG_AI_SCORING
    debug_printf("rand %d, switchScore %d\n", rand, switchScore);
#endif // DEBUG_AI_SCORING
    if (rand < switchScore) {
        return TRUE;
    }

    return FALSE;
}

void LONG_CALL CalcTurnStateDamagesAndScores(struct BattleSystem *bsys, u32 attacker, u32 defender, struct AIContext *aiOp1, struct AIContext *aiOp2, struct AI_turnState *turnState)
{
    struct BattleStruct *ctx = bsys->sp;
    u32 ally = BATTLER_ALLY(attacker);
    u32 defenderAcross = BATTLER_ACROSS(attacker);
    if (ctx->battlemon[defender].hp > 0 && !aiOp1->ignoreTarget) {
        SetupStateVariables(bsys, attacker, defender, aiOp1);
        for (u8 i = 0; i < 4; i++) {
            turnState->damages[defender][i] = aiOp1->attackerRolledMoveDamages[i];
        }
        aiOp1->shouldSwitch = CalculateSwitch(bsys, attacker, defender, aiOp1);
        aiOp2->highestPostKoScoreFromParty = aiOp1->highestPostKoScoreFromParty;
        aiOp2->postKoScoringPosition = aiOp1->postKoScoringPosition;
    } else {
#ifdef DEBUG_AI_SCORING
        debug_printf("attacker(%d) ignores target %d.\n", attacker, defender);
#endif // DEBUG_AI_SCORING
    }

    if (BattleTypeGet(bsys) & (BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG)) {
        if (ctx->battlemon[defenderAcross].hp > 0 && !aiOp2->ignoreTarget) {
            SetupStateVariables(bsys, attacker, defenderAcross, aiOp2);
            for (u8 i = 0; i < 4; i++) {
                turnState->damages[defenderAcross][i] = aiOp2->attackerRolledMoveDamages[i];
            }
            aiOp2->shouldSwitch = CalculateSwitch(bsys, attacker, defenderAcross, aiOp2);
        }
    } else {
#ifdef DEBUG_AI_SCORING
        debug_printf("attacker(%d) ignores target %d.\n", attacker, defenderAcross);
#endif // DEBUG_AI_SCORING
    }


    if (aiOp1->playerCanOneShotMonWithAnyMove) {
        aiOp2->playerCanOneShotMonWithAnyMove = TRUE;
    }
    if (aiOp2->playerCanOneShotMonWithAnyMove) {
        aiOp1->playerCanOneShotMonWithAnyMove = TRUE;
    }

    if (ctx->battlemon[defender].hp > 0 && !aiOp1->ignoreTarget) {
        turnState->highestScoredMove = ScoreMovesAgainstDefender(bsys, attacker, defender, turnState->moveScores, aiOp1);
    }

    if (BattleTypeGet(bsys) & (BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG)) {
        int highestScoredMoveAcross = 0;
        if (ctx->battlemon[defenderAcross].hp > 0 && !aiOp2->ignoreTarget) {
            highestScoredMoveAcross = ScoreMovesAgainstDefender(bsys, attacker, defenderAcross, turnState->moveScores, aiOp2);
            if (highestScoredMoveAcross > turnState->highestScoredMove) {
                turnState->highestScoredMove = highestScoredMoveAcross;
            }
        }

        highestScoredMoveAcross = ScoreMovesAgainstAlly(bsys, attacker, ally, turnState->moveScores, aiOp1);
        if (highestScoredMoveAcross > turnState->highestScoredMove) {
            turnState->highestScoredMove = highestScoredMoveAcross;
        }
    }
}




