#include "bz/planner.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace bz {

namespace {

std::int64_t ceil_div(std::int64_t value, std::int64_t divisor) {
    return (value + divisor - 1) / divisor;
}

/// Quantities searched by plan_paths(), far above what slots, budget or volume allow.
constexpr std::int64_t kMaxSearchedQuantity = 1'000'000'000'000;

/// Ratio between two consecutive quantities of the plan_paths() grid.
constexpr double kGridRatio = 1.5;

/// Evaluates the constraints for any quantity of one path.
class Sizer {
public:
    Sizer(const Catalog& catalog, ItemIndex target, const CraftPath& path,
          const PricingConfig& pricing, const Constraints& constraints)
        : catalog_(catalog), path_(path), constraints_(constraints) {
        if (!constraints.order_slots && !constraints.budget && !constraints.liquidity_share) {
            throw std::invalid_argument("at least one planning constraint must be set");
        }
        if (!std::is_sorted(path.purchases.begin(), path.purchases.end(),
                            [](const Purchase& a, const Purchase& b) { return a.item < b.item; })) {
            throw std::invalid_argument("craft path purchases must be sorted by item");
        }
        if (path.craft && catalog.recipe(path.craft->recipe).output != target) {
            throw std::invalid_argument("craft path does not produce the target");
        }
        prices_.reserve(path.purchases.size());
        for (const Purchase& purchase : path.purchases) {
            prices_.push_back(unit_buy_cost(catalog.item(purchase.item), pricing.buy));
        }
        if (constraints.liquidity_share) {
            const auto weekly =
                static_cast<double>(weekly_sell_volume(catalog.item(target), pricing.sell));
            liquidity_cap_ =
                static_cast<std::int64_t>(std::floor(*constraints.liquidity_share * weekly + 1e-9));
        }
    }

    /**
     * Units of each purchase (same order as CraftPath::purchases) needed for @p quantity units
     * of the target. With a craft tree, every recipe is rounded up to whole crafts, from the
     * target down; otherwise each purchase is rounded up once, like the reference.
     */
    [[nodiscard]] std::vector<std::int64_t> units(std::int64_t quantity,
                                                  std::vector<CraftLine>* crafts = nullptr) const {
        std::vector<std::int64_t> result(path_.purchases.size(), 0);
        if (quantity <= 0) {
            return result;
        }
        if (path_.craft) {
            expand(*path_.craft, quantity, result, crafts);
            return result;
        }
        for (std::size_t i = 0; i < result.size(); ++i) {
            const double needed =
                std::ceil(path_.purchases[i].amount * static_cast<double>(quantity) - 1e-9);
            result[i] = needed > 0.0 ? static_cast<std::int64_t>(needed) : 0;
        }
        return result;
    }

    [[nodiscard]] static std::int64_t slots(const std::vector<std::int64_t>& units) {
        std::int64_t total = 0;
        for (std::int64_t count : units) {
            total += ceil_div(count, kMaxItemsPerOrder);
        }
        return total;
    }

    /// Bazaar cost of @p units; infinite if a needed ingredient has no price.
    [[nodiscard]] double cost(const std::vector<std::int64_t>& units) const {
        double total = 0.0;
        for (std::size_t i = 0; i < units.size(); ++i) {
            if (units[i] == 0) {
                continue;
            }
            if (!prices_[i]) {
                return std::numeric_limits<double>::infinity();
            }
            total += static_cast<double>(units[i]) * *prices_[i];
        }
        return total;
    }

    /// Constraints that @p quantity violates, in Limit order.
    [[nodiscard]] std::vector<Limit> violated(std::int64_t quantity) const {
        const std::vector<std::int64_t> needed = units(quantity);
        std::vector<Limit> limits;
        if (constraints_.order_slots && slots(needed) > *constraints_.order_slots) {
            limits.push_back(Limit::OrderSlots);
        }
        if (constraints_.budget && cost(needed) > *constraints_.budget) {
            limits.push_back(Limit::Budget);
        }
        if (liquidity_cap_ && quantity > *liquidity_cap_) {
            limits.push_back(Limit::Liquidity);
        }
        return limits;
    }

    [[nodiscard]] bool feasible(std::int64_t quantity) const {
        return quantity == 0 || violated(quantity).empty();
    }

