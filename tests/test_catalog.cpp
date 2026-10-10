#include "bz/bazaar.hpp"
#include "bz/catalog.hpp"
#include "bz/neu.hpp"
#include "bz/util.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <stdexcept>

using Catch::Matchers::WithinRel;

namespace {

const std::filesystem::path kFixtures{BZ_FIXTURES_DIR};

bz::Catalog fixture_catalog() {
    const auto bazaar = bz::parse_bazaar(bz::read_text_file(kFixtures / "bazaar.json"));
    const auto neu = bz::load_neu_recipes(kFixtures / "neu" / "items");
    return bz::Catalog::build(bazaar, neu.recipes);
}

} // namespace

TEST_CASE("parse_item_count reads ids and quantities", "[neu]") {
    CHECK(bz::parse_item_count("ENCHANTED_COCOA:32") == bz::ItemCount{"ENCHANTED_COCOA", 32});
    CHECK(bz::parse_item_count("WHEAT") == bz::ItemCount{"WHEAT", 1});
    CHECK(bz::parse_item_count("INK_SACK-3:32") == bz::ItemCount{"INK_SACK-3", 32});
    CHECK(bz::parse_item_count("INK_SACK:3:2") == bz::ItemCount{"INK_SACK-3", 2});
    CHECK_FALSE(bz::parse_item_count("").has_value());
    CHECK_FALSE(bz::parse_item_count("WHEAT:0").has_value());
    // Like Python's str.isdigit(): a signed suffix is part of the id, not a count.
    CHECK(bz::parse_item_count("WHEAT:-3") == bz::ItemCount{"WHEAT--3", 1});
}

TEST_CASE("parse_neu_item merges a crafting grid", "[neu]") {
    const auto recipes = bz::parse_neu_item(
        bz::read_text_file(kFixtures / "neu" / "items" / "ENCHANTED_COCOA.json"));

    REQUIRE(recipes.size() == 1);
    CHECK(recipes[0].output == "ENCHANTED_COCOA");
    CHECK(recipes[0].output_count == 1);
    REQUIRE(recipes[0].ingredients.size() == 1);
    CHECK(recipes[0].ingredients[0] == bz::ItemCount{"INK_SACK-3", 160});
}

TEST_CASE("parse_neu_item ignores NPC shops and other non-crafting types", "[neu]") {
    // NPC prices must never enter the item graph, as in the Python reference.
    const auto shop = bz::parse_neu_item(
        bz::read_text_file(kFixtures / "neu" / "items" / "FARM_MERCHANT_NPC.json"));
    CHECK(shop.empty());

    const auto gem =
        bz::parse_neu_item(bz::read_text_file(kFixtures / "neu" / "items" / "SAMPLE_GEM.json"));
    REQUIRE(gem.size() == 2); // 'recipe' + 'recipes' crafting entry, no forge.
    for (const auto& recipe : gem) {
        CHECK(recipe.output == "SAMPLE_GEM");
        CHECK(recipe.output_count == 2);
        CHECK(recipe.ingredients == std::vector<bz::ItemCount>{{"ROUGH_GEM", 80}});
    }
}

TEST_CASE("parse_neu_item follows the reference parsing rules", "[neu]") {
    // A grid inside an npc_shop entry is still not a crafting recipe.
    CHECK(bz::parse_neu_item(
              R"({"internalname": "X", "recipes": [{"type": "npc_shop", "A1": "Y:2",
                "cost": ["SKYBLOCK_COIN:10"], "result": "X"}]})")
              .empty());

    // count: missing, null or 0 means 1, like int(raw.get("count", 1) or 1).
    const auto zero =
        bz::parse_neu_item(R"({"internalname": "X", "recipe": {"A1": "Y", "count": 0}})");
    REQUIRE(zero.size() == 1);
    CHECK(zero[0].output_count == 1);
    const auto null_count =
        bz::parse_neu_item(R"({"internalname": "X", "recipe": {"A1": "Y", "count": null}})");
    REQUIRE(null_count.size() == 1);
    CHECK(null_count[0].output_count == 1);

    // The output is always the file's internalname.
    const auto overridden = bz::parse_neu_item(
        R"({"internalname": "x", "recipe": {"A1": "Y", "overrideOutputId": "Z"}})");
    REQUIRE(overridden.size() == 1);
    CHECK(overridden[0].output == "X");

    // Without internalname, no recipe is kept.
    CHECK(bz::parse_neu_item(R"({"recipe": {"A1": "Y"}})").empty());
}

TEST_CASE("parse_neu_item rejects invalid JSON", "[neu]") {
    CHECK_THROWS_AS(bz::parse_neu_item("{ not json"), std::runtime_error);
    CHECK_THROWS_AS(bz::parse_neu_item("[1, 2]"), std::runtime_error);
}

TEST_CASE("load_neu_recipes skips unreadable files", "[neu]") {
    const auto result = bz::load_neu_recipes(kFixtures / "neu" / "items");
    CHECK(result.files_read == 5);
    CHECK(result.files_skipped == 1);
    CHECK(result.recipes.size() == 5); // The 3 NPC shop offers are ignored.

    CHECK_THROWS_AS(bz::load_neu_recipes(kFixtures / "missing"), std::runtime_error);
}

