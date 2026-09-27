#ifndef CONTENT_RESOURCE_API_V1_H
#define CONTENT_RESOURCE_API_V1_H

#include <cstdint>
#include <string>

namespace ContentResourcesV1 {
enum class Result { Inactive, Ready, Invalid };

class Provider {
public:
	virtual ~Provider() = default;
	virtual Result ResolveResource(std::string const &package,
								   std::string const &symbol,
								   std::string const &kind,
								   std::uint32_t &value,
								   std::string &reason) const = 0;
};
} // namespace ContentResourcesV1

#endif