    /**
     * A quantity that is certainly infeasible, so the binary search can start below it.
     * Rounding only adds units, so bounds computed with the average amounts are safe.
     */
    [[nodiscard]] std::int64_t upper_bound() const {
        double bound = std::numeric_limits<double>::infinity();
        double min_amount = std::numeric_limits<double>::infinity();
        double total_amount = 0.0;
        double unit_cost = 0.0;
        for (std::size_t i = 0; i < path_.purchases.size(); ++i) {
            min_amount = std::min(min_amount, path_.purchases[i].amount);
            total_amount += path_.purchases[i].amount;
            unit_cost += path_.purchases[i].amount * prices_[i].value_or(0.0);
        }
        if (constraints_.order_slots && std::isfinite(min_amount) && min_amount > 0.0) {
            const double units = static_cast<double>(*constraints_.order_slots) *
                                 static_cast<double>(kMaxItemsPerOrder);
            bound = std::min(bound, std::floor(units / min_amount) + 1.0);
        }
        if (constraints_.budget && unit_cost > 0.0) {
            bound = std::min(bound, std::floor(*constraints_.budget / unit_cost) + 1.0);
        }
        if (liquidity_cap_) {
            bound = std::min(bound, static_cast<double>(*liquidity_cap_) + 1.0);
        }
        // Keeps every unit count far below the int64 range, even for absurd budgets.
        constexpr double kMaxUnits = 1e15;
        bound = std::min(bound, std::floor(kMaxUnits / std::max(1.0, total_amount)));
        return static_cast<std::int64_t>(bound);
    }

private:
    /// Adds the purchases for @p output_units units made by @p step, in whole crafts.
    void expand(const CraftStep& step, std::int64_t output_units, std::vector<std::int64_t>& result,
                std::vector<CraftLine>* crafts) const {
        const Recipe& recipe = catalog_.recipe(step.recipe);
        const std::int64_t executions = ceil_div(output_units, recipe.output_count);
        for (const Ingredient& ingredient : recipe.ingredients) {
            const std::int64_t needed = executions * ingredient.count;
            const auto input =
                std::find_if(step.inputs.begin(), step.inputs.end(), [&](const CraftStep& child) {
                    return catalog_.recipe(child.recipe).output == ingredient.item;
                });
            if (input != step.inputs.end()) {
                expand(*input, needed, result, crafts);
                continue;
            }
            const auto purchase =
                std::lower_bound(path_.purchases.begin(), path_.purchases.end(), ingredient.item,
                                 [](const Purchase& p, ItemIndex item) { return p.item < item; });
            if (purchase == path_.purchases.end() || purchase->item != ingredient.item) {
                throw std::invalid_argument("craft path buys an item missing from its purchases");
            }
            result[static_cast<std::size_t>(purchase - path_.purchases.begin())] += needed;
        }
        if (crafts) {
            const auto line = std::find_if(crafts->begin(), crafts->end(), [&](const CraftLine& l) {
                return l.recipe == step.recipe;
            });
            if (line != crafts->end()) {
                line->crafts += executions;
            } else {
                crafts->push_back({step.recipe, executions});
            }
        }
    }

    const Catalog& catalog_;
    const CraftPath& path_;
    const Constraints& constraints_;
    std::vector<std::optional<double>> prices_;
    std::optional<std::int64_t> liquidity_cap_;
};

/// A way to obtain a given number of units of an item, see PlanSearch.
struct SizedOption {
    std::vector<std::pair<ItemIndex, std::int64_t>> units; ///< Purchases, sorted by item.
    std::optional<CraftStep> craft;                        ///< Empty when the item is bought.
    std::int64_t slots = 0;                                ///< Orders for units.
    double cost = 0.0;                                     ///< Bazaar cost of units.
};

/**
 * Finds, for one quantity of the target, the cheapest craft tree that fits the order slots and
 * the budget, with every recipe rounded to whole crafts.
 *
 * Each item keeps the Pareto front of its options over (slots, cost): the cheapest option for
 * every number of slots, at most order_slots + 1 options. Without a slot limit only the cheapest
 * option is kept. Identical purchases of different branches are merged before counting slots.
 */
class PlanSearch {
public:
    PlanSearch(const Catalog& catalog, const PricingConfig& pricing, const Constraints& constraints)
        : catalog_(catalog), constraints_(constraints), prices_(catalog.items().size()),
          on_branch_(catalog.items().size(), false) {
        for (ItemIndex item = 0; item < prices_.size(); ++item) {
            prices_[item] = unit_buy_cost(catalog.item(item), pricing.buy);
        }
    }

