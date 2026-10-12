#include "bz/scanner.hpp"

#include <algorithm>
#include <utility>

namespace bz {

namespace {

/// Evaluates one item; empty if it is not a profitable craft matching the filters.
std::optional<ScanResult> evaluate(const Catalog& catalog, ItemIndex item,
                                   const PricingConfig& pricing, const ScanFilters& filters,
                                   const PathOptions& options) {
    const Item& data = catalog.item(item);
    if (data.produced_by.empty() || !data.market ||
        data.market->buy_moving_week < filters.min_weekly_volume) {
        return std::nullopt;
    }
    const auto revenue = unit_sell_revenue(data, pricing.sell, pricing.tax_rate);
    if (!revenue) {
        return std::nullopt;
    }

    std::vector<CraftPath> paths = best_paths(catalog, item, pricing, options);
    if (paths.empty()) {
        return std::nullopt;
    }
    CraftPath& cheapest = paths.front();
    const double profit = *revenue - cheapest.unit_cost;
    if (profit <= 0.0 || cheapest.unit_cost <= 0.0 ||
        (filters.max_unit_cost && cheapest.unit_cost > *filters.max_unit_cost)) {
        return std::nullopt;
    }
    const double margin = profit / cheapest.unit_cost;
    if (margin < filters.min_margin) {
        return std::nullopt;
    }
    return ScanResult{.item = item,
                      .path = std::move(cheapest),
                      .unit_revenue = *revenue,
                      .unit_profit = profit,
                      .margin = margin};
}

} // namespace

std::vector<ScanResult> scan(const Catalog& catalog, const PricingConfig& pricing,
                             const ScanFilters& filters, const PathOptions& options) {
    // Sequential on purpose: the whole scan takes under 1 ms, and an OpenMP loop gained nothing
    // measurable on the full command (bench/results/scan-threads.md).
    std::vector<ScanResult> results;
    for (ItemIndex item = 0; item < catalog.items().size(); ++item) {
        if (auto result = evaluate(catalog, item, pricing, filters, options)) {
            results.push_back(std::move(*result));
        }
    }
    std::stable_sort(results.begin(), results.end(), [](const ScanResult& a, const ScanResult& b) {
        return a.unit_profit > b.unit_profit;
    });
    if (results.size() > filters.top) {
        results.resize(filters.top);
    }
    return results;
}

} // namespace bz
