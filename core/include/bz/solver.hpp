/**
 * @file solver.hpp
 * @brief Cheapest acquisition cost of every item and ranking of craft paths.
 */
#pragma once

#include "bz/catalog.hpp"
#include "bz/model.hpp"
#include "bz/pricing.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace bz {

/// Cheapest known way to obtain one unit of an item.
struct AcquisitionCost {
    double unit_cost = 0.0;            ///< Coins per unit.
    std::optional<RecipeIndex> recipe; ///< Recipe to execute; empty means "buy on the Bazaar".
};

/// Minimal acquisition costs indexed by ItemIndex; an empty entry means "unobtainable".
using CostTable = std::vector<std::optional<AcquisitionCost>>;

/// Bazaar quantity to buy for one crafted unit.
struct Purchase {
    ItemIndex item = 0;  ///< Item bought on the Bazaar.
    double amount = 0.0; ///< Units per crafted unit; fractional when a recipe yields several.
};

/**
 * @brief A complete way to craft one unit of a target, expanded down to Bazaar purchases.
 */
struct CraftPath {
    std::vector<Purchase> purchases; ///< Merged per item, sorted by item index.
    std::vector<RecipeIndex> steps;  ///< Recipes executed, from the target downwards.
    double unit_cost = 0.0;          ///< Bazaar cost per crafted unit under the PricingConfig.
};

/// Limits of the path enumeration.
struct PathOptions {
    int max_depth = 3;      ///< Maximum number of recipe levels expanded below the target.
    std::size_t top_k = 10; ///< Number of paths kept, at the target and at every level.
};

/**
 * @brief Computes the cheapest acquisition cost of every item.
 *
 * An item costs the minimum of its Bazaar buy price and, for each crafting recipe, the cost
 * of its ingredients divided by the output count. Only Bazaar prices are used: an item that
 * is neither on the Bazaar nor craftable from Bazaar items is unobtainable. A recipe with an
 * unobtainable ingredient is skipped; it never counts that ingredient as free.
 *
 * Components are processed in dependency order (see Catalog::component()), so the ingredients
 * of an item are final before the item itself, except inside a crafting cycle. A single-item
 * component needs one pass; a cycle is relaxed until no cost improves or @p max_iterations
 * passes have run. A recipe replaces the current cost only if it is strictly cheaper, so ties
 * keep the Bazaar purchase and a self-loop recipe never lowers a cost. Every recipe is
 * evaluated once per pass: the total work is linear in the number of ingredient entries.
 *
 * @param catalog Item graph.
 * @param pricing Trading strategy; only PricingConfig::buy is used.
 * @param max_iterations Upper bound on relaxation passes for each crafting cycle.
 * @return One entry per item, indexed by ItemIndex; an empty entry means "unobtainable".
 */
[[nodiscard]] CostTable compute_min_costs(const Catalog& catalog, const PricingConfig& pricing,
                                          int max_iterations = 64);

/**
 * @brief Enumerates the cheapest ways to craft a target.
 *
 * The target is always crafted: buying it is not a craft path, and it is never bought as an
 * ingredient of itself either. Each ingredient is bought or, up to PathOptions::max_depth
 * recipe levels below the target (1 = the target's recipes with bought ingredients), crafted
 * recursively. An item already being expanded on the current branch is only bought, so cycles
 * are never followed. A recipe with an ingredient that can be neither bought nor crafted is
 * skipped, like in compute_min_costs().
 *
 * Only the PathOptions::top_k cheapest options of every item and of every partial recipe are
 * kept. Costs add up, so this pruning never loses one of the top_k cheapest complete paths.
 * Paths buying the same amounts as a cheaper one are dropped, so returned paths are distinct.
 * Matches get_crafting_paths() of the Python reference, ranked by get_path_cost().
 *
 * @param catalog Item graph.
 * @param target Item to craft.
 * @param pricing Trading strategy; only PricingConfig::buy is used.
 * @param options Enumeration limits.
 * @return At most PathOptions::top_k paths, sorted by increasing CraftPath::unit_cost; empty if
 *         the target has no recipe made of obtainable items or if max_depth < 1.
 * @throws std::out_of_range If @p target is not a valid item index.
 */
[[nodiscard]] std::vector<CraftPath> best_paths(const Catalog& catalog, ItemIndex target,
                                                const PricingConfig& pricing,
                                                const PathOptions& options = {});

} // namespace bz
