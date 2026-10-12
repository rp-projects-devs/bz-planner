/**
 * @file commands.hpp
 * @brief Implementation of the `bz` sub-commands, independent of argument parsing.
 *
 * Each command returns a process exit code and lets exceptions propagate to main().
 */
#pragma once

#include "bz/planner.hpp"
#include "bz/pricing.hpp"
#include "bz/scanner.hpp"
#include "bz/solver.hpp"

#include "display.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace bz::app {

/// Options shared by every command.
struct GlobalOptions {
    std::filesystem::path data_dir;          ///< Holds `neu/` and `cache/`.
    PricingConfig pricing;                   ///< Trading strategy.
    std::chrono::seconds cache_max_age{600}; ///< Age beyond which cached prices trigger a warning.
    Style style;                             ///< Output styling.
};

/// Options of `bz scan`.
struct ScanOptions {
    ScanFilters filters;                    ///< Result filters.
    PathOptions paths;                      ///< Path enumeration limits.
    std::optional<Constraints> constraints; ///< When set, rank by the profit of a sized plan.
};

/// Options of `bz item`.
struct ItemOptions {
    std::string id;    ///< Item to inspect.
    PathOptions paths; ///< Path enumeration limits; max_depth also bounds the recipe tree.
};

/// Options of `bz plan`.
struct PlanOptions {
    std::string id;          ///< Item to craft.
    Constraints constraints; ///< Sizing limits.
    PathOptions paths;       ///< Path enumeration limits.
};

/// Ranks the most profitable crafts.
int run_scan(const GlobalOptions& global, const ScanOptions& options);

/// Shows an item's market data, recipe tree and best craft paths.
int run_item(const GlobalOptions& global, const ItemOptions& options);

/// Builds an execution plan for the best craft path of an item.
int run_plan(const GlobalOptions& global, const PlanOptions& options);

/// Downloads fresh Bazaar prices, caches them and reports the state of local data.
/// This is the only command that accesses the network.
int run_update(const GlobalOptions& global);

/// Rebuilds the recipe cache from the NEU repository. Run by the `neu-update` build target.
int run_neu_cache(const GlobalOptions& global);

} // namespace bz::app
