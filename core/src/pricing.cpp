#include "bz/pricing.hpp"

#include <cmath>

namespace bz {

namespace {

std::optional<double> positive(double price) noexcept {
    if (!std::isfinite(price) || price <= 0.0) {
        return std::nullopt;
    }
    return price;
}

} // namespace

std::optional<double> unit_buy_cost(const MarketData& market, BuyMode mode) noexcept {
    return positive(mode == BuyMode::Instant ? market.buy_price : market.sell_price);
}

std::optional<double> unit_sell_revenue(const MarketData& market, SellMode mode,
                                        double tax_rate) noexcept {
    const auto price = positive(mode == SellMode::Instant ? market.sell_price : market.buy_price);
    if (!price) {
        return std::nullopt;
    }
    return *price * (1.0 - tax_rate);
}

std::optional<double> unit_buy_cost(const Item& item, BuyMode mode) noexcept {
    return item.market ? unit_buy_cost(*item.market, mode) : std::nullopt;
}

std::optional<double> unit_sell_revenue(const Item& item, SellMode mode, double tax_rate) noexcept {
    return item.market ? unit_sell_revenue(*item.market, mode, tax_rate) : std::nullopt;
}

std::int64_t weekly_sell_volume(const Item& item, SellMode mode) noexcept {
    if (!item.market) {
        return 0;
    }
    return mode == SellMode::Offer ? item.market->buy_moving_week : item.market->sell_moving_week;
}

} // namespace bz
