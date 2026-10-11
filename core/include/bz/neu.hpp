/**
 * @file neu.hpp
 * @brief Loading of crafting table recipes from the NotEnoughUpdates item repository.
 *
 * Mirrors `SkyblockDataEngine.load_recipes()` of the Python reference: only the 3×3 crafting
 * grid (`A1`…`C3`) is read. NPC shop offers (and their coin costs), forge recipes, Kat
 * upgrades, trades and mob drops are ignored, so NPC prices never enter a calculation.
 */
#pragma once

#include "bz/model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bz {

/// An item id with a quantity, as written in NEU files (`ENCHANTED_COCOA:32`).
struct ItemCount {
    std::string id;         ///< Normalized item id.
    std::int64_t count = 1; ///< Strictly positive quantity.

    friend bool operator==(const ItemCount&, const ItemCount&) = default;
};

/// A crafting recipe as described by NEU, before item ids are resolved to catalog indices.
struct NeuRecipe {
    std::string output;                 ///< Normalized id of the produced item.
    std::int64_t output_count = 1;      ///< Units produced per craft.
    std::vector<ItemCount> ingredients; ///< Item inputs, merged per item.

    friend bool operator==(const NeuRecipe&, const NeuRecipe&) = default;
};

/// Outcome of loading a whole NEU items directory.
struct NeuLoadResult {
    /// Every recipe found: in file name order from load_neu_recipes(), grouped by output id
    /// from parse_recipe_cache().
    std::vector<NeuRecipe> recipes;
    std::size_t files_read = 0;    ///< JSON files successfully parsed.
    std::size_t files_skipped = 0; ///< JSON files that could not be parsed.
};

/**
 * @brief Parses an NEU ingredient entry such as `ENCHANTED_COCOA:32`.
 *
 * The quantity is read after the last `:` when it is a number, so legacy entries like
 * `INK_SACK:3:2` resolve to id `INK_SACK-3` with quantity 2. A missing quantity means 1.
 *
 * @param entry Raw entry.
 * @return The parsed entry, or `std::nullopt` if it is blank or its quantity is not positive.
 */
[[nodiscard]] std::optional<ItemCount> parse_item_count(std::string_view entry);

/**
 * @brief Extracts the crafting recipes of one NEU item file.
 *
 * Reads the `recipe` object and the `crafting` entries of the `recipes` array (an entry
 * without `type` counts as crafting). Entries of any other type (`npc_shop`, `forge`, ...)
 * are skipped. The output is always the file's `internalname`; `count` defaults to 1 when
 * missing, null or not positive, as `int(raw.get("count", 1) or 1)` does in the reference.
 * A grid without any valid ingredient produces no recipe.
 *
 * @param json_text Content of the item file.
 * @return The recipes found, possibly none.
 * @throws std::runtime_error If @p json_text is not a JSON object.
 */
[[nodiscard]] std::vector<NeuRecipe> parse_neu_item(std::string_view json_text);

/**
 * @brief Loads every `*.json` file of an NEU items directory, recursively.
 *
 * Unparsable files are counted in NeuLoadResult::files_skipped instead of aborting the load.
 *
 * @param items_dir Path to the `items` directory of the NEU repository.
 * @return All recipes found, with load statistics.
 * @throws std::runtime_error If @p items_dir is not a directory.
 */
[[nodiscard]] NeuLoadResult load_neu_recipes(const std::filesystem::path& items_dir);

/**
 * @brief Version of the recipe cache layout and of the parsing rules that produced it.
 *
 * Bump it whenever parse_neu_item() or the cache layout changes: caches written with another
 * version are rejected by parse_recipe_cache(), so they get rebuilt from the NEU repository.
 */
inline constexpr int kRecipeCacheFormat = 1;

/**
 * @brief Serializes loaded NEU recipes into the recipe cache.
 *
 * Reading the cache takes a few milliseconds, against a full parse of the NEU repository for
 * load_neu_recipes(). Recipes are grouped under their output id and ingredients are keyed by
 * id, all keys sorted, so the file reads like a recipe book and can be searched by item id:
 *
 * @code{.json}
 * {"format": 1, "files_read": 8934, "files_skipped": 0,
 *  "recipes": {"ENCHANTED_COCOA": [{"count": 1, "ingredients": {"INK_SACK-3": 160}}]}}
 * @endcode
 *
 * Ingredients come back sorted by id, which makes no difference to the catalog.
 *
 * @param result Recipes and load statistics, as returned by load_neu_recipes().
 * @return Indented JSON text.
 */
[[nodiscard]] std::string serialize_recipe_cache(const NeuLoadResult& result);

/**
 * @brief Reads a recipe cache written by serialize_recipe_cache().
 * @param json_text Content of the cache file.
 * @return The cached recipes, grouped by output id with ingredients sorted by id, and the
 *         original load statistics.
 * @throws std::runtime_error If the text is not a valid cache, or was written with another
 *         kRecipeCacheFormat.
 */
[[nodiscard]] NeuLoadResult parse_recipe_cache(std::string_view json_text);

} // namespace bz
