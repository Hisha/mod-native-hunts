#ifndef HUNT_CURRENCY_SERVICE_H
#define HUNT_CURRENCY_SERVICE_H
#include "api/ContentCapabilityApiV1.h"
#include <cstdint>
#include <string>
class Player;
// Native currency is available only after resource validation and the one-time
// realm import complete at startup. Failure never enables another economy.
class HuntCurrencyService
{
public:
    static HuntCurrencyService& Instance();
    void Initialize();
    bool Available() const { return _ready; }
    std::uint32_t GetBalance(Player const* player) const;
    // Native award, stats, and removal of the outstanding hunt commit together.
    bool CompleteNativeHunt(Player*,std::uint32_t amount,std::string const& statsSql,std::string& error);
    ContentCapabilitiesV1::Vendor const& Vendor() const { return _vendor; }
    std::uint32_t SealItem() const { return _seal; }
    std::string const& Reason() const { return _reason; }
private:
    bool _ready=false;
    std::uint32_t _seal=0;
    ContentCapabilitiesV1::Vendor _vendor;
    std::string _reason="Currency service has not initialized";
    bool Migrate();
    void Fail(std::string const& reason);
};
#define sHuntCurrency HuntCurrencyService::Instance()
#endif
