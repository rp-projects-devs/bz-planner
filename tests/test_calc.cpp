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

TEST_CASE("Default pricing matches the Python reference", "[pricing]") {
    // Reference: cost_type="buy" uses sellPrice, revenue_type="sell" uses buyPrice, no tax.
    const bz::PricingConfig defaults{};
    const bz::MarketData market{.buy_price = 100.0, .sell_price = 80.0};
    CHECK_THAT(*bz::unit_buy_cost(market, defaults.buy), WithinRel(80.0));
    CHECK_THAT(*bz::unit_sell_revenue(market, defaults.sell, defaults.tax_rate), WithinRel(100.0));
}

TEST_CASE("Missing or invalid prices yield no price", "[pricing]") {
    const bz::MarketData empty{};
    CHECK_FALSE(bz::unit_buy_cost(empty, bz::BuyMode::Instant).has_value());
    CHECK_FALSE(bz::unit_sell_revenue(empty, bz::SellMode::Offer, 0.0).has_value());

    const bz::Item off_bazaar{.id = "GHOST"};
    CHECK_FALSE(bz::unit_buy_cost(off_bazaar, bz::BuyMode::Order).has_value());
}

// ---------------------------------------------------------------------------
// v1 algorithms on the synthetic economy.
// ---------------------------------------------------------------------------

TEST_CASE("compute_min_costs picks the cheapest source and survives cycles", "[solver]") {
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

    CHECK_FALSE(costs[index_of(catalog, "GHOST")].has_value());
    CHECK_FALSE(costs[index_of(catalog, "WIDGET")].has_value());
}

