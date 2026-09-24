#ifndef CUSTOM_AI_H
#define CUSTOM_AI_H
#include "../battle.h"
#include "../trainer_ai.h"

#define DEBUG_AI_SCORING 1




struct PACKED AI_turnState {
    int moveScores[4][4]; // account for BATTLER_OPPONENT (0), attacker (1), BATTLER_ACROSS(2), BATTLER_ALLY(3),  4 moves each or
                          // account for BATTLER_OPPONENT (2), attacker (3), BATTLER_ACROSS(0), BATTLER_ALLY(1),  4 moves each
    int damages[4][4]; // rolled damage for each move against each target

    int highestScoredMove;
};

struct PACKED AI_sDamageCalc {
    u16 species;
    u16 hp;
    u16 maxhp;
    u16 percenthp;
    u16 dummy;
    u32 item;
    u32 item_held_effect;
    u32 item_power;

    u32 condition;
    u32 condition2;
    u8 isGrounded;

    u16 ability;
    u8 sex;
    u8 type1;
    u8 type2;
    u8 type3;

    u32 speed;

    u32 weight;

    u32 attack;
    u32 defense;
    u32 sp_attack;
    u32 sp_defense;
    s8 states[8];
    u8 level;
    u32 form;

    BOOL hasMoldBreaker;
    u32 effect_of_moves;
    BOOL flashFireActivated;
    u8 hiddenPowerType;

    u8 slowStartCount;
    u8 furyCutterCount;
    u8 metronomeTurns;
    u8 lastResortCount;
    u8 attackerHasMoveFailureLastTurn;
    u8 canBelch;
    u8 paradoxBoostedStat;
};

struct PACKED AIContext {
    struct AI_sDamageCalc attackerMon;
    struct AI_sDamageCalc defenderMon; //"player"
    struct AI_sDamageCalc aimonAlly;

    int highestPostKoScoreFromParty;
    int postKoScoringPosition;
    int highestDamageHitPrct;

    BOOL isAllyAlive : 1;
    BOOL isDoubleBattle : 1;
    BOOL isPartnerGrounded : 1;

    BOOL aiMovesFirst : 1;
    BOOL playerMovesFirst : 1;
    BOOL isSpeedTie : 1;
    BOOL padding0 : 2;


    BOOL defenderHasMagicBounce : 1;
    BOOL defenderAllyHasMagicBounce : 1;
    BOOL defenderImmuneToPoison : 1;
    BOOL defenderImmuneToParalysis : 1;
    BOOL defenderImmuneToBurn : 1;
    BOOL defenderImmuneToSleep : 1;
    BOOL defenderImmuneToStatDrop : 1;
    BOOL padding1 : 1;

    int attacker;
    int defender;
    int defenderSide;
    int attackerSide;

    int attackerSpeed;
    int defenderSpeed;

    u8 partySizeAttacker;
    u8 livingMembersAttacker;
    u8 monWithMegaInParty;

    u8 partySizeDefender;
    u8 livingMembersDefender;

    u8 attackerMovesKnown;
    BOOL attackerHasAttackingMoves;

    u32 attackerLastUsedMove;
    u32 defenderLastUsedMove;
    u32 defenderLastUsedMoveEffect;
    u32 defenderTurnsOnField;
    u32 attackerTurnsOnField;

    u32 attackerMove;
    u32 attackerMoveEffect;

    BOOL isDefenderIncapacitated : 1;
    BOOL defenderKnowsThawingMove : 1;
    BOOL defenderHasAtleastOneUsefulSoundMove : 1;
    BOOL defenderCanForceSwitching : 1;
    BOOL defenderHasSturdyOrFocusSashActive : 1;
    BOOL attackerHasSturdyOrFocusSashActive : 1;
    BOOL padding2 : 2;

    u32 maxDamageReceived;

    BOOL playerCanOneShotMonWithMove[4];
    BOOL playerCanOneShotMonWithAnyMove : 1;
    BOOL monCanOneShotPlayerWithAnyMove : 1;
    BOOL defenderHasAtleastOnePhysicalMove : 1;
    BOOL defenderHasAtleastOneSpecialMove : 1;
    BOOL defenderHasAtleastOneStatusMove : 1;
    BOOL attackerHasValidSwitchingMove : 1;
    BOOL attackerHasValidDamagingMove : 1;
    BOOL shouldSwitch : 1;
    
    BOOL monCanOneShotPlayerWithMove[4];
    u32 attackerRolledMoveDamages[4];
    u32 effectivenessOnPlayer[4];
    u32 attackerRolledMaxDamage;

    u32 partnerMoveNo;
    BOOL partnerClicksAttackingMove;
    BOOL ignoreTarget;

    u8 attackerPositiveStatChangesSum;
};

struct PACKED AI_damage {
    u32 damageRoll;
    u32 damageRange[16];
    u32 moveEffectiveness;
};

void LONG_CALL SetupStateVariables(struct BattleSystem *bsys, u32 attacker, u32 defender, struct AIContext *ai);


int LONG_CALL ScoreMovesAgainstDefender(struct BattleSystem *bsys, u32 attacker, u32 target, int moveScores[4][4], struct AIContext *ai);
int LONG_CALL ScoreMovesAgainstAlly(struct BattleSystem *bsys, u32 attacker, u32 target, int moveScores[4][4], struct AIContext *ai);


