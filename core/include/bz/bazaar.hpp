/**
 * @file bazaar.hpp
 * @brief Parsing of the Hypixel Bazaar API response (`/v2/skyblock/bazaar`) and its compact
 *        on-disk form.
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

/**
 * @brief Serializes a snapshot in the API format, keeping only the fields the library uses.
 *
 * The full API response weighs several megabytes, mostly order books that are never read.
 * The output keeps `success`, `lastUpdated` and, per product, the four `quick_status` fields
 * of MarketData, so it is a few hundred kilobytes and parse_bazaar() reads it back unchanged:
 * `parse_bazaar(serialize_bazaar(s))` yields a snapshot equal to `s`.
 *
 * @param snapshot Snapshot to serialize.
 * @return Compact JSON text.
 */
[[nodiscard]] std::string serialize_bazaar(const BazaarSnapshot& snapshot);

} // namespace bz
