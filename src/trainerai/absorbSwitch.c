#include "../../include/battle.h"
#include "../../include/config.h"
#include "../../include/constants/species.h"
#include "../../include/custom/custom_ai.h"
#include "../../include/debug.h"
#include "../../include/mega.h"
#include "../../include/pokemon.h"
#include "../../include/types.h"

#ifdef IMPLEMENT_AI_ABSORB_SWITCH

/**
 *  @brief choose the best Pokémon to switch into out of those that were checked
 *         pure selection logic, so it can be tested without a running battle
 *
 *  @param candidates Pokémon that were checked
 *  @param count how many there are
 *  @return party slot of the best candidate, or -1 if none is good enough
 *          a candidate must be immune to the killing move and must not be KO'd by any of the player's other moves;
 *          among those, the one that would lose the smallest share of its HP to the player's other moves wins
 */
int LONG_CALL AI_PickImmuneCandidate(const AI_immuneCandidate *candidates, int count)
{
    int best = -1;
    for (int i = 0; i < count; i++) {
        if (!candidates[i].immune || candidates[i].killedByOtherMove) {
            continue;
        }
        if (best < 0 || candidates[i].worstOtherPercent < candidates[best].worstOtherPercent) {
            best = i;
        }
    }
    return best < 0 ? -1 : candidates[best].slot;
}

#ifdef AI_ABSORB_SWITCH_MEGA_PREVIEW
u32 LONG_CALL GrabMegaTargetForm(u32 mon, u32 item, u32 form);

/**
 *  @brief the player's Pokémon as the AI should calculate with it this turn: Mega Evolved, if it can Mega Evolve
 *         the AI chooses before the player does, so it cannot know whether the player will; it plays safe and assumes they will.
 *         The real Pokémon is never touched: the stats, ability and types are worked out on a copy.
 *
 *  @param bsys battle system
 *  @param ctx battle context
 *  @param client the player's battler
 *  @param out the AI's picture of the player's Pokémon; its form, ability, stats and types are replaced when TRUE is returned
 *  @return TRUE if the Pokémon can Mega Evolve and `out` now shows the Mega form
 */
static BOOL AI_PreviewPlayerMega(struct BattleSystem *bsys, struct BattleStruct *ctx, u32 client, struct AI_sDamageCalc *out)
{
    u32 species = ctx->battlemon[client].species;
    u32 item = ctx->battlemon[client].item;
    u32 form = ctx->battlemon[client].form_no;

    if (ctx->battlemon[client].canMega || newBS.SideMega[client]) {
        return FALSE; // the same two checks CheckCanMega makes: it has already Mega Evolved this battle
    }
    if (!CheckMegaData(species, item, form)) {
        return FALSE;
    }
    u32 megaForm = GrabMegaTargetForm(species, item, form);
    if (megaForm == 0) {
        return FALSE;
    }
    struct PartyPokemon *real = BattleWorkPokemonParamGet(bsys, client, ctx->sel_mons_no[client]);
    if (real == NULL) {
        return FALSE;
    }

    struct PartyPokemon copy;
    memcpy(&copy, real, sizeof(struct PartyPokemon));
    SetMonData(&copy, MON_DATA_FORM, &megaForm);
    RecalcPartyPokemonStats(&copy);
    ResetPartyPokemonAbility(&copy);

    struct AI_sDamageCalc mega = { 0 };
    FillDamageStructFromPartyMon(bsys, ctx, &mega, &copy, client, ctx->sel_mons_no[client]);
    out->form = mega.form;
    out->ability = mega.ability;
    out->hasMoldBreaker = mega.hasMoldBreaker;
    out->attack = mega.attack;
    out->defense = mega.defense;
    out->sp_attack = mega.sp_attack;
    out->sp_defense = mega.sp_defense;
    out->type1 = mega.type1;
    out->type2 = mega.type2;
    return TRUE;
}
#endif // AI_ABSORB_SWITCH_MEGA_PREVIEW

/**
 *  @brief damage the player's move would do to a party Pokémon, using the same estimate as the rest of the AI
 */
static u32 DamageToPartyMon(struct BattleSystem *bsys, struct BattleStruct *ctx, u32 attacker, u32 defender, struct AIContext *ai, struct AI_sDamageCalc *player, struct AI_sDamageCalc *partyMon, u32 moveno)
{
    struct AI_damage damages = { 0 };
    struct BattleMove move = ctx->moveTbl[moveno];

    damages.damageRoll = BattleAI_CalcDamage(bsys, ctx, moveno, move.power, defender, attacker, &damages, player, partyMon);
    damages.damageRoll = damages.damageRange[15]; // best-case roll; stays 0 when the move cannot hurt this Pokémon at all
    damages.damageRoll = BattleAI_AdjustUnusualMoveDamage(player, partyMon, damages.damageRoll, move.effect, moveno, damages.moveEffectiveness);
#ifdef IMPLEMENT_AI_FIXED_DAMAGE_ESTIMATES
    damages.damageRoll = AI_ScaleDamage(damages.damageRoll, AI_PARTY_DAMAGE_PERCENT);
#endif // IMPLEMENT_AI_FIXED_DAMAGE_ESTIMATES
    return damages.damageRoll;
}