TEST_CASE("parse_bazaar reads quick_status and normalizes ids", "[bazaar]") {
    const auto snapshot = bz::parse_bazaar(bz::read_text_file(kFixtures / "bazaar.json"));

    CHECK(snapshot.last_updated_ms == 1782688693578);
    // NO_STATUS has no quick_status but is still a Bazaar product, as in the reference.
    REQUIRE(snapshot.products.size() == 4);
    const auto no_status =
        std::find_if(snapshot.products.begin(), snapshot.products.end(),
                     [](const bz::BazaarProduct& p) { return p.id == "NO_STATUS"; });
    REQUIRE(no_status != snapshot.products.end());
    CHECK(no_status->market == bz::MarketData{});

    const auto ink = std::find_if(snapshot.products.begin(), snapshot.products.end(),
                                  [](const bz::BazaarProduct& p) { return p.id == "INK_SACK-3"; });
    REQUIRE(ink != snapshot.products.end());
    CHECK_THAT(ink->market.buy_price, WithinRel(3.1));
    CHECK_THAT(ink->market.sell_price, WithinRel(2.9));
    CHECK(ink->market.buy_moving_week == 70000000);
}

TEST_CASE("parse_bazaar rejects malformed responses", "[bazaar]") {
    CHECK_THROWS_AS(bz::parse_bazaar("not json"), std::runtime_error);
    CHECK_THROWS_AS(bz::parse_bazaar(R"({"success": true})"), std::runtime_error);
    CHECK_THROWS_AS(bz::parse_bazaar(R"({"success": false, "products": {}})"), std::runtime_error);
}

TEST_CASE("serialize_bazaar round-trips through parse_bazaar", "[bazaar]") {
    const std::string full = bz::read_text_file(kFixtures / "bazaar.json");
    const auto original = bz::parse_bazaar(full);

    const std::string compact = bz::serialize_bazaar(original);
    const auto restored = bz::parse_bazaar(compact);

    CHECK(compact.size() < full.size());
    CHECK(restored.last_updated_ms == original.last_updated_ms);
    REQUIRE(restored.products.size() == original.products.size());
    for (std::size_t i = 0; i < original.products.size(); ++i) {
        CAPTURE(original.products[i].id);
        CHECK(restored.products[i].id == original.products[i].id);
        CHECK(restored.products[i].market == original.products[i].market);
    }
}

TEST_CASE("Catalog links recipes in both directions", "[catalog]") {
    const bz::Catalog catalog = fixture_catalog();

    const auto cocoa = catalog.find("enchanted_cocoa");
    const auto beans = catalog.find("INK_SACK:3");
    REQUIRE(cocoa);
    REQUIRE(beans);

    const bz::Item& cocoa_item = catalog.item(*cocoa);
    REQUIRE(cocoa_item.market.has_value());
    REQUIRE(cocoa_item.produced_by.size() == 1);

    const bz::Recipe& recipe = catalog.recipe(cocoa_item.produced_by[0]);
    CHECK(recipe.output == *cocoa);
    CHECK(recipe.ingredients == std::vector<bz::Ingredient>{{*beans, 160}});
    CHECK(catalog.item(*beans).used_in == std::vector<bz::RecipeIndex>{cocoa_item.produced_by[0]});
}

TEST_CASE("Catalog stores duplicate recipes once", "[catalog]") {
    const bz::Catalog catalog = fixture_catalog();
    const auto gem = catalog.find("SAMPLE_GEM");
    REQUIRE(gem);
    CHECK(catalog.item(*gem).produced_by.size() == 1);
    CHECK(catalog.recipes().size() == 4);
}

TEST_CASE("Catalog keeps cycles and items outside the Bazaar", "[catalog]") {
    const bz::Catalog catalog = fixture_catalog();
    const auto block = catalog.find("BLOCK_A");
    const auto item = catalog.find("ITEM_A");
    REQUIRE(block);
    REQUIRE(item);

    CHECK_FALSE(catalog.item(*block).market.has_value());
    CHECK(catalog.item(*block).produced_by.size() == 1);
    CHECK(catalog.item(*item).produced_by.size() == 1);
    CHECK(catalog.item(*block).used_in == catalog.item(*item).produced_by);
}

TEST_CASE("Catalog contains no NPC shop data", "[catalog]") {
    const bz::Catalog catalog = fixture_catalog();
    CHECK_FALSE(catalog.find("SKYBLOCK_COIN").has_value());
    CHECK_FALSE(catalog.find("HAY_BLOCK").has_value());
    CHECK_FALSE(catalog.find("COMPOST").has_value());

    const auto wheat = catalog.find("WHEAT");
    REQUIRE(wheat); // Known through the Bazaar only.
    CHECK(catalog.item(*wheat).produced_by.empty());
}

TEST_CASE("Catalog validates recipes", "[catalog]") {
    bz::Catalog catalog;
    const bz::ItemIndex a = catalog.intern("A");

    CHECK_THROWS_AS(catalog.intern("  "), std::invalid_argument);
    CHECK_THROWS_AS(catalog.add_recipe({.output = 42}), std::out_of_range);
    CHECK_THROWS_AS(catalog.add_recipe({.output = a, .output_count = 0}), std::invalid_argument);
    CHECK_THROWS_AS(catalog.add_recipe({.output = a, .ingredients = {{a, -1}}}),
                    std::invalid_argument);
}
