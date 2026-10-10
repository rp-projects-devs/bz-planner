#include "bz/util.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

using Catch::Matchers::WithinRel;

TEST_CASE("trim removes surrounding whitespace only", "[util]") {
    CHECK(bz::trim("  a b \t\n") == "a b");
    CHECK(bz::trim("") == "");
    CHECK(bz::trim(" \t ") == "");
}

TEST_CASE("normalize_item_id produces the catalog key", "[util]") {
    CHECK(bz::normalize_item_id("INK_SACK:3") == "INK_SACK-3");
    CHECK(bz::normalize_item_id(" enchanted_cocoa ") == "ENCHANTED_COCOA");
    CHECK(bz::normalize_item_id("MOSQUITO;4") == "MOSQUITO;4");
    CHECK(bz::normalize_item_id("   ").empty());
}

TEST_CASE("parse_coins accepts plain numbers and suffixes", "[util]") {
    const auto [text, expected] = GENERATE(table<std::string_view, double>({
        {"5000000", 5e6},
        {"5,000,000", 5e6},
        {"5_000_000", 5e6},
        {"5m", 5e6},
        {"5M", 5e6},
        {"2.5b", 2.5e9},
        {"750k", 7.5e5},
        {"1t", 1e12},
        {"  1.5m  ", 1.5e6},
        {"0", 0.0},
    }));
    CAPTURE(text);
    const std::optional<double> coins = bz::parse_coins(text);
    REQUIRE(coins.has_value());
    CHECK_THAT(*coins, WithinRel(expected));
}

TEST_CASE("parse_coins rejects invalid amounts", "[util]") {
    const std::string_view text = GENERATE("", "   ", "m", "abc", "5x", "-5m", "5mm", "1e400");
    CAPTURE(text);
    CHECK_FALSE(bz::parse_coins(text).has_value());
}

TEST_CASE("write_text_file replaces content and creates directories", "[util]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bz_planner_test_write";
    const fs::path file = dir / "nested" / "out.txt";
    fs::remove_all(dir);

    bz::write_text_file(file, "first");
    CHECK(bz::read_text_file(file) == "first");

    bz::write_text_file(file, "second, longer content");
    CHECK(bz::read_text_file(file) == "second, longer content");
    CHECK_FALSE(fs::exists(file.string() + ".tmp"));

    fs::remove_all(dir);
}
