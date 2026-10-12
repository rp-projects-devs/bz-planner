#include "bz/solver.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <span>
#include <stdexcept>
#include <utility>

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

/// Adds @p factor × @p source to @p target; both are sorted by item, and so is the result.
std::vector<Purchase> merge_purchases(const std::vector<Purchase>& target,
                                      const std::vector<Purchase>& source, double factor) {
    std::vector<Purchase> merged;
    merged.reserve(target.size() + source.size());
    auto a = target.begin();
    auto b = source.begin();
    while (a != target.end() || b != source.end()) {
        if (b == source.end() || (a != target.end() && a->item < b->item)) {
            merged.push_back(*a++);
        } else if (a == target.end() || b->item < a->item) {
            merged.push_back({b->item, b->amount * factor});
            ++b;
        } else {
            merged.push_back({a->item, a->amount + b->amount * factor});
            ++a;
            ++b;
        }
    }
    return merged;
}

bool same_purchases(const std::vector<Purchase>& a, const std::vector<Purchase>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const Purchase& x, const Purchase& y) {
                          return x.item == y.item && std::abs(x.amount - y.amount) <=
                                                         1e-9 * std::max(1.0, std::abs(x.amount));
                      });
}

/// Sorts @p paths by cost, drops paths buying the same amounts as a cheaper one, keeps top_k.
void keep_best(std::vector<CraftPath>& paths, std::size_t top_k) {
    std::stable_sort(paths.begin(), paths.end(), [](const CraftPath& a, const CraftPath& b) {
        return a.unit_cost < b.unit_cost;
    });
    std::vector<CraftPath> kept;
    for (CraftPath& path : paths) {
        if (kept.size() == top_k) {
            break;
        }
        const bool duplicate = std::any_of(kept.begin(), kept.end(), [&](const CraftPath& other) {
            return same_purchases(other.purchases, path.purchases);
        });
        if (!duplicate) {
            kept.push_back(std::move(path));
        }
    }
    paths = std::move(kept);
}

/// Depth-limited enumeration of the cheapest ways to obtain one unit of an item.
class PathSearch {
public:
    PathSearch(const Catalog& catalog, const PricingConfig& pricing, std::size_t top_k)
        : catalog_(catalog), pricing_(pricing), top_k_(top_k),
          on_branch_(catalog.items().size(), false) {}

    /**
     * Ways to obtain one unit of @p item, expanding at most @p depth_left recipe levels.
     *
     * An item already expanded on the current branch yields no option at all: buying it to
     * craft it again costs at least as much as buying it directly, so such a path only wastes
     * a top_k slot. This also keeps the target out of its own purchases.
     */
    std::vector<CraftPath> resolve(ItemIndex item, int depth_left, bool buyable) {
        std::vector<CraftPath> options;
        if (on_branch_[item]) {
            return options;
        }
        if (buyable) {
            if (const auto price = unit_buy_cost(catalog_.item(item), pricing_.buy)) {
                options.push_back({.purchases = {{item, 1.0}}, .unit_cost = *price});
            }
        }
        if (depth_left <= 0) {
            return options;
        }

        on_branch_[item] = true;
        for (RecipeIndex recipe_idx : catalog_.item(item).produced_by) {
            std::vector<CraftPath> crafted = resolve_recipe(recipe_idx, depth_left);
            options.insert(options.end(), std::make_move_iterator(crafted.begin()),
                           std::make_move_iterator(crafted.end()));
        }
        on_branch_[item] = false;

        keep_best(options, top_k_);
        return options;
    }

private:
    /// Ways to obtain one output unit of a recipe, combining the options of its ingredients.
    std::vector<CraftPath> resolve_recipe(RecipeIndex recipe_idx, int depth_left) {
        const Recipe& recipe = catalog_.recipe(recipe_idx);
        std::vector<CraftPath> partial{{.craft = CraftStep{.recipe = recipe_idx}}};

        for (const Ingredient& ingredient : recipe.ingredients) {
            const std::vector<CraftPath> options = resolve(ingredient.item, depth_left - 1, true);
            if (options.empty()) {
                return {};
            }
            const double factor =
                static_cast<double>(ingredient.count) / static_cast<double>(recipe.output_count);

            std::vector<CraftPath> combined;
            combined.reserve(partial.size() * options.size());
            for (const CraftPath& base : partial) {
                for (const CraftPath& option : options) {
                    CraftPath path{.purchases =
                                       merge_purchases(base.purchases, option.purchases, factor),
                                   .craft = base.craft,
                                   .unit_cost = base.unit_cost + factor * option.unit_cost};
                    if (option.craft) {
                        path.craft->inputs.push_back(*option.craft);
                    }
                    combined.push_back(std::move(path));
                }
            }
            keep_best(combined, top_k_);
            partial = std::move(combined);
        }
        return partial;
    }

    const Catalog& catalog_;
    const PricingConfig& pricing_;
    std::size_t top_k_;
    /// Items whose recipes are being expanded on the current branch (cycle guard).
    std::vector<bool> on_branch_;
};

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

std::vector<CraftPath> best_paths(const Catalog& catalog, ItemIndex target,
                                  const PricingConfig& pricing, const PathOptions& options) {
    if (target >= catalog.items().size()) {
        throw std::out_of_range("bz::best_paths: unknown target item");
    }
    if (options.top_k == 0) {
        return {};
    }
    PathSearch search(catalog, pricing, options.top_k);
    return search.resolve(target, options.max_depth, false);
}

} // namespace bz
