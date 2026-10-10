/**
 * @file model.hpp
 * @brief Value types shared by the whole library: items, recipes and market data.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bz {

/// Dense index of an item inside a Catalog.
using ItemIndex = std::size_t;

/// Dense index of a recipe inside a Catalog.
using RecipeIndex = std::size_t;

/// Maximum number of units a single Bazaar order can hold.
inline constexpr std::int64_t kMaxItemsPerOrder = 71'680;

/// Number of Bazaar order slots available to a player by default.
inline constexpr int kDefaultOrderSlots = 21;

/**
 * @brief Bazaar `quick_status` of a product.
 *
 * Fields keep the Hypixel API naming, which is written from the order book's point of view:
 * - `buy_price` is the lowest sell offer, i.e. what a player pays to buy instantly;
 * - `sell_price` is the highest buy order, i.e. what a player receives when selling instantly.
 *
 * Use the helpers of pricing.hpp instead of reading these fields directly.
 */
struct MarketData {
    double buy_price = 0.0;            ///< `quick_status.buyPrice`.
    double sell_price = 0.0;           ///< `quick_status.sellPrice`.
    std::int64_t buy_moving_week = 0;  ///< `quick_status.buyMovingWeek`: units bought over 7 days.
    std::int64_t sell_moving_week = 0; ///< `quick_status.sellMovingWeek`: units sold over 7 days.

    friend bool operator==(const MarketData&, const MarketData&) = default;
};

/// One input of a recipe.
struct Ingredient {
    ItemIndex item = 0;     ///< Consumed item.
    std::int64_t count = 0; ///< Units consumed per craft.

    friend bool operator==(const Ingredient&, const Ingredient&) = default;
};

/**
 * @brief A crafting table recipe: a way to obtain an item from other items.
 *
 * Only crafting grids are modeled, like the Python reference: NPC shop offers, forge
 * recipes and every other acquisition method are never part of the item graph, so no
 * NPC price can leak into a cost.
 *
 * Ingredients are merged per item and sorted by item index once stored in a Catalog,
 * so two equivalent recipes always compare equal.
 */
struct Recipe {
    ItemIndex output = 0;                ///< Produced item.
    std::int64_t output_count = 1;       ///< Units produced per craft.
    std::vector<Ingredient> ingredients; ///< Item inputs.

    friend bool operator==(const Recipe&, const Recipe&) = default;
};

/// A node of the item graph.
struct Item {
    std::string id;                       ///< Normalized id, see normalize_item_id().
    std::optional<MarketData> market;     ///< Bazaar data; empty if not sold on the Bazaar.
    std::vector<RecipeIndex> produced_by; ///< Recipes whose output is this item.
    std::vector<RecipeIndex> used_in;     ///< Recipes consuming this item.
};

} // namespace bz