/**
 *  @brief look for a Pokémon in the AI's party that is immune to the move the player is about to use
 *         (Water Absorb, Volt Absorb, Flash Fire, Sap Sipper, Levitate, Lightning Rod, Storm Drain, Motor Drive, type immunities...)
 *         this only matters when that move would KO the AI's current Pokémon
 *
 *  @param bsys battle system
 *  @param attacker the AI battler
 *  @param defender the player's battler
 *  @param ai AI context
 *  @return party slot to switch into, or -1 if the AI should not make this kind of switch
 */
int LONG_CALL AI_FindImmuneSwitchIn(struct BattleSystem *bsys, u32 attacker, u32 defender, struct AIContext *ai)
{
    struct BattleStruct *ctx = bsys->sp;
    AI_immuneCandidate candidates[6];
    int count = 0;

    if (ai->isDoubleBattle || ai->livingMembersAttacker < 2) {
        return -1;
    }
    if (ai->playerPredictedMove == MOVE_NONE || !ai->playerPredictedMoveKills) {
        return -1; // the move we expect would not KO us, so there is nothing to dodge
    }
    if (ai->monCanOneShotPlayerWithAnyMove && ai->aiMovesFirst) {
        return -1; // we get there first with a KO of our own
    }
    if (ai->attackerPositiveStatChangesSum > 0) {
        return -1; // already invested in setup
    }

    // the player's Pokémon as the AI should calculate with it: Mega Evolved if it can be (it may do so this turn, after the AI has chosen)
    struct AI_sDamageCalc playerCalc = ai->defenderMon;
    BOOL playerMega = FALSE;
#ifdef AI_ABSORB_SWITCH_MEGA_PREVIEW
    playerMega = AI_PreviewPlayerMega(bsys, ctx, defender, &playerCalc);
#endif // AI_ABSORB_SWITCH_MEGA_PREVIEW

    int partySize = Battle_GetClientPartySize(bsys, attacker);
    for (int i = 0; i < partySize && count < 6; i++) {
        struct PartyPokemon *mon = Battle_GetClientPartyMon(bsys, attacker, i);
        u32 species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG, 0);
        u32 hp = GetMonData(mon, MON_DATA_HP, 0);
        if (species == SPECIES_NONE || species == SPECIES_EGG || hp == 0 || i == ctx->sel_mons_no[attacker]) {
            continue;
        }

        struct AI_sDamageCalc partyMon = { 0 };
        FillDamageStructFromPartyMon(bsys, ctx, &partyMon, mon, attacker, i);

        AI_immuneCandidate *c = &candidates[count];
        c->slot = i;
        c->immune = (DamageToPartyMon(bsys, ctx, attacker, defender, ai, &playerCalc, &partyMon, ai->playerPredictedMove) == 0);
        c->killedByOtherMove = FALSE;
        c->worstOtherPercent = 0;

        if (c->immune) {
            // make sure it is not simply KO'd by something else the player knows
            for (int k = 0; k < GetBattlerLearnedMoveCount(bsys, ctx, defender); k++) {
                u32 moveno = ctx->battlemon[defender].move[k];
                struct BattleMove move = ctx->moveTbl[moveno];
                if (moveno == ai->playerPredictedMove || move.split == SPLIT_STATUS || move.power == 0
                    || !IsMoveUsable(ctx, defender, moveno, ai->defenderLastUsedMove, move.split, k)) {
                    continue;
                }
                u32 damage = DamageToPartyMon(bsys, ctx, attacker, defender, ai, &playerCalc, &partyMon, moveno);
                if (CanAttackerOneShotDefender(damage, move.split, moveno, &playerCalc, &partyMon)) {
                    c->killedByOtherMove = TRUE;
                }
                u32 percent = partyMon.hp > 0 ? (100 * damage / partyMon.hp) : 100;
                if (percent > c->worstOtherPercent) {
                    c->worstOtherPercent = percent;
                }
                if (percent >= AI_ABSORB_SWITCH_SAFE_PERCENT) {
                    c->killedByOtherMove = TRUE; // too close to a KO: the estimate ignores crits, items and surprises
                }
            }
        }
#ifdef DEBUG_AI_SCORING
        debug_printf("immune switch check: slot %d immune %d, killed by other move %d, worst other %d pct, player mega preview %d\n", c->slot, c->immune, c->killedByOtherMove, c->worstOtherPercent, playerMega);
#endif // DEBUG_AI_SCORING
        count++;
    }

    return AI_PickImmuneCandidate(candidates, count);
}

#endif // IMPLEMENT_AI_ABSORB_SWITCH
