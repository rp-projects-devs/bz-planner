#include "bz/planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace bz {

namespace {

/// Units of one ingredient to buy for @p quantity crafts, rounded up like the reference
/// (`math.ceil(quantity - 1e-9)`, so floating-point noise never adds a unit).
std::int64_t units_to_buy(double amount, std::int64_t quantity) {
    const double units = std::ceil(amount * static_cast<double>(quantity) - 1e-9);
    return units > 0.0 ? static_cast<std::int64_t>(units) : 0;
}

std::int64_t orders_for(std::int64_t units) {
    return (units + kMaxItemsPerOrder - 1) / kMaxItemsPerOrder;
}

/// Evaluates the constraints for any quantity of one path.
class Sizer {
public:
    Sizer(const Catalog& catalog, ItemIndex target, const CraftPath& path,
          const PricingConfig& pricing, const Constraints& constraints)
        : path_(path), constraints_(constraints) {
        if (!constraints.order_slots && !constraints.budget && !constraints.liquidity_share) {
            throw std::invalid_argument("at least one planning constraint must be set");
        }
        prices_.reserve(path.purchases.size());
        for (const Purchase& purchase : path.purchases) {
            prices_.push_back(unit_buy_cost(catalog.item(purchase.item), pricing.buy));
        }
        if (constraints.liquidity_share) {
            const auto& market = catalog.item(target).market;
            const double weekly = market ? static_cast<double>(market->buy_moving_week) : 0.0;
            liquidity_cap_ =
                static_cast<std::int64_t>(std::floor(*constraints.liquidity_share * weekly + 1e-9));
        }
    }

    [[nodiscard]] std::int64_t slots(std::int64_t quantity) const {
        std::int64_t total = 0;
        for (const Purchase& purchase : path_.purchases) {
            total += orders_for(units_to_buy(purchase.amount, quantity));
        }
        return total;
    }

    /// Bazaar cost of the rounded-up ingredients; infinite if an ingredient has no price.
    [[nodiscard]] double cost(std::int64_t quantity) const {
        double total = 0.0;
        for (std::size_t i = 0; i < path_.purchases.size(); ++i) {
            const std::int64_t units = units_to_buy(path_.purchases[i].amount, quantity);
            if (units == 0) {
                continue;
            }
            if (!prices_[i]) {
                return std::numeric_limits<double>::infinity();
            }
            total += static_cast<double>(units) * *prices_[i];
        }
        return total;
    }

    /// Constraints that @p quantity violates, in Limit order.
    [[nodiscard]] std::vector<Limit> violated(std::int64_t quantity) const {
        std::vector<Limit> limits;
        if (constraints_.order_slots && slots(quantity) > *constraints_.order_slots) {
            limits.push_back(Limit::OrderSlots);
        }
        if (constraints_.budget && cost(quantity) > *constraints_.budget) {
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

    /// A quantity that is certainly infeasible, so the binary search can start below it.
    [[nodiscard]] std::int64_t upper_bound() const {
        double bound = std::numeric_limits<double>::infinity();
        double min_amount = std::numeric_limits<double>::infinity();
        double unit_cost = 0.0;
        for (std::size_t i = 0; i < path_.purchases.size(); ++i) {
            min_amount = std::min(min_amount, path_.purchases[i].amount);
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
        // A path without purchases is free and unbounded by slots or budget.
        constexpr double kMaxQuantity = 1e15;
        return static_cast<std::int64_t>(std::min(bound, kMaxQuantity));
    }

private:
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

    for (const Purchase& purchase : path.purchases) {
        OrderLine line{.item = purchase.item,
                       .quantity = units_to_buy(purchase.amount, plan.quantity)};
        if (line.quantity == 0) {
            continue;
        }
        for (std::int64_t left = line.quantity; left > 0; left -= kMaxItemsPerOrder) {
            line.slot_amounts.push_back(std::min(left, kMaxItemsPerOrder));
        }
        const auto price = unit_buy_cost(catalog.item(purchase.item), pricing.buy);
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
