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
};

/// Outcome of loading a whole NEU items directory.
struct NeuLoadResult {
    std::vector<NeuRecipe> recipes; ///< Every recipe found, in file name order.
    std::size_t files_read = 0;     ///< JSON files successfully parsed.
    std::size_t files_skipped = 0;  ///< JSON files that could not be parsed.
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

} // namespace bz
