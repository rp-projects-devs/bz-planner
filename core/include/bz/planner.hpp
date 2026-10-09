/**
 * @file planner.hpp
 * @brief Turns a craft path into an executable plan under order-slot, budget and liquidity limits.
 */
#pragma once

#include "bz/catalog.hpp"
#include "bz/model.hpp"
#include "bz/pricing.hpp"
#include "bz/solver.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace bz {

/// Limits applied when sizing a plan. Unset limits are ignored.
struct Constraints {
    std::optional<int> order_slots;        ///< Bazaar orders available for ingredients.
    std::optional<double> budget;          ///< Coins available for ingredients and NPC costs.
    std::optional<double> liquidity_share; ///< Max share, in (0, 1], of the target's weekly
                                           ///< bought volume (MarketData::buy_moving_week).
};

/// A constraint that prevents crafting one more unit.
enum class Limit {
    OrderSlots, ///< Constraints::order_slots.
    Budget,     ///< Constraints::budget.
    Liquidity,  ///< Constraints::liquidity_share.
};

/// Bazaar orders to place for one ingredient.
struct OrderLine {
    ItemIndex item = 0;                     ///< Ingredient.
    std::int64_t quantity = 0;              ///< Total units, rounded up.
    std::vector<std::int64_t> slot_amounts; ///< Units per order, each <= kMaxItemsPerOrder.
    double unit_price = 0.0;                ///< Price per unit under the PricingConfig.
    double total_cost = 0.0;                ///< quantity × unit_price.
};

/// An executable plan for a craft path.
struct Plan {
    std::int64_t quantity = 0;         ///< Units of the target to craft.
    int slots_used = 0;                ///< Bazaar orders needed.
    double total_cost = 0.0;           ///< Ingredients and NPC coins.
    double total_revenue = 0.0;        ///< Sale of the crafted units, after tax.
    double profit = 0.0;               ///< total_revenue − total_cost.
    std::vector<Limit> binding_limits; ///< Constraints blocking quantity + 1.
    std::vector<OrderLine> orders;     ///< Sorted by decreasing number of slots, then quantity.
};

/**
 * @brief Largest quantity of the target that satisfies every set constraint.
 *
 * Ingredient quantities are rounded up to whole units before checking slots and budget.
 *
 * @return The maximum quantity, possibly 0.
 * @throws std::invalid_argument If no constraint is set (the quantity would be unbounded).
 */
[[nodiscard]] std::int64_t max_quantity(const Catalog& catalog, ItemIndex target,
                                        const CraftPath& path, const PricingConfig& pricing,
                                        const Constraints& constraints);

/**
 * @brief Builds the plan for the largest feasible quantity.
 * @throws std::invalid_argument If no constraint is set.
 */
[[nodiscard]] Plan make_plan(const Catalog& catalog, ItemIndex target, const CraftPath& path,
                             const PricingConfig& pricing, const Constraints& constraints);

} // namespace bz
