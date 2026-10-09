#include "bz/catalog.hpp"
#include "bz/planner.hpp"
#include "bz/pricing.hpp"
#include "bz/scanner.hpp"
#include "bz/solver.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <stdexcept>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// Instant buy and instant sell without tax, so expected values read directly from prices.
constexpr bz::PricingConfig kRawPrices{bz::BuyMode::Instant, bz::SellMode::Instant, 0.0};

bz::BazaarProduct product(const char* id, double buy, double sell,
                          std::int64_t weekly = 1'000'000) {
    return {id, bz::MarketData{buy, sell, weekly, weekly}};
}

/**
 * Small synthetic economy:
 * - MID  = 4 RAW               → 40 crafted vs 50 on the Bazaar
 * - TOP  = 3 MID + 1 RAW       → 130 crafted vs 200 on the Bazaar
 * - BLOCK = 9 RAW, 1 BLOCK → 9 RAW (cycle)
 * - SHARD: NPC sells 2 for 30 coins
 * - WIDGET = 1 GHOST, GHOST is unobtainable
 */
bz::Catalog synthetic_world() {
    bz::BazaarSnapshot bazaar;
    bazaar.products = {product("RAW", 10, 9), product("MID", 50, 45),
                       product("TOP", 200, 180, 1'000), product("BLOCK", 95, 85)};

    const std::vector<bz::NeuRecipe> recipes{
        {.output = "MID", .ingredients = {{"RAW", 4}}},
        {.output = "TOP", .ingredients = {{"MID", 3}, {"RAW", 1}}},
        {.output = "BLOCK", .ingredients = {{"RAW", 9}}},
        {.output = "RAW", .output_count = 9, .ingredients = {{"BLOCK", 1}}},
        {.kind = bz::RecipeKind::NpcShop, .output = "SHARD", .output_count = 2, .coin_cost = 30},
        {.output = "WIDGET", .ingredients = {{"GHOST", 1}}},
    };
    return bz::Catalog::build(bazaar, recipes);
}

bz::ItemIndex index_of(const bz::Catalog& catalog, const char* id) {
    const auto index = catalog.find(id);
    REQUIRE(index);
    return *index;
}

} // namespace

TEST_CASE("Buy modes map to the right side of the order book", "[pricing]") {
    const bz::MarketData market{.buy_price = 493.4, .sell_price = 459.8};
    CHECK_THAT(*bz::unit_buy_cost(market, bz::BuyMode::Instant), WithinRel(493.4));
    CHECK_THAT(*bz::unit_buy_cost(market, bz::BuyMode::Order), WithinRel(459.8));
}

TEST_CASE("Sell modes map to the right side of the order book and pay tax", "[pricing]") {
    const bz::MarketData market{.buy_price = 100.0, .sell_price = 80.0};
    CHECK_THAT(*bz::unit_sell_revenue(market, bz::SellMode::Instant, 0.0125), WithinRel(79.0));
    CHECK_THAT(*bz::unit_sell_revenue(market, bz::SellMode::Offer, 0.0125), WithinRel(98.75));
}

TEST_CASE("Missing or invalid prices yield no price", "[pricing]") {
    const bz::MarketData empty{};
    CHECK_FALSE(bz::unit_buy_cost(empty, bz::BuyMode::Instant).has_value());
    CHECK_FALSE(bz::unit_sell_revenue(empty, bz::SellMode::Offer, 0.0).has_value());

    const bz::Item off_bazaar{.id = "GHOST"};
    CHECK_FALSE(bz::unit_buy_cost(off_bazaar, bz::BuyMode::Order).has_value());
}

// ---------------------------------------------------------------------------
// Specification tests for the v1 algorithms. They are hidden until implemented:
// run them with `bz_tests "[todo]"`, then remove the "[.]" tag once they pass.
// ---------------------------------------------------------------------------

TEST_CASE("compute_min_costs picks the cheapest source and survives cycles", "[.][todo][solver]") {
    const bz::Catalog catalog = synthetic_world();
    const bz::CostTable costs = bz::compute_min_costs(catalog, kRawPrices);
    REQUIRE(costs.size() == catalog.items().size());

    const auto& raw = costs[index_of(catalog, "RAW")];
    REQUIRE(raw);
    CHECK_THAT(raw->unit_cost, WithinAbs(10.0, 1e-9));

    const auto& mid = costs[index_of(catalog, "MID")];
    REQUIRE(mid);
    CHECK_THAT(mid->unit_cost, WithinAbs(40.0, 1e-9));
    CHECK(mid->recipe.has_value());

    const auto& top = costs[index_of(catalog, "TOP")];
    REQUIRE(top);
    CHECK_THAT(top->unit_cost, WithinAbs(130.0, 1e-9));

    const auto& shard = costs[index_of(catalog, "SHARD")];
    REQUIRE(shard);
    CHECK_THAT(shard->unit_cost, WithinAbs(15.0, 1e-9));

    CHECK_FALSE(costs[index_of(catalog, "GHOST")].has_value());
    CHECK_FALSE(costs[index_of(catalog, "WIDGET")].has_value());
}

TEST_CASE("best_paths ranks craft paths by unit cost", "[.][todo][solver]") {
    const bz::Catalog catalog = synthetic_world();
    const auto paths = bz::best_paths(catalog, index_of(catalog, "TOP"), kRawPrices);

    REQUIRE_FALSE(paths.empty());
    CHECK(std::is_sorted(paths.begin(), paths.end(),
                         [](const auto& a, const auto& b) { return a.unit_cost < b.unit_cost; }));

    const bz::CraftPath& best = paths.front();
    CHECK_THAT(best.unit_cost, WithinAbs(130.0, 1e-9));
    REQUIRE(best.purchases.size() == 1);
    CHECK(best.purchases[0].item == index_of(catalog, "RAW"));
    CHECK_THAT(best.purchases[0].amount, WithinAbs(13.0, 1e-9));

    CHECK(bz::best_paths(catalog, index_of(catalog, "WIDGET"), kRawPrices).empty());
}

TEST_CASE("max_quantity honors each constraint", "[.][todo][planner]") {
    const bz::Catalog catalog = synthetic_world();
    const bz::ItemIndex top = index_of(catalog, "TOP");
    const bz::CraftPath path{.purchases = {{index_of(catalog, "RAW"), 13.0}}, .unit_cost = 130.0};

    // 13 RAW per TOP: floor(71'680 / 13) = 5'513 crafts fit in one order.
    CHECK(bz::max_quantity(catalog, top, path, kRawPrices, {.order_slots = 1}) == 5'513);
    CHECK(bz::max_quantity(catalog, top, path, kRawPrices, {.budget = 1'300.0}) == 10);
    CHECK(bz::max_quantity(catalog, top, path, kRawPrices, {.liquidity_share = 0.1}) == 100);
    CHECK(bz::max_quantity(catalog, top, path, kRawPrices, {.order_slots = 1, .budget = 1'300.0}) ==
          10);
    CHECK_THROWS_AS(bz::max_quantity(catalog, top, path, kRawPrices, {}), std::invalid_argument);
}

TEST_CASE("make_plan splits purchases into Bazaar orders", "[.][todo][planner]") {
    const bz::Catalog catalog = synthetic_world();
    const bz::ItemIndex raw = index_of(catalog, "RAW");
    const bz::CraftPath path{.purchases = {{raw, 13.0}}, .unit_cost = 130.0};

    const bz::Plan plan =
        bz::make_plan(catalog, index_of(catalog, "TOP"), path, kRawPrices, {.order_slots = 2});

    CHECK(plan.quantity == 11'027);
    CHECK(plan.slots_used == 2);
    CHECK_THAT(plan.total_cost, WithinRel(1'433'510.0));
    CHECK_THAT(plan.total_revenue, WithinRel(1'984'860.0));
    CHECK_THAT(plan.profit, WithinRel(551'350.0));
    CHECK(plan.binding_limits == std::vector<bz::Limit>{bz::Limit::OrderSlots});

    REQUIRE(plan.orders.size() == 1);
    CHECK(plan.orders[0].item == raw);
    CHECK(plan.orders[0].quantity == 143'351);
    CHECK(plan.orders[0].slot_amounts == std::vector<std::int64_t>{71'680, 71'671});
}

TEST_CASE("scan returns profitable crafts sorted by unit profit", "[.][todo][scanner]") {
    const bz::Catalog catalog = synthetic_world();
    const auto results = bz::scan(catalog, kRawPrices, {.top = 10});

    REQUIRE(results.size() == 2);
    CHECK(results[0].item == index_of(catalog, "TOP"));
    CHECK_THAT(results[0].unit_profit, WithinAbs(50.0, 1e-9));
    CHECK(results[1].item == index_of(catalog, "MID"));
    CHECK_THAT(results[1].unit_profit, WithinAbs(5.0, 1e-9));
}
