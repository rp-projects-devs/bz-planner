#include "bz/bazaar.hpp"
#include "bz/catalog.hpp"
#include "bz/neu.hpp"
#include "bz/util.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
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
}

TEST_CASE("parse_neu_item merges a crafting grid", "[neu]") {
    const auto recipes = bz::parse_neu_item(
        bz::read_text_file(kFixtures / "neu" / "items" / "ENCHANTED_COCOA.json"));

    REQUIRE(recipes.size() == 1);
    CHECK(recipes[0].kind == bz::RecipeKind::Crafting);
    CHECK(recipes[0].output == "ENCHANTED_COCOA");
    CHECK(recipes[0].output_count == 1);
    REQUIRE(recipes[0].ingredients.size() == 1);
    CHECK(recipes[0].ingredients[0] == bz::ItemCount{"INK_SACK-3", 160});
}

TEST_CASE("parse_neu_item reads NPC shops and ignores unsupported types", "[neu]") {
    const auto shop = bz::parse_neu_item(
        bz::read_text_file(kFixtures / "neu" / "items" / "FARM_MERCHANT_NPC.json"));
    REQUIRE(shop.size() == 3);

    CHECK(shop[0].kind == bz::RecipeKind::NpcShop);
    CHECK(shop[0].output == "WHEAT");
    CHECK(shop[0].output_count == 3);
    CHECK(shop[0].ingredients.empty());
    CHECK_THAT(shop[0].coin_cost, WithinRel(7.0));

    CHECK(shop[1].output == "HAY_BLOCK");
    CHECK_THAT(shop[1].coin_cost, WithinRel(100.0));
    CHECK(shop[1].ingredients == std::vector<bz::ItemCount>{{"WHEAT", 2}});

    const auto gem =
        bz::parse_neu_item(bz::read_text_file(kFixtures / "neu" / "items" / "SAMPLE_GEM.json"));
    CHECK(gem.size() == 2); // 'recipe' + 'recipes' crafting entry; the forge entry is ignored.
}

TEST_CASE("parse_neu_item rejects invalid JSON", "[neu]") {
    CHECK_THROWS_AS(bz::parse_neu_item("{ not json"), std::runtime_error);
    CHECK_THROWS_AS(bz::parse_neu_item("[1, 2]"), std::runtime_error);
}

TEST_CASE("load_neu_recipes skips unreadable files", "[neu]") {
    const auto result = bz::load_neu_recipes(kFixtures / "neu" / "items");
    CHECK(result.files_read == 5);
    CHECK(result.files_skipped == 1);
    CHECK(result.recipes.size() == 8);

    CHECK_THROWS_AS(bz::load_neu_recipes(kFixtures / "missing"), std::runtime_error);
}

TEST_CASE("parse_bazaar reads quick_status and normalizes ids", "[bazaar]") {
    const auto snapshot = bz::parse_bazaar(bz::read_text_file(kFixtures / "bazaar.json"));

    CHECK(snapshot.last_updated_ms == 1782688693578);
    REQUIRE(snapshot.products.size() == 3); // NO_STATUS has no quick_status.

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
    CHECK(catalog.recipes().size() == 7);
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

TEST_CASE("Catalog validates recipes", "[catalog]") {
    bz::Catalog catalog;
    const bz::ItemIndex a = catalog.intern("A");

    CHECK_THROWS_AS(catalog.intern("  "), std::invalid_argument);
    CHECK_THROWS_AS(catalog.add_recipe({.output = 42}), std::out_of_range);
    CHECK_THROWS_AS(catalog.add_recipe({.output = a, .output_count = 0}), std::invalid_argument);
    CHECK_THROWS_AS(catalog.add_recipe({.output = a, .ingredients = {{a, -1}}}),
                    std::invalid_argument);
}
