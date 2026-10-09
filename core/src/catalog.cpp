#include "bz/catalog.hpp"

#include "bz/util.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace bz {

namespace {

/// Merges duplicate ingredients and sorts them so equivalent recipes compare equal.
void canonicalize(std::vector<Ingredient>& ingredients) {
    std::sort(ingredients.begin(), ingredients.end(),
              [](const Ingredient& a, const Ingredient& b) { return a.item < b.item; });

    std::vector<Ingredient> merged;
    merged.reserve(ingredients.size());
    for (const Ingredient& ingredient : ingredients) {
        if (!merged.empty() && merged.back().item == ingredient.item) {
            merged.back().count += ingredient.count;
        } else {
            merged.push_back(ingredient);
        }
    }
    ingredients = std::move(merged);
}

} // namespace

Catalog Catalog::build(const BazaarSnapshot& bazaar, const std::vector<NeuRecipe>& recipes) {
    Catalog catalog;

    for (const BazaarProduct& product : bazaar.products) {
        catalog.set_market(catalog.intern(product.id), product.market);
    }

    for (const NeuRecipe& source : recipes) {
        Recipe recipe;
        recipe.kind = source.kind;
        recipe.output = catalog.intern(source.output);
        recipe.output_count = source.output_count;
        recipe.coin_cost = source.coin_cost;
        recipe.ingredients.reserve(source.ingredients.size());
        for (const ItemCount& ingredient : source.ingredients) {
            recipe.ingredients.push_back({catalog.intern(ingredient.id), ingredient.count});
        }
        catalog.add_recipe(std::move(recipe));
    }

    return catalog;
}

ItemIndex Catalog::intern(std::string_view id) {
    std::string key = normalize_item_id(id);
    if (key.empty()) {
        throw std::invalid_argument("item id must not be blank");
    }

    const auto [it, inserted] = index_by_id_.try_emplace(key, items_.size());
    if (inserted) {
        Item item;
        item.id = std::move(key);
        items_.push_back(std::move(item));
    }
    return it->second;
}

std::optional<ItemIndex> Catalog::find(std::string_view id) const {
    const auto it = index_by_id_.find(normalize_item_id(id));
    if (it == index_by_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void Catalog::set_market(ItemIndex item, const MarketData& market) {
    items_.at(item).market = market;
}

RecipeIndex Catalog::add_recipe(Recipe recipe) {
    if (recipe.output >= items_.size()) {
        throw std::out_of_range("recipe output is not a known item");
    }
    if (recipe.output_count <= 0) {
        throw std::invalid_argument("recipe output count must be positive");
    }
    for (const Ingredient& ingredient : recipe.ingredients) {
        if (ingredient.item >= items_.size()) {
            throw std::out_of_range("recipe ingredient is not a known item");
        }
        if (ingredient.count <= 0) {
            throw std::invalid_argument("recipe ingredient count must be positive");
        }
    }
    canonicalize(recipe.ingredients);

    const std::vector<RecipeIndex>& existing = items_[recipe.output].produced_by;
    const auto duplicate = std::find_if(existing.begin(), existing.end(), [&](RecipeIndex index) {
        return recipes_[index] == recipe;
    });
    if (duplicate != existing.end()) {
        return *duplicate;
    }

    const RecipeIndex index = recipes_.size();
    items_[recipe.output].produced_by.push_back(index);
    for (const Ingredient& ingredient : recipe.ingredients) {
        items_[ingredient.item].used_in.push_back(index);
    }
    recipes_.push_back(std::move(recipe));
    return index;
}

} // namespace bz
