/**
 * @file pricing.hpp
 * @brief Conversion of Bazaar market data into unit costs and revenues.
 *
 * | Mode               | Price used              | API field               |
 * |--------------------|-------------------------|-------------------------|
 * | BuyMode::Instant   | lowest sell offer       | MarketData::buy_price   |
 * | BuyMode::Order     | highest buy order       | MarketData::sell_price  |
 * | SellMode::Instant  | highest buy order − tax | MarketData::sell_price  |
 * | SellMode::Offer    | lowest sell offer − tax | MarketData::buy_price   |
 */
#pragma once

#include "bz/model.hpp"

#include <optional>

namespace bz {

/// How ingredients are bought.
enum class BuyMode {
    Instant, ///< Buy instantly from existing sell offers.
    Order,   ///< Place a buy order and wait for it to fill.
};

/// How the crafted item is sold.
enum class SellMode {
    Instant, ///< Sell instantly into existing buy orders.
    Offer,   ///< Place a sell offer and wait for it to fill.
};

/// In-game Bazaar sales tax (1.25 %), to pass explicitly with `--tax`.
inline constexpr double kBazaarTax = 0.0125;

/**
 * @brief Trading strategy used for every cost and revenue computation.
 *
 * The defaults reproduce the Python reference exactly: buy orders for ingredients
 * (`price_type="buy"` → `sellPrice`), sell offers for the output (`price_type="sell"` →
 * `buyPrice`) and no tax.
 */
struct PricingConfig {
    BuyMode buy = BuyMode::Order;    ///< Ingredient side.
    SellMode sell = SellMode::Offer; ///< Output side.
    double tax_rate = 0.0;           ///< Fraction of revenue lost on sales, in [0, 1).
};

/**
 * @brief Price paid for one unit.
 * @return The unit cost, or `std::nullopt` if the relevant price is not strictly positive.
 */
[[nodiscard]] std::optional<double> unit_buy_cost(const MarketData& market, BuyMode mode) noexcept;

/**
 * @brief Coins received for one unit, after tax.
 * @return The unit revenue, or `std::nullopt` if the relevant price is not strictly positive.
 */
[[nodiscard]] std::optional<double> unit_sell_revenue(const MarketData& market, SellMode mode,
                                                      double tax_rate) noexcept;

/// Overload for items, returning `std::nullopt` when the item is not on the Bazaar.
[[nodiscard]] std::optional<double> unit_buy_cost(const Item& item, BuyMode mode) noexcept;

/// Overload for items, returning `std::nullopt` when the item is not on the Bazaar.
[[nodiscard]] std::optional<double> unit_sell_revenue(const Item& item, SellMode mode,
                                                      double tax_rate) noexcept;

} // namespace bz
