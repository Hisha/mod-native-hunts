#include <limits>
#include "HuntManager.h"
#include "HuntCurrencyService.h"

#include "AllCreatureScript.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "GameObject.h"
#include "GameObjectScript.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "SharedDefines.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellInfo.h"

#include <algorithm>
#include <sstream>
#include <vector>
#include "ScriptedGossip.h"

namespace
{
enum HuntGossipAction : uint32
{
    ACTION_HUNT_STATUS = GOSSIP_ACTION_INFO_DEF + 1,
    ACTION_REQUEST_HUNT = GOSSIP_ACTION_INFO_DEF + 2,
    ACTION_TURN_IN_HUNT = GOSSIP_ACTION_INFO_DEF + 3,
    ACTION_ABANDON_HUNT = GOSSIP_ACTION_INFO_DEF + 4,
    ACTION_HUNT_STATS = GOSSIP_ACTION_INFO_DEF + 5,
    ACTION_REQUEST_ELITE_HUNT = GOSSIP_ACTION_INFO_DEF + 6,
    ACTION_NATIVE_VENDOR = GOSSIP_ACTION_INFO_DEF + 20,
    ACTION_GUARD_HUNTMASTER = GOSSIP_ACTION_INFO_DEF + 700
};

struct SealSpecChoice
{
    uint32 Spec = 0;
    char const* Name = "";
};

std::vector<SealSpecChoice> GetSealSpecs(Player const* player)
{
    if (!player)
        return {};

    switch (player->getClass())
    {
        case CLASS_WARRIOR:
            return {{TALENT_TREE_WARRIOR_ARMS, "Arms"}, {TALENT_TREE_WARRIOR_FURY, "Fury"}, {TALENT_TREE_WARRIOR_PROTECTION, "Protection"}};
        case CLASS_PALADIN:
            return {{TALENT_TREE_PALADIN_HOLY, "Holy"}, {TALENT_TREE_PALADIN_PROTECTION, "Protection"}, {TALENT_TREE_PALADIN_RETRIBUTION, "Retribution"}};
        case CLASS_HUNTER:
            return {{TALENT_TREE_HUNTER_BEAST_MASTERY, "Beast Mastery"}, {TALENT_TREE_HUNTER_MARKSMANSHIP, "Marksmanship"}, {TALENT_TREE_HUNTER_SURVIVAL, "Survival"}};
        case CLASS_ROGUE:
            return {{TALENT_TREE_ROGUE_ASSASSINATION, "Assassination"}, {TALENT_TREE_ROGUE_COMBAT, "Combat"}, {TALENT_TREE_ROGUE_SUBTLETY, "Subtlety"}};
        case CLASS_PRIEST:
            return {{TALENT_TREE_PRIEST_DISCIPLINE, "Discipline"}, {TALENT_TREE_PRIEST_HOLY, "Holy"}, {TALENT_TREE_PRIEST_SHADOW, "Shadow"}};
        case CLASS_DEATH_KNIGHT:
            return {{TALENT_TREE_DEATH_KNIGHT_BLOOD, "Blood"}, {TALENT_TREE_DEATH_KNIGHT_FROST, "Frost"}, {TALENT_TREE_DEATH_KNIGHT_UNHOLY, "Unholy"}};
        case CLASS_SHAMAN:
            return {{TALENT_TREE_SHAMAN_ELEMENTAL, "Elemental"}, {TALENT_TREE_SHAMAN_ENHANCEMENT, "Enhancement"}, {TALENT_TREE_SHAMAN_RESTORATION, "Restoration"}};
        case CLASS_MAGE:
            return {{TALENT_TREE_MAGE_ARCANE, "Arcane"}, {TALENT_TREE_MAGE_FIRE, "Fire"}, {TALENT_TREE_MAGE_FROST, "Frost"}};
        case CLASS_WARLOCK:
            return {{TALENT_TREE_WARLOCK_AFFLICTION, "Affliction"}, {TALENT_TREE_WARLOCK_DEMONOLOGY, "Demonology"}, {TALENT_TREE_WARLOCK_DESTRUCTION, "Destruction"}};
        case CLASS_DRUID:
            return {{TALENT_TREE_DRUID_BALANCE, "Balance"}, {TALENT_TREE_DRUID_FERAL_COMBAT, "Feral"}, {TALENT_TREE_DRUID_RESTORATION, "Restoration"}};
        default:
            return {};
    }
}

bool NativeProofEligible(Player* player, Creature* creature)
{
    if (!sHuntCurrency.Available() || !creature || creature->GetEntry()!=sHuntCurrency.Vendor().creatureEntry) return false;
    // Preserve eligibility for every valid specialization of the player's class.
    for (auto const& choice:GetSealSpecs(player))
        if(sHuntMgr.IsNativeProofEligible(player,choice.Spec))return true;
    return false;
}

class HuntmasterScript final : public CreatureScript
{
public:
    HuntmasterScript() : CreatureScript("mod_hunts_huntmaster") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sHuntMgr.IsEnabled() || !sHuntMgr.IsHuntGiver(creature->GetEntry()))
            return false;

        ShowMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        ClearGossipMenuFor(player);
        if (action == ACTION_NATIVE_VENDOR)
        {
            CloseGossipMenuFor(player);
            if (NativeProofEligible(player,creature))
                player->GetSession()->SendListInventory(creature->GetGUID());
            return true;
        }

        std::string message;
        switch (action)
        {
            case ACTION_REQUEST_HUNT:
                sHuntMgr.RequestHunt(player, creature, message);
                break;
            case ACTION_REQUEST_ELITE_HUNT:
                sHuntMgr.RequestEliteHunt(player, creature, message);
                break;
            case ACTION_TURN_IN_HUNT:
                sHuntMgr.TurnInHunt(player, creature, message);
                break;
            case ACTION_ABANDON_HUNT:
                sHuntMgr.AbandonHunt(player, message);
                break;
            case ACTION_HUNT_STATS:
                message = sHuntMgr.BuildStats(player);
                break;
            case ACTION_HUNT_STATUS:
            default:
                message = sHuntMgr.BuildStatus(player);
                break;
        }

        ChatHandler(player->GetSession()).PSendSysMessage("|cff33ccff[Hunts]|r {}", message);
        CloseGossipMenuFor(player);
        return true;
    }

private:
    void ShowMainMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);
        hunts::HuntRuntime const* runtime = sHuntMgr.GetRuntime(player);
        if (!runtime)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I seek dangerous prey.", GOSSIP_SENDER_MAIN, ACTION_REQUEST_HUNT);
            if (sHuntMgr.IsEliteUnlocked(player))
            {
                if (sHuntMgr.IsEliteAvailableToday(player))
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I seek an Elite Hunt.", GOSSIP_SENDER_MAIN, ACTION_REQUEST_ELITE_HUNT);
                else
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "My Elite assignment is spent for today.", GOSSIP_SENDER_MAIN, ACTION_HUNT_STATS);
            }
        }
        else if (runtime->State == hunts::HuntState::ReadyToTurnIn)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I have slain my quarry.", GOSSIP_SENDER_MAIN, ACTION_TURN_IN_HUNT);
        else
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Tell me about my current hunt.", GOSSIP_SENDER_MAIN, ACTION_HUNT_STATUS);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I wish to abandon this hunt.", GOSSIP_SENDER_MAIN, ACTION_ABANDON_HUNT);
        }

        if (NativeProofEligible(player,creature))
        {
            std::ostringstream label;
            uint32 const seals = sHuntMgr.GetSealBalance(player);
            label << "Trade physical Seals for the proof reward. (" << seals << " Seal" << (seals == 1 ? "" : "s") << ")";
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, label.str(), GOSSIP_SENDER_MAIN, ACTION_NATIVE_VENDOR);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Show me my hunting record.", GOSSIP_SENDER_MAIN, ACTION_HUNT_STATS);
        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

};

// Extends the normal capital-city guard gossip without replacing the stock
// directions.  We prepare the guard's normal database menu, append one Living
// World option, and only consume our own action when it is selected.
class HuntGuardLocatorScript final : public AllCreatureScript
{
public:
    HuntGuardLocatorScript() : AllCreatureScript("HuntGuardLocatorScript") { }

    bool CanCreatureGossipHello(Player* player, Creature* creature) override
    {
        if (!sHuntMgr.IsEnabled() || !player || !creature || !sHuntMgr.IsGuardLocator(creature->GetEntry()))
            return false;

        player->PrepareGossipMenu(creature, creature->GetGossipMenuId(), true);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Where is the Huntmaster?", GOSSIP_SENDER_MAIN, ACTION_GUARD_HUNTMASTER);
        player->SendPreparedGossip(creature);
        return true;
    }

    bool CanCreatureGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        if (action != ACTION_GUARD_HUNTMASTER || !player || !creature || !sHuntMgr.IsGuardLocator(creature->GetEntry()))
            return false;

        std::string message;
        sHuntMgr.SendHuntmasterLocation(player, creature->GetEntry(), message);
        if (!message.empty())
            ChatHandler(player->GetSession()).PSendSysMessage("|cff33ccff[Hunts]|r {}", message);
        CloseGossipMenuFor(player);
        return true;
    }
};

// Elite Hunt prey are intended to fight like dangerous player-class opponents,
// not like dungeon bosses.  They therefore accept normal player crowd control,
// but hard CC uses PvP-style diminishing returns so an Elite cannot be
// stun-locked indefinitely.
//
// Stun DR is deliberately encounter-local:
//   1st stun = 100% duration
//   2nd stun =  50% duration
//   3rd stun =  25% duration
//   4th+     = immune until 15 seconds after the previous stun expires
//
// Other CC categories can be added to this same AI as Elite archetypes grow.
class HuntElitePreyAI final : public ScriptedAI
{
public:
    explicit HuntElitePreyAI(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        ClearStunDiminishing();
    }

    void SpellHit(Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!caster || !spellInfo || !caster->GetCharmerOrOwnerPlayerOrPlayerItself())
            return;

