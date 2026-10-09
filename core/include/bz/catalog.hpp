/**
 * @file catalog.hpp
 * @brief The item graph: every known item, its market data and the recipes linking items.
 */
#pragma once

#include "bz/bazaar.hpp"
#include "bz/model.hpp"
#include "bz/neu.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bz {

/**
 * @brief Owns items and recipes, and indexes the links between them in both directions.
 *
 * Items and recipes are addressed by dense indices that stay valid for the catalog's
 * lifetime. Item ids are normalized on every lookup, so callers may pass raw ids.
 *
 * The graph may contain cycles (for instance an item and its compacted block); algorithms
 * walking it are responsible for handling them.
 */
class Catalog {
public:
    /**
     * @brief Builds a catalog from a Bazaar snapshot and NEU recipes.
     *
     * Every Bazaar product and every item mentioned by a recipe becomes an item.
     * Duplicate recipes are stored once.
     *
     * @param bazaar Market data.
     * @param recipes Recipes to link.
     * @return The populated catalog.
     */
    [[nodiscard]] static Catalog build(const BazaarSnapshot& bazaar,
                                       const std::vector<NeuRecipe>& recipes);

    /**
     * @brief Returns the index of an item, creating it if it does not exist.
     * @param id Item id, normalized internally.
     * @return Index of the item.
     * @throws std::invalid_argument If @p id is blank.
     */
    ItemIndex intern(std::string_view id);

    /**
     * @brief Looks up an item by id.
     * @param id Item id, normalized internally.
     * @return Index of the item, or `std::nullopt` if unknown.
     */
    [[nodiscard]] std::optional<ItemIndex> find(std::string_view id) const;

    /**
     * @brief Attaches Bazaar data to an item.
     * @throws std::out_of_range If @p item is not a valid index.
     */
    void set_market(ItemIndex item, const MarketData& market);

    /**
     * @brief Stores a recipe and links it to its output and ingredients.
     *
     * Ingredients are merged per item and sorted. If an equal recipe is already stored,
     * nothing is added.
     *
     * @param recipe Recipe referencing existing item indices.
     * @return Index of the stored (or already existing) recipe.
     * @throws std::out_of_range If the recipe references an unknown item.
     * @throws std::invalid_argument If a count is not strictly positive.
     */
    RecipeIndex add_recipe(Recipe recipe);

    /// @throws std::out_of_range If @p index is not a valid item index.
    [[nodiscard]] const Item& item(ItemIndex index) const { return items_.at(index); }

    /// @throws std::out_of_range If @p index is not a valid recipe index.
    [[nodiscard]] const Recipe& recipe(RecipeIndex index) const { return recipes_.at(index); }

    /// All items, indexed by ItemIndex.
    [[nodiscard]] std::span<const Item> items() const noexcept { return items_; }

    /// All recipes, indexed by RecipeIndex.
    [[nodiscard]] std::span<const Recipe> recipes() const noexcept { return recipes_; }

private:
    std::vector<Item> items_;
    std::vector<Recipe> recipes_;
    std::unordered_map<std::string, ItemIndex> index_by_id_;
};

} // namespace bz
