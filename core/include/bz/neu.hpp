/**
 * @file neu.hpp
 * @brief Loading of crafting and NPC shop recipes from the NotEnoughUpdates item repository.
 *
 * Only recipes that can be priced in coins are kept: crafting table recipes and NPC shop
 * offers. Forge, Kat upgrades, trades and mob drops are ignored.
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

/// NEU id of the coin currency in NPC shop costs.
inline constexpr std::string_view kCoinId = "SKYBLOCK_COIN";

/// An item id with a quantity, as written in NEU files (`ENCHANTED_COCOA:32`).
struct ItemCount {
    std::string id;         ///< Normalized item id.
    std::int64_t count = 1; ///< Strictly positive quantity.

    friend bool operator==(const ItemCount&, const ItemCount&) = default;
};

/// A recipe as described by NEU, before item ids are resolved to catalog indices.
struct NeuRecipe {
    RecipeKind kind = RecipeKind::Crafting; ///< Execution mechanism.
    std::string output;                     ///< Normalized id of the produced item.
    std::int64_t output_count = 1;          ///< Units produced per execution.
    std::vector<ItemCount> ingredients;     ///< Item inputs, merged per item.
    double coin_cost = 0.0;                 ///< Coins paid per execution.
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
 * @brief Extracts the supported recipes of one NEU item file.
 *
 * Reads the `recipe` object (crafting grid) and the `recipes` array (`crafting` and
 * `npc_shop` entries). For NPC shops, `SKYBLOCK_COIN` costs become NeuRecipe::coin_cost and
 * any other cost becomes an ingredient.
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