TEST_CASE("best_paths ranks craft paths by unit cost", "[solver]") {
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

TEST_CASE("max_quantity honors each constraint", "[planner]") {
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

TEST_CASE("make_plan splits purchases into Bazaar orders", "[planner]") {
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

TEST_CASE("scan returns profitable crafts sorted by unit profit", "[scanner]") {
    const bz::Catalog catalog = synthetic_world();
    const auto results = bz::scan(catalog, kRawPrices, {.top = 10});

    REQUIRE(results.size() == 2);
    CHECK(results[0].item == index_of(catalog, "TOP"));
    CHECK_THAT(results[0].unit_profit, WithinAbs(50.0, 1e-9));
    CHECK(results[1].item == index_of(catalog, "MID"));
    CHECK_THAT(results[1].unit_profit, WithinAbs(5.0, 1e-9));
}

TEST_CASE("best_paths honors max_depth and never buys the target", "[solver]") {
    const bz::Catalog catalog = synthetic_world();
    const bz::ItemIndex top = index_of(catalog, "TOP");

    // One level: MID is bought, 3 × 50 + 10.
    const auto shallow = bz::best_paths(catalog, top, kRawPrices, {.max_depth = 1});
    REQUIRE_FALSE(shallow.empty());
    CHECK_THAT(shallow.front().unit_cost, WithinAbs(160.0, 1e-9));
    REQUIRE(shallow.front().craft);
    CHECK(shallow.front().craft->inputs.empty());
    CHECK(bz::best_paths(catalog, top, kRawPrices, {.max_depth = 0}).empty());

    // RAW ← BLOCK ← RAW is a cycle: BLOCK must be bought, never crafted from RAW.
    const bz::ItemIndex raw = index_of(catalog, "RAW");
    const auto raw_paths = bz::best_paths(catalog, raw, kRawPrices);
    REQUIRE(raw_paths.size() == 1);
    REQUIRE(raw_paths[0].purchases.size() == 1);
    CHECK(raw_paths[0].purchases[0].item == index_of(catalog, "BLOCK"));
    CHECK_THAT(raw_paths[0].unit_cost, WithinAbs(95.0 / 9.0, 1e-9));

    const auto top_two = bz::best_paths(catalog, top, kRawPrices, {.top_k = 2});
    CHECK(top_two.size() == 2);
    for (const bz::CraftPath& path : bz::best_paths(catalog, top, kRawPrices)) {
        for (const bz::Purchase& purchase : path.purchases) {
            CHECK(purchase.item != top);
        }
    }
}

TEST_CASE("make_plan reports budget and liquidity limits", "[planner]") {
    const bz::Catalog catalog = synthetic_world();
    const bz::ItemIndex top = index_of(catalog, "TOP");
    const bz::CraftPath path{.purchases = {{index_of(catalog, "RAW"), 13.0}}, .unit_cost = 130.0};

    const bz::Plan budget = bz::make_plan(catalog, top, path, kRawPrices, {.budget = 1'300.0});
    CHECK(budget.quantity == 10);
    CHECK(budget.binding_limits == std::vector<bz::Limit>{bz::Limit::Budget});
    CHECK_THAT(budget.total_cost, WithinRel(1'300.0));

    const bz::Plan both =
        bz::make_plan(catalog, top, path, kRawPrices, {.budget = 13'000.0, .liquidity_share = 0.1});
    CHECK(both.quantity == 100);
    CHECK(both.binding_limits == std::vector<bz::Limit>{bz::Limit::Budget, bz::Limit::Liquidity});

    const bz::Plan none = bz::make_plan(catalog, top, path, kRawPrices, {.budget = 1.0});
    CHECK(none.quantity == 0);
    CHECK(none.orders.empty());
    CHECK(none.slots_used == 0);
}

TEST_CASE("scan applies its filters", "[scanner]") {
    const bz::Catalog catalog = synthetic_world();
    const bz::ItemIndex mid = index_of(catalog, "MID");

    const auto liquid = bz::scan(catalog, kRawPrices, {.min_weekly_volume = 10'000});
    REQUIRE(liquid.size() == 1);
    CHECK(liquid[0].item == mid);

    const auto high_margin = bz::scan(catalog, kRawPrices, {.min_margin = 0.2});
    REQUIRE(high_margin.size() == 1);
    CHECK(high_margin[0].item == index_of(catalog, "TOP"));
    CHECK_THAT(high_margin[0].margin, WithinAbs(50.0 / 130.0, 1e-9));

    const auto cheap = bz::scan(catalog, kRawPrices, {.max_unit_cost = 100.0});
    REQUIRE(cheap.size() == 1);
    CHECK(cheap[0].item == mid);

    CHECK(bz::scan(catalog, kRawPrices, {.top = 1}).size() == 1);
}

TEST_CASE("make_plan rounds every nested recipe to whole crafts", "[planner]") {
    // PLATE: 2 ORE → 3 PLATE (not on the Bazaar); GEAR: 2 PLATE → 1 GEAR.
    bz::BazaarSnapshot bazaar;
    bazaar.products = {product("ORE", 1, 1),
                       {"GEAR", bz::MarketData{.buy_price = 10,
                                               .sell_price = 10,
                                               .buy_moving_week = 1'000,
                                               .sell_moving_week = 20}}};
    const std::vector<bz::NeuRecipe> recipes{
        {.output = "PLATE", .output_count = 3, .ingredients = {{"ORE", 2}}},
        {.output = "GEAR", .ingredients = {{"PLATE", 2}}},
    };
    const bz::Catalog catalog = bz::Catalog::build(bazaar, recipes);
    const bz::ItemIndex gear = index_of(catalog, "GEAR");
    const bz::ItemIndex ore = index_of(catalog, "ORE");

    const auto paths = bz::best_paths(catalog, gear, kRawPrices);
    REQUIRE(paths.size() == 1);
    CHECK_THAT(paths[0].unit_cost, WithinAbs(4.0 / 3.0, 1e-9));

    // Selling instantly is limited by the weekly instant sells: 10 % of 20 = 2 gears.
    const bz::Plan plan =
        bz::make_plan(catalog, gear, paths[0], kRawPrices, {.liquidity_share = 0.1});
    CHECK(plan.quantity == 2);
    CHECK(plan.binding_limits == std::vector<bz::Limit>{bz::Limit::Liquidity});
    // 2 gears need 4 plates, i.e. 2 crafts of 2 ORE: 4 ORE, not ceil(2 × 4/3) = 3.
    REQUIRE(plan.orders.size() == 1);
    CHECK(plan.orders[0].item == ore);
    CHECK(plan.orders[0].quantity == 4);
    CHECK_THAT(plan.total_cost, WithinRel(4.0));
    REQUIRE(plan.crafts.size() == 2);
    CHECK(catalog.recipe(plan.crafts[0].recipe).output == index_of(catalog, "PLATE"));
    CHECK(plan.crafts[0].crafts == 2);
    CHECK(catalog.recipe(plan.crafts[1].recipe).output == gear);
    CHECK(plan.crafts[1].crafts == 2);

    // Sell offers are filled by instant buyers: 10 % of 1 000.
    const bz::PricingConfig offers{bz::BuyMode::Instant, bz::SellMode::Offer, 0.0};
    CHECK(bz::max_quantity(catalog, gear, paths[0], offers, {.liquidity_share = 0.1}) == 100);

    // Without a craft tree, purchases are rounded once, like the reference.
    const bz::CraftPath flat{.purchases = {{ore, 4.0 / 3.0}}, .unit_cost = 4.0 / 3.0};
    const bz::Plan flat_plan =
        bz::make_plan(catalog, gear, flat, kRawPrices, {.liquidity_share = 0.1});
    REQUIRE(flat_plan.orders.size() == 1);
    CHECK(flat_plan.orders[0].quantity == 3);
    CHECK(flat_plan.crafts.empty());

    const bz::CraftPath wrong{.purchases = {{ore, 1.0}}, .craft = paths[0].craft->inputs.front()};
    CHECK_THROWS_AS(bz::max_quantity(catalog, gear, wrong, kRawPrices, {.order_slots = 1}),
                    std::invalid_argument);
}

TEST_CASE("best_paths never buys an item inside its own recipe tree", "[solver]") {
    // A self-loop (like WOOL) and a 2-cycle: neither may buy the item being crafted.
    bz::BazaarSnapshot bazaar;
    bazaar.products = {product("WOOL", 5, 5), product("DYE", 1, 1), product("CARPET", 20, 20)};
    const std::vector<bz::NeuRecipe> recipes{
        {.output = "WOOL", .ingredients = {{"WOOL", 1}, {"DYE", 1}}},
        {.output = "CARPET", .output_count = 2, .ingredients = {{"WOOL", 3}}},
        {.output = "WOOL", .ingredients = {{"CARPET", 1}}},
    };
    const bz::Catalog catalog = bz::Catalog::build(bazaar, recipes);
    const bz::ItemIndex wool = index_of(catalog, "WOOL");

    for (const char* id : {"WOOL", "CARPET"}) {
        const bz::ItemIndex target = index_of(catalog, id);
        for (const bz::CraftPath& path :
             bz::best_paths(catalog, target, kRawPrices, {.max_depth = 5})) {
            for (const bz::Purchase& purchase : path.purchases) {
                CHECK(purchase.item != target);
            }
        }
    }
    // WOOL: only from a bought CARPET (the dye recipe needs WOOL itself).
    const auto wool_paths = bz::best_paths(catalog, wool, kRawPrices, {.max_depth = 5});
    REQUIRE(wool_paths.size() == 1);
    CHECK_THAT(wool_paths[0].unit_cost, WithinAbs(20.0, 1e-9));
}

TEST_CASE("plan_paths finds paths that save order slots", "[planner]") {
    // CHIP = 160 DUST (1 coin each) or 1 DUST_BLOCK (200 coins), sold 300.
    bz::BazaarSnapshot bazaar;
    bazaar.products = {product("DUST", 1, 1), product("DUST_BLOCK", 200, 200),
                       product("CHIP", 300, 300)};
    const std::vector<bz::NeuRecipe> recipes{
        {.output = "DUST_BLOCK", .ingredients = {{"DUST", 160}}},
        {.output = "CHIP", .ingredients = {{"DUST_BLOCK", 1}}},
    };
    const bz::Catalog catalog = bz::Catalog::build(bazaar, recipes);
    const bz::ItemIndex chip = index_of(catalog, "CHIP");
    const bz::Constraints one_slot{.order_slots = 1};

    // Per unit, crafting the block is cheapest (160 vs 200)...
    const auto cheapest = bz::best_paths(catalog, chip, kRawPrices, {.top_k = 1});
    REQUIRE(cheapest.size() == 1);
    CHECK_THAT(cheapest[0].unit_cost, WithinAbs(160.0, 1e-9));
    const auto cheap_plan = bz::best_plan(catalog, chip, cheapest, kRawPrices, one_slot);
    REQUIRE(cheap_plan);
    CHECK(cheap_plan->plan.quantity == 448); // 71 680 DUST / 160

    // ...but one order of blocks crafts 160 times more chips.
    const auto paths = bz::plan_paths(catalog, chip, kRawPrices, one_slot, {.top_k = 1});
    const auto best = bz::best_plan(catalog, chip, paths, kRawPrices, one_slot);
    REQUIRE(best);
    CHECK(best->plan.quantity == 71'680);
    CHECK_THAT(best->plan.profit, WithinRel(71'680.0 * 100.0));
    REQUIRE(best->plan.orders.size() == 1);
    CHECK(best->plan.orders[0].item == index_of(catalog, "DUST_BLOCK"));

    CHECK_THROWS_AS(bz::plan_paths(catalog, chip, kRawPrices, {}), std::invalid_argument);
}

TEST_CASE("scan with constraints ranks by the profit of the best plan", "[scanner]") {
    const bz::Catalog catalog = synthetic_world();
    // TOP earns 50 per unit but only 1 000 sell per week; MID earns 5 with 1 000 000.
    const auto results =
        bz::scan(catalog, kRawPrices, {}, {}, bz::Constraints{.liquidity_share = 0.1});
    REQUIRE(results.size() == 2);
    CHECK(results[0].item == index_of(catalog, "MID"));
    REQUIRE(results[0].plan);
    CHECK(results[0].plan->quantity == 100'000);
    CHECK_THAT(results[0].plan->profit, WithinRel(500'000.0));
    CHECK(results[1].item == index_of(catalog, "TOP"));
    REQUIRE(results[1].plan);
    CHECK_THAT(results[1].plan->profit, WithinRel(5'000.0));

    CHECK_FALSE(bz::scan(catalog, kRawPrices, {}).front().plan.has_value());
    CHECK_THROWS_AS(bz::scan(catalog, kRawPrices, {}, {}, bz::Constraints{}),
                    std::invalid_argument);
}
