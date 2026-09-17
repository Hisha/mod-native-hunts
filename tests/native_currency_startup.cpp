// Run with -DNATIVE_HUNTS_MEMORY_DATABASE and tests/currency_adapter headers.
#include "HuntCurrencyService.h"
#include "FakeCore.h"
#include <iostream>

namespace
{
bool latched = false;
std::string state = "NATIVE";
uint32 version = 1, seal = 60001;
QueryResult Row(std::initializer_list<Field> fields)
{
    auto result=std::make_shared<Result>();result->rows.emplace_back(fields);return result;
}
struct Provider final : WorldScript, ContentCapabilitiesV1::Provider
{
    ContentCapabilitiesV1::Result result = ContentCapabilitiesV1::Result::Ready;
    ContentCapabilitiesV1::Result Resolve(std::string const& package,std::string const& symbol,
        std::vector<ContentCapabilitiesV1::Resource>& resources,ContentCapabilitiesV1::Vendor& vendor,
        std::string& reason) const override
    {
        assert(package=="mod-hunts" && symbol=="seal-proof-vendor"); // Persistent ownership key.
        assert(resources.size()==2 && resources[0].symbol=="seal");
        reason="Injected capability result";
        resources[0].value=60001;resources[1].value=77;vendor={123456,40717,77};
        return result;
    }
};
}
int main()
{
    CharacterDatabase.query=[](std::string const& sql)->QueryResult {
        if(sql.find("information_schema.TABLES")!=std::string::npos)return Row({{false,"8"}});
        if(sql.find("SELECT r.state,r.migration_version,r.seal_item") == 0)
            return Row({{!latched,state},{false,std::to_string(version)},{false,std::to_string(seal)}});
        if(sql.find("SELECT r.state FROM") == 0)return Row({{!latched,state}});
        throw std::runtime_error("Unexpected query: "+sql);
    };
    auto& service=sHuntCurrency;
    Player player;player.guid.n=42;player.inventory[60001]=9;
    service.Initialize();assert(!service.Available() && service.GetBalance(&player)==0);
    Provider provider;
    ScriptRegistry<WorldScript>::ScriptPointerList[1]=&provider;
    for(auto result:{ContentCapabilitiesV1::Result::Inactive,ContentCapabilitiesV1::Result::Invalid})
    {
        provider.result=result;service.Initialize();assert(!service.Available());
    }
    provider.result=ContentCapabilitiesV1::Result::Ready;
    manager.items[60001]={200,1,0,0};manager.items[40717]={1,1,0,1};
    Cost cost;cost.reqitem[0]=60001;cost.reqitemcount[0]=5;sItemExtendedCostStore.rows[77]=cost;
    // An already-imported realm requires no writes and retains dynamic identities.
    latched=true;service.Initialize();assert(service.Available() && service.SealItem()==60001);
    assert(service.GetBalance(&player)==9);
    auto blocked=[&] {service.Initialize();assert(!service.Available() && service.GetBalance(&player)==0);};
    sItemExtendedCostStore.rows[77].reqitemcount[0]=4;blocked();sItemExtendedCostStore.rows[77]=cost;
    sItemExtendedCostStore.rows[77].reqitem[1]=2;blocked();sItemExtendedCostStore.rows[77]=cost;
    manager.items[40717].BuyPrice=1;blocked();manager.items[40717].BuyPrice=0;
    manager.items.erase(60001);blocked();manager.items[60001]={0,1,0,0};blocked();manager.items[60001].Stackable=200;
    version=2;blocked();version=1;seal=60002;blocked();seal=60001;
    state="UNKNOWN";blocked();state="NATIVE";
    ObjectAccessor::players[42]=&player;blocked();ObjectAccessor::players.clear();
    Provider duplicate;ScriptRegistry<WorldScript>::ScriptPointerList[2]=&duplicate;blocked();
    ScriptRegistry<WorldScript>::ScriptPointerList.erase(2);
    service.Initialize();assert(service.Available());
    ScriptRegistry<WorldScript>::ScriptPointerList.clear();blocked();
    std::string error;
    assert(!service.CompleteNativeHunt(&player,2,"unused",error) && !error.empty());
    assert(player.mails.empty() && player.items.empty() && CharacterDatabase.transactions==0);
    std::cout<<"PASS native startup: absent/inactive/invalid/duplicate provider, dynamic IDs, physical balance, invalid costs/templates, durable identity/state checks, startup-only guard, no fallback or writes\n";
}