int LONG_CALL BattleAI_PostKOSwitchIn(struct BattleSystem *bsys, int attacker);
int LONG_CALL BattleAI_PostKOSwitchIn_Internal(struct BattleSystem *bsys, int attacker, int *score, BOOL calcWithHighestDamageHit, int *highestDamageHitPrct);

u8 LONG_CALL BattleAI_CalcSpeed(void *bw, struct BattleStruct *sp, int client1, struct PartyPokemon *partyMon, int flag, int* effectivePartyMonSpeed);

int LONG_CALL BattleAI_CalcBaseDamage(void *bw, struct BattleStruct *sp, int moveno, u32 side_cond, u32 field_cond, u16 pow, u8 type, u8 critical, u8 attackerSlot, u8 defenderSlot, struct AI_sDamageCalc *attacker, struct AI_sDamageCalc *defender);
int LONG_CALL BattleAI_CalcDamage(void *bw, struct BattleStruct *sp, int moveno, u32 side_cond, u32 field_cond, u16 pow, u8 type, u8 critical, u8 attackerSlot, u8 defenderSlot, struct AI_damage *damages, struct AI_sDamageCalc *attacker, struct AI_sDamageCalc *defender);

void LONG_CALL FillDamageStructFromPartyMon(void *bw UNUSED, struct BattleStruct *sp, struct AI_sDamageCalc *monStruct, struct PartyPokemon *pp, int attackerPos UNUSED, int partyPos UNUSED);

void LONG_CALL FillDamageStructFromBattleMon(void *bw, struct BattleStruct *sp, struct AI_sDamageCalc *monStruct, int numSlot);

BOOL LONG_CALL IsMoveBoostedBySheerForce(u32 moveno, u32 moveeffect);
int LONG_CALL BattleAI_GetTypeEffectiveness(void *bw, struct BattleStruct *sp, int moveno, int move_type, u8 attackerSlot, u8 defenderSlot, struct AI_sDamageCalc *attacker, struct AI_sDamageCalc *defender);

BOOL LONG_CALL BattleAI_AttackerHasOnlyIneffectiveMoves(struct BattleStruct *ctx, u32 attacker, int knownMoves, u32 effectiveness[4]);

int LONG_CALL BattleAI_AdjustUnusualMoveDamage(struct AI_sDamageCalc *attacker, struct AI_sDamageCalc *defender, u32 damage, u32 moveEffect, u32 moveno UNUSED, u32 effectiveness);
int LONG_CALL BattleAI_GetDynamicMoveType(struct BattleSystem *bsys, struct BattleStruct *ctx, struct AI_sDamageCalc *attacker, int moveno);
BOOL LONG_CALL BattleAI_IsKnockOffPoweredUp(struct AI_sDamageCalc *defender);

BOOL LONG_CALL CanAttackerOneShotDefender(u32 attackerHighestDamage, u8 split, u32 moveno, struct AI_sDamageCalc *attacker, struct AI_sDamageCalc *defender);
BOOL LONG_CALL BattlerKnowsThawingMove(struct BattleSystem *bsys, u32 battler, struct AIContext *ai UNUSED);

BOOL LONG_CALL IsChoicedMoveConsidedUseless(u32 moveno, u8 split);

BOOL LONG_CALL IsBattleMonSlowerThanOpposition(struct BattleSystem *bsys, u8 slot, BOOL isDoubleBattle);

BOOL LONG_CALL IsPartyPokemonGrounded(struct BattleStruct *sp, struct PartyPokemon *pp);

BOOL LONG_CALL IsMoveUsefulSoundMove(u32 moveno);
BOOL LONG_CALL IsMoveForceSwitching(u32 moveno);
BOOL LONG_CALL IsMoveValidSwitchingMove(u32 moveno);

u32 LONG_CALL BattleAI_GetWeather(struct BattleSystem *bsys, struct BattleStruct *ctx, int ability);

u8 LONG_CALL BattleAI_GetHighestParadoxStat(u8 atk, u8 def, u8 spatk, u8 spdef, u8 speed);

BOOL LONG_CALL HasMovePranksterPriority(struct BattleSystem *bsys, u8 attacker, u32 attackerMove, u32 attackerAbility, u8 defender);

BOOL LONG_CALL IsMoveUsable(struct BattleStruct *ctx, u8 attacker, u32 move, u32 moveLastUsed, u8 split, u8 index);


int LONG_CALL BattlerPositiveStatChangesSum(struct BattleSystem *bsys, u32 battler);
BOOL LONG_CALL MonDiesFromResidualDamage(struct BattleStruct *ctx, u32 attacker, u32 attackerCondition, BOOL isSeeded);
BOOL LONG_CALL IsMonInflictedWithAnyNegativeStatus(struct BattleStruct *ctx, u32 attacker);

BOOL LONG_CALL BattlerHasSoundBasedMove(struct BattleSystem *bsys, u32 battler, struct AIContext *ai UNUSED);
BOOL LONG_CALL BattlerKnowsFlinchingMove(struct BattleSystem *bsys, u32 battler, struct AIContext *ai UNUSED);
BOOL LONG_CALL BattlerKnowsMove(struct BattleSystem *bsys, u32 battler, u32 move, struct AIContext *ai UNUSED);
BOOL LONG_CALL BattlerKnowsMoveWithEffect(struct BattleSystem *bsys, u32 battler, u32 moveEffect, struct AIContext *ai UNUSED);
BOOL LONG_CALL IsUsedMoveEncouragedToEncore(struct BattleSystem *bsys UNUSED, u32 move, u32 moveEffect);
#endif // !CUSTOM_AI_H