    /// Feasible craft trees for @p quantity units of @p target: the cheapest one for every
    /// number of slots, by increasing cost.
    std::vector<CraftStep> front(ItemIndex target, std::int64_t quantity, int max_depth) {
        std::vector<CraftStep> trees;
        for (SizedOption& option : resolve(target, quantity, max_depth, false)) {
            trees.push_back(std::move(*option.craft));
        }
        return trees;
    }

private:
    std::vector<SizedOption> resolve(ItemIndex item, std::int64_t units, int depth_left,
                                     bool buyable) {
        std::vector<SizedOption> options;
        if (on_branch_[item] || units > kMaxUnits) {
            return options;
        }
        if (buyable && prices_[item]) {
            options.push_back({.units = {{item, units}},
                               .slots = ceil_div(units, kMaxItemsPerOrder),
                               .cost = static_cast<double>(units) * *prices_[item]});
        }
        if (depth_left > 0) {
            on_branch_[item] = true;
            for (RecipeIndex recipe_idx : catalog_.item(item).produced_by) {
                std::vector<SizedOption> crafted = resolve_recipe(recipe_idx, units, depth_left);
                options.insert(options.end(), std::make_move_iterator(crafted.begin()),
                               std::make_move_iterator(crafted.end()));
            }
            on_branch_[item] = false;
        }
        keep_front(options);
        return options;
    }

    std::vector<SizedOption> resolve_recipe(RecipeIndex recipe_idx, std::int64_t units,
                                            int depth_left) {
        const Recipe& recipe = catalog_.recipe(recipe_idx);
        const std::int64_t crafts = ceil_div(units, recipe.output_count);
        std::vector<SizedOption> partial{{.craft = CraftStep{.recipe = recipe_idx}}};

        for (const Ingredient& ingredient : recipe.ingredients) {
            if (crafts > kMaxUnits / ingredient.count) {
                return {};
            }
            const std::vector<SizedOption> inputs =
                resolve(ingredient.item, crafts * ingredient.count, depth_left - 1, true);
            std::vector<SizedOption> combined;
            combined.reserve(partial.size() * inputs.size());
            for (const SizedOption& base : partial) {
                for (const SizedOption& input : inputs) {
                    SizedOption option = merge(base, input);
                    if (input.craft) {
                        option.craft->inputs.push_back(*input.craft);
                    }
                    combined.push_back(std::move(option));
                }
            }
            keep_front(combined);
            if (combined.empty()) {
                return {};
            }
            partial = std::move(combined);
        }
        return partial;
    }

