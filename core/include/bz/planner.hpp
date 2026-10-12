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
    std::optional<double> budget;          ///< Coins available for Bazaar ingredients.
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
    double total_cost = 0.0;           ///< Bazaar cost of the ingredients.
    double total_revenue = 0.0;        ///< Sale of the crafted units, after tax.
    double profit = 0.0;               ///< total_revenue − total_cost.
    std::vector<Limit> binding_limits; ///< Constraints blocking quantity + 1.
    std::vector<OrderLine> orders;     ///< Sorted by decreasing number of slots, then quantity.
};

/**
 * @brief Largest quantity of the target that satisfies every set constraint.
 *
 * For a quantity `q`, each purchase needs `ceil(amount × q)` units, split into orders of at
 * most kMaxItemsPerOrder units. `q` is feasible when those orders fit in order_slots, their
 * cost under PricingConfig::buy fits in budget (an ingredient without a price exceeds any
 * budget), and `q <= floor(liquidity_share × buy_moving_week)` of the target (0 if the target
 * is not on the Bazaar). Every limit grows with `q`, so the result is found by binary search.
 * Matches get_max_craftable_quantity() of the Python reference, plus the liquidity limit.
 *
 * @param catalog Item graph.
 * @param target Crafted item, used for the liquidity limit.
 * @param path Craft path to size, usually from best_paths().
 * @param pricing Trading strategy; only PricingConfig::buy is used.
 * @param constraints Limits; unset limits are ignored.
 * @return The maximum quantity, possibly 0.
 * @throws std::invalid_argument If no constraint is set (the quantity would be unbounded).
 */
[[nodiscard]] std::int64_t max_quantity(const Catalog& catalog, ItemIndex target,
                                        const CraftPath& path, const PricingConfig& pricing,
                                        const Constraints& constraints);

/**
 * @brief Builds the plan for the largest feasible quantity, see max_quantity().
 *
 * Costs use the rounded-up ingredient quantities. The revenue is the quantity times
 * unit_sell_revenue() of the target, or 0 if the target cannot be sold on the Bazaar. Purchases
 * rounding to 0 units get no order line. Plan::binding_limits lists the constraints that
 * quantity + 1 violates, in Limit order.
 *
 * @param catalog Item graph.
 * @param target Crafted item.
 * @param path Craft path to size, usually from best_paths().
 * @param pricing Trading strategy.
 * @param constraints Limits; unset limits are ignored.
 * @return The plan; Plan::quantity may be 0, with no order.
 * @throws std::invalid_argument If no constraint is set.
 */
[[nodiscard]] Plan make_plan(const Catalog& catalog, ItemIndex target, const CraftPath& path,
                             const PricingConfig& pricing, const Constraints& constraints);

} // namespace bz
