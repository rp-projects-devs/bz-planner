#include "bz/planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace bz {

namespace {

std::int64_t ceil_div(std::int64_t value, std::int64_t divisor) {
    return (value + divisor - 1) / divisor;
}

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

} // namespace bz