    /// @p base with the purchases of @p input added; slots and cost are recomputed after merging.
    SizedOption merge(const SizedOption& base, const SizedOption& input) const {
        SizedOption merged{.craft = base.craft};
        merged.units.reserve(base.units.size() + input.units.size());
        std::merge(base.units.begin(), base.units.end(), input.units.begin(), input.units.end(),
                   std::back_inserter(merged.units),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
        std::size_t out = 0;
        for (std::size_t i = 0; i < merged.units.size(); ++i) {
            if (out > 0 && merged.units[out - 1].first == merged.units[i].first) {
                merged.units[out - 1].second += merged.units[i].second;
            } else {
                merged.units[out++] = merged.units[i];
            }
        }
        merged.units.resize(out);
        for (const auto& [item, count] : merged.units) {
            merged.slots += ceil_div(count, kMaxItemsPerOrder);
            merged.cost += static_cast<double>(count) * *prices_[item];
        }
        return merged;
    }

    /**
     * Drops infeasible options, then dominated ones. Without a slot limit only the cheapest
     * option matters. With one, A dominates B when A costs no more and buys no more units of any
     * item: merging with the purchases of other branches can then never favor B. Comparing only
     * (slots, cost) would be wrong, since two branches buying the same item share its orders.
     */
    void keep_front(std::vector<SizedOption>& options) const {
        std::erase_if(options, [&](const SizedOption& option) {
            return (constraints_.order_slots && option.slots > *constraints_.order_slots) ||
                   (constraints_.budget && option.cost > *constraints_.budget);
        });
        std::stable_sort(options.begin(), options.end(),
                         [](const SizedOption& a, const SizedOption& b) {
                             return a.cost < b.cost || (a.cost == b.cost && a.slots < b.slots);
                         });
        std::vector<SizedOption> front;
        for (SizedOption& option : options) {
            if (!constraints_.order_slots && !front.empty()) {
                break;
            }
            const bool dominated =
                std::any_of(front.begin(), front.end(),
                            [&](const SizedOption& kept) { return buys_no_more(kept, option); });
            if (!dominated) {
                front.push_back(std::move(option));
            }
            if (front.size() == kMaxFront) {
                break;
            }
        }
        options = std::move(front);
    }

    /// True if every purchase of @p a is also made by @p b, in at least the same quantity.
    static bool buys_no_more(const SizedOption& a, const SizedOption& b) {
        auto it = b.units.begin();
        for (const auto& [item, count] : a.units) {
            it = std::lower_bound(it, b.units.end(), item, [](const auto& entry, ItemIndex value) {
                return entry.first < value;
            });
            if (it == b.units.end() || it->first != item || it->second < count) {
                return false;
            }
        }
        return true;
    }

    /// Options kept per item and quantity, cheapest first; bounds the search on wide recipes.
    static constexpr std::size_t kMaxFront = 256;

    /// Larger unit counts exceed any order slots or budget; also keeps products in range.
    static constexpr std::int64_t kMaxUnits = 1'000'000'000'000'000;

    const Catalog& catalog_;
    const Constraints& constraints_;
    std::vector<std::optional<double>> prices_;
    std::vector<bool> on_branch_;
};

/// Per-unit purchases and cost of a craft tree, as best_paths() reports them.
CraftPath path_from_tree(const Catalog& catalog, CraftStep craft, const PricingConfig& pricing) {
    std::vector<Purchase> purchases;
    const auto add = [&](auto& self, const CraftStep& step, double factor) -> void {
        const Recipe& recipe = catalog.recipe(step.recipe);
        for (const Ingredient& ingredient : recipe.ingredients) {
            const double amount = factor * static_cast<double>(ingredient.count) /
                                  static_cast<double>(recipe.output_count);
            const auto input =
                std::find_if(step.inputs.begin(), step.inputs.end(), [&](const CraftStep& child) {
                    return catalog.recipe(child.recipe).output == ingredient.item;
                });
            if (input != step.inputs.end()) {
                self(self, *input, amount);
                continue;
            }
            const auto it =
                std::lower_bound(purchases.begin(), purchases.end(), ingredient.item,
                                 [](const Purchase& p, ItemIndex item) { return p.item < item; });
            if (it != purchases.end() && it->item == ingredient.item) {
                it->amount += amount;
            } else {
                purchases.insert(it, {ingredient.item, amount});
            }
        }
    };
    add(add, craft, 1.0);
    double unit_cost = 0.0;
    for (const Purchase& purchase : purchases) {
        unit_cost +=
            purchase.amount * unit_buy_cost(catalog.item(purchase.item), pricing.buy).value_or(0.0);
    }
    return {.purchases = std::move(purchases), .craft = std::move(craft), .unit_cost = unit_cost};
}

} // namespace

std::int64_t max_quantity(const Catalog& catalog, ItemIndex target, const CraftPath& path,
                          const PricingConfig& pricing, const Constraints& constraints) {
    const Sizer sizer(catalog, target, path, pricing, constraints);

    // Every constraint is monotonic in the quantity, so the feasible quantities are [0, best].
    std::int64_t low = 0;
    std::int64_t high = sizer.upper_bound();
    while (low < high) {
        const std::int64_t mid = low + (high - low + 1) / 2;
        if (sizer.feasible(mid)) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
}

Plan make_plan(const Catalog& catalog, ItemIndex target, const CraftPath& path,
               const PricingConfig& pricing, const Constraints& constraints) {
    const Sizer sizer(catalog, target, path, pricing, constraints);

    Plan plan;
    plan.quantity = max_quantity(catalog, target, path, pricing, constraints);
    plan.binding_limits = sizer.violated(plan.quantity + 1);

    const std::vector<std::int64_t> units = sizer.units(plan.quantity, &plan.crafts);
    for (std::size_t i = 0; i < units.size(); ++i) {
        OrderLine line{.item = path.purchases[i].item, .quantity = units[i]};
        if (line.quantity == 0) {
            continue;
        }
        for (std::int64_t left = line.quantity; left > 0; left -= kMaxItemsPerOrder) {
            line.slot_amounts.push_back(std::min(left, kMaxItemsPerOrder));
        }
        const auto price = unit_buy_cost(catalog.item(line.item), pricing.buy);
        line.unit_price = price.value_or(std::numeric_limits<double>::infinity());
        line.total_cost = static_cast<double>(line.quantity) * line.unit_price;

        plan.slots_used += static_cast<int>(line.slot_amounts.size());
        plan.total_cost += line.total_cost;
        plan.orders.push_back(std::move(line));
    }
    std::stable_sort(plan.orders.begin(), plan.orders.end(),
                     [](const OrderLine& a, const OrderLine& b) {
                         if (a.slot_amounts.size() != b.slot_amounts.size()) {
                             return a.slot_amounts.size() > b.slot_amounts.size();
                         }
                         return a.quantity > b.quantity;
                     });

    const auto revenue =
        unit_sell_revenue(catalog.item(target), pricing.sell, pricing.tax_rate).value_or(0.0);
    plan.total_revenue = static_cast<double>(plan.quantity) * revenue;
    plan.profit = plan.total_revenue - plan.total_cost;
    return plan;
}

std::optional<BestPlan> best_plan(const Catalog& catalog, ItemIndex target,
                                  std::span<const CraftPath> paths, const PricingConfig& pricing,
                                  const Constraints& constraints) {
    std::optional<BestPlan> best;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        Plan plan = make_plan(catalog, target, paths[i], pricing, constraints);
        if (plan.quantity > 0 && (!best || plan.profit > best->plan.profit)) {
            best = BestPlan{.path_index = i, .plan = std::move(plan)};
        }
    }
    return best;
}

std::vector<CraftPath> plan_paths(const Catalog& catalog, ItemIndex target,
                                  const PricingConfig& pricing, const Constraints& constraints,
                                  const PathOptions& options) {
    const int max_depth = options.max_depth;
    if (!constraints.order_slots && !constraints.budget && !constraints.liquidity_share) {
        throw std::invalid_argument("at least one planning constraint must be set");
    }
    PlanSearch search(catalog, pricing, constraints);
    std::int64_t cap = std::numeric_limits<std::int64_t>::max() / 4;
    if (constraints.liquidity_share) {
        const auto weekly =
            static_cast<double>(weekly_sell_volume(catalog.item(target), pricing.sell));
        cap = static_cast<std::int64_t>(std::floor(*constraints.liquidity_share * weekly + 1e-9));
    }
    const auto feasible = [&](std::int64_t quantity) {
        return quantity <= cap && !search.front(target, quantity, max_depth).empty();
    };
    std::vector<CraftPath> paths = best_paths(catalog, target, pricing, options);
    if (!feasible(1)) {
        return paths;
    }

    // Largest quantity some craft tree allows: feasibility only shrinks as the quantity grows.
    std::int64_t low = 1;
    std::int64_t high = 2;
    while (feasible(high) && high < kMaxSearchedQuantity) {
        low = high;
        high *= 2;
    }
    while (high - low > 1) {
        const std::int64_t mid = low + (high - low) / 2;
        (feasible(mid) ? low : high) = mid;
    }

    // The best tree changes with the quantity (a tree using fewer slots scales further but
    // costs more per unit), so collect the fronts along a geometric grid of quantities.
    for (double quantity = static_cast<double>(low); quantity >= 1.0; quantity /= kGridRatio) {
        for (CraftStep& craft :
             search.front(target, static_cast<std::int64_t>(quantity), max_depth)) {
            CraftPath path = path_from_tree(catalog, std::move(craft), pricing);
            const bool known = std::any_of(paths.begin(), paths.end(), [&](const CraftPath& other) {
                return other.purchases.size() == path.purchases.size() &&
                       std::equal(other.purchases.begin(), other.purchases.end(),
                                  path.purchases.begin(), [](const Purchase& a, const Purchase& b) {
                                      return a.item == b.item &&
                                             std::abs(a.amount - b.amount) <=
                                                 1e-9 * std::max(1.0, std::abs(a.amount));
                                  });
            });
            if (!known) {
                paths.push_back(std::move(path));
            }
        }
    }
    std::stable_sort(paths.begin(), paths.end(), [](const CraftPath& a, const CraftPath& b) {
        return a.unit_cost < b.unit_cost;
    });
    return paths;
}

} // namespace bz
