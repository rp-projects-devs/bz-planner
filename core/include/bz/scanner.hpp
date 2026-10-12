/**
 * @file scanner.hpp
 * @brief Ranking of the most profitable crafts across the whole catalog.
 */
#pragma once

#include "bz/catalog.hpp"
#include "bz/model.hpp"
#include "bz/planner.hpp"
#include "bz/pricing.hpp"
#include "bz/solver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace bz {

/// Criteria an item must meet to appear in the scan results.
struct ScanFilters {
    std::int64_t min_weekly_volume = 0;  ///< Minimum weekly_sell_volume() of the target.
    double min_margin = 0.0;             ///< Minimum unit_profit / unit_cost (0.05 = 5 %).
    std::optional<double> max_unit_cost; ///< Maximum cost of one craft, in coins.
    std::size_t top = 20;                ///< Maximum number of results.
};

/// One profitable craft.
struct ScanResult {
    ItemIndex item = 0;        ///< Crafted item.
    CraftPath path;            ///< Cheapest craft path.
    double unit_revenue = 0.0; ///< Sale price after tax.
    double unit_profit = 0.0;  ///< unit_revenue − CraftPath::unit_cost.
    double margin = 0.0;       ///< unit_profit / CraftPath::unit_cost.
    std::optional<Plan> plan;  ///< Most profitable plan for @ref path, when scan() sizes plans.
};

/**
 * @brief Finds the most profitable crafts.
 *
 * Every Bazaar item with a recipe and a sale price is evaluated with the paths of best_paths()
 * that have a strictly positive unit profit and meet every filter.
 *
 * Without @p constraints, an item is represented by its cheapest such path and results are
 * ranked by unit profit. With @p constraints, the paths come from plan_paths() and are sized
 * with best_plan(), so an
 * item is represented by the plan earning the most within the player's slots, coins and
 * liquidity; items whose best plan does not make a profit are dropped, and results are ranked by
 * Plan::profit. Ties keep item index order.
 *
 * @param catalog Item graph.
 * @param pricing Trading strategy.
 * @param filters Result filters.
 * @param options Path enumeration limits applied to every item.
 * @param constraints Limits to size a plan for every item, or `std::nullopt` to rank by unit
 *        profit only.
 * @return At most ScanFilters::top results, sorted by decreasing total profit when plans are
 *         sized, by decreasing unit profit otherwise.
 * @throws std::invalid_argument If @p constraints is set but holds no limit.
 */
[[nodiscard]] std::vector<ScanResult>
scan(const Catalog& catalog, const PricingConfig& pricing, const ScanFilters& filters,
     const PathOptions& options = {}, const std::optional<Constraints>& constraints = std::nullopt);

} // namespace bz
