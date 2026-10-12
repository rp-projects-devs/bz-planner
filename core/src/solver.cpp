#include "bz/solver.hpp"

#include <span>
#include <stdexcept>

namespace bz {

namespace {

/// Cost of one output unit of @p recipe, or `std::nullopt` if an ingredient is unobtainable.
std::optional<double> recipe_unit_cost(const Recipe& recipe, const CostTable& table) {
    double total = 0.0;
    for (const Ingredient& ingredient : recipe.ingredients) {
        const std::optional<AcquisitionCost>& cost = table[ingredient.item];
        if (!cost) {
            return std::nullopt;
        }
        total += static_cast<double>(ingredient.count) * cost->unit_cost;
    }
    return total / static_cast<double>(recipe.output_count);
}

/// Lowers table[item] with every recipe producing it; returns whether it changed.
bool relax_item(const Catalog& catalog, ItemIndex item, CostTable& table) {
    bool improved = false;
    for (RecipeIndex recipe_idx : catalog.item(item).produced_by) {
        const std::optional<double> cost = recipe_unit_cost(catalog.recipe(recipe_idx), table);
        if (!cost) {
            continue;
        }
        std::optional<AcquisitionCost>& best = table[item];
        if (!best || *cost < best->unit_cost) {
            best = AcquisitionCost{.unit_cost = *cost, .recipe = recipe_idx};
            improved = true;
        }
    }
    return improved;
}

} // namespace

CostTable compute_min_costs(const Catalog& catalog, const PricingConfig& pricing,
                            int max_iterations) {
    CostTable table(catalog.items().size());
    for (std::size_t c = 0; c < catalog.component_count(); ++c) {
        const std::span<const ItemIndex> members = catalog.component(c);
        for (ItemIndex item : members) {
            if (const auto price = unit_buy_cost(catalog.item(item), pricing.buy)) {
                table[item] = AcquisitionCost{.unit_cost = *price};
            }
        }

        if (members.size() == 1) {
            relax_item(catalog, members.front(), table);
            continue;
        }
        bool improved = true;
        for (int pass = 0; improved && pass < max_iterations; ++pass) {
            improved = false;
            for (ItemIndex item : members) {
                improved |= relax_item(catalog, item, table);
            }
        }
    }
    return table;
}

// TODO(v1): expand the target's recipes recursively, keeping only the top_k
// cheapest partial paths per item and depth, and skipping items already on the
// current branch (cycles).
std::vector<CraftPath> best_paths([[maybe_unused]] const Catalog& catalog,
                                  [[maybe_unused]] ItemIndex target,
                                  [[maybe_unused]] const PricingConfig& pricing,
                                  [[maybe_unused]] const PathOptions& options) {
    throw std::logic_error("bz::best_paths is not implemented yet");
}

} // namespace bz
