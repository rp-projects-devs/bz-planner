#include "bz/scanner.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <utility>

namespace bz {

namespace {

/// Evaluates one item; empty if it is not a profitable craft matching the filters.
std::optional<ScanResult> evaluate(const Catalog& catalog, ItemIndex item,
                                   const PricingConfig& pricing, const ScanFilters& filters,
                                   const PathOptions& options,
                                   const std::optional<Constraints>& constraints) {
    const Item& data = catalog.item(item);
    if (data.produced_by.empty() || !data.market ||
        weekly_sell_volume(data, pricing.sell) < filters.min_weekly_volume) {
        return std::nullopt;
    }
    const auto revenue = unit_sell_revenue(data, pricing.sell, pricing.tax_rate);
    if (!revenue) {
        return std::nullopt;
    }

    std::vector<CraftPath> candidates;
    for (CraftPath& path : constraints ? plan_paths(catalog, item, pricing, *constraints, options)
                                       : best_paths(catalog, item, pricing, options)) {
        const double profit = *revenue - path.unit_cost;
        if (profit > 0.0 && path.unit_cost > 0.0 && profit / path.unit_cost >= filters.min_margin &&
            (!filters.max_unit_cost || path.unit_cost <= *filters.max_unit_cost)) {
            candidates.push_back(std::move(path));
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }

    std::size_t chosen = 0; // paths are sorted by cost: the first one has the best unit profit
    std::optional<Plan> plan;
    if (constraints) {
        auto best = best_plan(catalog, item, candidates, pricing, *constraints);
        if (!best || best->plan.profit <= 0.0) {
            return std::nullopt;
        }
        chosen = best->path_index;
        plan = std::move(best->plan);
    }
    CraftPath& path = candidates[chosen];
    const double profit = *revenue - path.unit_cost;
    const double margin = profit / path.unit_cost;
    return ScanResult{.item = item,
                      .path = std::move(path),
                      .unit_revenue = *revenue,
                      .unit_profit = profit,
                      .margin = margin,
                      .plan = std::move(plan)};
}

/// Total profit of the plan when sized, unit profit otherwise.
double ranking_profit(const ScanResult& result) {
    return result.plan ? result.plan->profit : result.unit_profit;
}

} // namespace

std::vector<ScanResult> scan(const Catalog& catalog, const PricingConfig& pricing,
                             const ScanFilters& filters, const PathOptions& options,
                             const std::optional<Constraints>& constraints) {
    // One slot per item: each iteration writes only to its own slot. Sizing plans dominates the
    // run time (bench/results/scan-threads.md) and varies a lot between items, hence dynamic
    // scheduling; ranking by unit profit only is too fast to gain from threads. The loop index is
    // signed because MSVC only implements OpenMP 2.0.
    const std::size_t count = catalog.items().size();
    std::vector<std::optional<ScanResult>> evaluated(count);
    std::vector<std::exception_ptr> errors(count);
    const auto signed_count = static_cast<std::ptrdiff_t>(count);
#pragma omp parallel for schedule(dynamic, 1) if (constraints.has_value())
    for (std::ptrdiff_t index = 0; index < signed_count; ++index) {
        const auto item = static_cast<ItemIndex>(index);
        try {
            evaluated[item] = evaluate(catalog, item, pricing, filters, options, constraints);
        } catch (...) { // no exception may escape an OpenMP region
            errors[item] = std::current_exception();
        }
    }
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }

    std::vector<ScanResult> results;
    for (std::optional<ScanResult>& result : evaluated) {
        if (result) {
            results.push_back(std::move(*result));
        }
    }
    std::stable_sort(results.begin(), results.end(), [](const ScanResult& a, const ScanResult& b) {
        return ranking_profit(a) > ranking_profit(b);
    });
    if (results.size() > filters.top) {
        results.resize(filters.top);
    }
    return results;
}

} // namespace bz
