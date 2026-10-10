/**
 * @file bazaar.hpp
 * @brief Parsing of the Hypixel Bazaar API response (`/v2/skyblock/bazaar`).
 *
 * This module never performs network access: callers provide the response body.
 */
#pragma once

#include "bz/model.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bz {

/// One Bazaar product and its market data.
struct BazaarProduct {
    std::string id;    ///< Normalized item id.
    MarketData market; ///< Prices and weekly volumes.
};

/// A parsed Bazaar API response.
struct BazaarSnapshot {
    std::int64_t last_updated_ms = 0;    ///< Server timestamp, milliseconds since the Unix epoch.
    std::vector<BazaarProduct> products; ///< Products sorted by id.
};

/**
 * @brief Parses the body of a Bazaar API response.
 *
 * Like the Python reference, every product becomes a Bazaar item. A product without
 * `quick_status` keeps zero prices, so pricing.hpp reports no price for it; missing numeric
 * fields default to 0.
 *
 * @param json_text Response body.
 * @return The parsed snapshot.
 * @throws std::runtime_error If the body is not valid JSON, has no `products` object, or
 *         reports `"success": false`.
 */
[[nodiscard]] BazaarSnapshot parse_bazaar(std::string_view json_text);

} // namespace bz
