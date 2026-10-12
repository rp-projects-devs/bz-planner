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
#include <span>
#include <vector>

namespace bz {

/// Limits applied when sizing a plan. Unset limits are ignored.
struct Constraints {
    std::optional<int> order_slots;        ///< Bazaar orders available for ingredients.
    std::optional<double> budget;          ///< Coins available for Bazaar ingredients.
    std::optional<double> liquidity_share; ///< Max share, in (0, 1], of the target's weekly
                                           ///< sales volume (weekly_sell_volume()).
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

/// Number of times one recipe of the path is executed.
struct CraftLine {
    RecipeIndex recipe = 0;  ///< Recipe executed.
    std::int64_t crafts = 0; ///< Executions; each yields Recipe::output_count units.
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
    std::vector<CraftLine> crafts;     ///< Ingredients before the items made from them, target
                                       ///< last; empty for a path without CraftPath::craft.
};

/**
 * @brief Largest quantity of the target that satisfies every set constraint.
 *
 * The units to buy for a quantity `q` follow the craft tree (CraftPath::craft) from the target
 * down: a recipe needed for `n` units runs `ceil(n / output_count)` times and consumes whole
 * ingredients, so leftovers of multi-output recipes are paid for. A path without a craft tree
 * buys `ceil(amount × q)` units of each purchase, like get_max_craftable_quantity() of the
 * Python reference, which can underestimate when a nested recipe yields several units.
 *
 * Each purchase is split into orders of at most kMaxItemsPerOrder units. `q` is feasible when
 * those orders fit in order_slots, their cost under PricingConfig::buy fits in budget (an
 * ingredient without a price exceeds any budget), and `q <= floor(liquidity_share × weekly
 * volume)`, with weekly_sell_volume() of the target under PricingConfig::sell (0 if the target
 * is not on the Bazaar). Every limit grows with `q`, so the result is found by binary search.
 *
 * @param catalog Item graph.
 * @param target Crafted item, used for the liquidity limit.
 * @param path Craft path to size, usually from best_paths().
 * @param pricing Trading strategy.
 * @param constraints Limits; unset limits are ignored.
 * @return The maximum quantity, possibly 0.
 * @throws std::invalid_argument If no constraint is set (the quantity would be unbounded), or
 *         if @p path is inconsistent (unsorted purchases, craft tree for another item or
 *         buying an item missing from CraftPath::purchases).
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
 * @throws std::invalid_argument In the same cases as max_quantity().
 */
[[nodiscard]] Plan make_plan(const Catalog& catalog, ItemIndex target, const CraftPath& path,
                             const PricingConfig& pricing, const Constraints& constraints);

/// The most profitable plan among several craft paths, see best_plan().
struct BestPlan {
    std::size_t path_index = 0; ///< Index of the planned path in the input span.
    Plan plan;                  ///< Plan built by make_plan() for that path.
};

/**
 * @brief Plans every path and keeps the one with the highest total profit.
 *
 * The cheapest path per unit is not always the best under limits: a path with more distinct
 * ingredients uses more order slots. Like get_best_paths_limited() in the Python reference,
 * every path is sized and the highest Plan::profit wins; ties keep the earlier path.
 *
 * @param catalog Item graph.
 * @param target Crafted item.
 * @param paths Candidate paths, usually from plan_paths().
 * @param pricing Trading strategy.
 * @param constraints Limits; unset limits are ignored.
 * @return The best plan, or `std::nullopt` if no path allows a positive quantity.
 * @throws std::invalid_argument In the same cases as max_quantity().
 */
[[nodiscard]] std::optional<BestPlan> best_plan(const Catalog& catalog, ItemIndex target,
                                                std::span<const CraftPath> paths,
                                                const PricingConfig& pricing,
                                                const Constraints& constraints);

/**
 * @brief Craft paths worth sizing under constraints: best_paths() plus a plan-aware search.
 *
 * best_paths() ranks paths by unit cost, but under order slots a dearer path can earn more: for
 * instance buying enchanted ingredients instead of 160 times more raw items uses far fewer
 * orders, and such a path can rank far beyond PathOptions::top_k. This function therefore adds
 * the paths found by a search on whole quantities. For a quantity `q`, the search finds every
 * craft tree, rounded to whole crafts, whose orders fit in order_slots and budget and that is
 * not beaten by a cheaper tree buying no more units of any item (so that merging the purchases
 * of two branches into the same orders is accounted for); at most 256 options are kept per item
 * and quantity. It finds the largest feasible `q`, also capped by liquidity_share, then collects
 * these trees at every quantity of a geometric grid (ratio 1.5) down to 1. Pass the result to
 * best_plan().
 *
 * Items follow the same rules as best_paths(): bought or crafted up to PathOptions::max_depth
 * recipe levels, never needed again inside their own recipe tree. On the full NEU repository
 * and two Bazaar snapshots, best_plan() over these paths matched best_plan() over every path
 * (exhaustive enumeration) for every profitable item.
 *
 * @param catalog Item graph.
 * @param target Crafted item.
 * @param pricing Trading strategy.
 * @param constraints Limits; at least one must be set.
 * @param options Path enumeration limits.
 * @return Distinct paths sorted by unit cost; empty if the target cannot be crafted.
 * @throws std::invalid_argument If no constraint is set.
 */
[[nodiscard]] std::vector<CraftPath> plan_paths(const Catalog& catalog, ItemIndex target,
                                                const PricingConfig& pricing,
                                                const Constraints& constraints,
                                                const PathOptions& options = {});

} // namespace bz