        uint32 const mechanicMask = spellInfo->GetAllEffectsMechanicMask();
        if (!(mechanicMask & (1u << MECHANIC_STUN)))
            return;

        Aura* aura = me->GetAura(spellInfo->Id, caster->GetGUID());
        if (!aura)
            return;

        // If the reset window elapsed between AI updates and this hit, begin a
        // fresh DR chain before adjusting the newly applied aura.
        if (_stunResetMs == 0)
            _stunApplications = 0;

        ++_stunApplications;

        int32 duration = aura->GetDuration();
        if (_stunApplications == 2)
            duration = std::max<int32>(1, duration / 2);
        else if (_stunApplications >= 3)
            duration = std::max<int32>(1, duration / 4);

        aura->SetDuration(duration);

        // WotLK-style DR resets after the CC has ended, not when it was cast.
        _stunResetMs = static_cast<uint32>(std::max<int32>(0, duration)) + 15000u;

        if (_stunApplications >= 3 && !_stunImmune)
        {
            me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, true);
            _stunImmune = true;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (_stunResetMs)
        {
            if (_stunResetMs <= diff)
                ClearStunDiminishing();
            else
                _stunResetMs -= diff;
        }

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    void ClearStunDiminishing()
    {
        if (_stunImmune)
            me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, false);

        _stunApplications = 0;
        _stunResetMs = 0;
        _stunImmune = false;
    }

    uint8 _stunApplications = 0;
    uint32 _stunResetMs = 0;
    bool _stunImmune = false;
};

class HuntElitePreyScript final : public AllCreatureScript
{
public:
    HuntElitePreyScript() : AllCreatureScript("HuntElitePreyScript") { }

    CreatureAI* GetCreatureAI(Creature* creature) const override
    {
        if (!creature || !sHuntMgr.IsEnabled() || !sHuntMgr.IsElitePreyEntry(creature->GetEntry()))
            return nullptr;

        return new HuntElitePreyAI(creature);
    }
};

class HuntReturnRiftScript final : public GameObjectScript
{
public:
    HuntReturnRiftScript() : GameObjectScript("mod_hunts_return_rift") { }

    bool OnGossipHello(Player* player, GameObject* object) override
    {
        std::string message;
        if (!sHuntMgr.OnReturnRiftUsed(player, object, message) && player)
            ChatHandler(player->GetSession()).SendSysMessage(message);
        // Always suppress goober default use: no cooldown, spell or shared despawn.
        return true;
    }
};

class HuntActivationScript final : public GameObjectScript
{
public:
    HuntActivationScript() : GameObjectScript("mod_hunts_activation") { }

    bool OnGossipHello(Player* player, GameObject* gameObject) override
    {
        std::string message;
        sHuntMgr.OnFinalActivatorUsed(player, gameObject, message);
        if (!message.empty())
            ChatHandler(player->GetSession()).PSendSysMessage("|cff33ccff[Hunts]|r {}", message);
        return true;
    }
};

class HuntPlayerScript final : public PlayerScript
{
public:
    HuntPlayerScript() : PlayerScript("HuntPlayerScript") { }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        sHuntMgr.OnCreatureKill(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        sHuntMgr.OnCreatureKill(owner, killed);
    }

    void OnPlayerSendListInventory(Player* player, ObjectGuid guid, uint32& vendorEntry) override
    {
        Creature* creature=player?ObjectAccessor::GetCreature(*player,guid):nullptr;
        if (!creature || creature->GetScriptName()!="mod_hunts_huntmaster") return;
        // Empty vendor list for blocked/misrouted core opcodes. Creature entries
        // are 24-bit; this sentinel cannot refer to a legitimate template.
        vendorEntry=NativeProofEligible(player,creature)?0:std::numeric_limits<uint32>::max();
    }
    void OnPlayerBeforeBuyItemFromVendor(Player* player,ObjectGuid guid,uint32 vendorSlot,uint32& item,uint8 count,uint8,uint8) override
    {
        Creature* creature=player?ObjectAccessor::GetCreature(*player,guid):nullptr;
        if (!creature || creature->GetScriptName()!="mod_hunts_huntmaster") return;
        if (!NativeProofEligible(player,creature) || vendorSlot!=0 || item!=sHuntCurrency.Vendor().itemEntry
            || count!=1 || player->GetSession()->GetCurrentVendor()!=0)
        {
            item=0; // Core exits before lookup/debit when this hook clears item.
            ChatHandler(player->GetSession()).SendSysMessage("This Huntmaster purchase is unavailable.");
        }
        // The core alone debits ItemExtendedCost. Never Spend/DestroyItem here.
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        if (!player)
            return;
        sHuntMgr.ClearReturnRift(player);
    }
};
}

void AddNativeHuntGameplayScripts()
{
    new HuntmasterScript();
    new HuntGuardLocatorScript();
    new HuntElitePreyScript();
    new HuntActivationScript();
    new HuntReturnRiftScript();
    new HuntPlayerScript();
}
