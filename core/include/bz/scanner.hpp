/**
 * @file scanner.hpp
 * @brief Ranking of the most profitable crafts across the whole catalog.
 *
 * Every craftable Bazaar item is evaluated independently, which makes the scan
 * embarrassingly parallel; it is parallelized with OpenMP when enabled at build time.
 */
#pragma once

#include "bz/catalog.hpp"
#include "bz/model.hpp"
#include "bz/pricing.hpp"
#include "bz/solver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace bz {

/// Criteria an item must meet to appear in the scan results.
struct ScanFilters {
    std::int64_t min_weekly_volume = 0;  ///< Minimum MarketData::buy_moving_week of the target.
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
};

/**
 * @brief Finds the most profitable crafts.
 * @param catalog Item graph.
 * @param pricing Trading strategy.
 * @param filters Result filters.
 * @param options Path enumeration limits applied to every item.
 * @return At most ScanFilters::top results, sorted by decreasing unit profit.
 */
[[nodiscard]] std::vector<ScanResult> scan(const Catalog& catalog, const PricingConfig& pricing,
                                           const ScanFilters& filters,
                                           const PathOptions& options = {});

} // namespace bz
