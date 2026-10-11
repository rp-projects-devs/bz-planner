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

/**
 * State of Tarjan's algorithm. The graph goes from each item to the ingredients of its recipes,
 * so a component is closed only once every component it depends on is closed: components come
 * out in dependency order.
 */
struct Tarjan {
    std::span<const Item> items;
    std::span<const Recipe> recipes;

    /// Discovery order of each item; empty while the item is not visited yet.
    std::vector<std::optional<std::size_t>> index;
    /// Smallest discovery order reachable from each item through items still on `stack`.
    std::vector<std::size_t> low;
    /// True while the item is on `stack`, i.e. not yet assigned to a component.
    std::vector<bool> on_stack;
    /// Visited items not yet assigned to a component.
    std::vector<ItemIndex> stack;
    /// Discovery order given to the next visited item.
    std::size_t next_index = 0;
    /// Items of the closed components, component after component, in dependency order.
    std::vector<ItemIndex> order;
    /// Start of each closed component in `order`, followed by `order.size()`.
    std::vector<std::size_t> boundaries{0};

    /// Visits `item` and its ingredients, closing every component whose root is reached.
    void visit(ItemIndex item_idx);
};

void Tarjan::visit(ItemIndex item_idx) {
    index[item_idx] = next_index;
    low[item_idx] = next_index;
    ++next_index;
    stack.push_back(item_idx);
    on_stack[item_idx] = true;

    for (const RecipeIndex recipe_idx : items[item_idx].produced_by) {
        for (const Ingredient& ingredient : recipes[recipe_idx].ingredients) {
            const ItemIndex next_idx = ingredient.item;
            if (!index[next_idx]) {
                visit(next_idx);
                low[item_idx] = std::min(low[item_idx], low[next_idx]);
            } else if (on_stack[next_idx]) {
                low[item_idx] = std::min(low[item_idx], *index[next_idx]);
            }
        }
    }

    if (low[item_idx] == *index[item_idx]) {
        ItemIndex top_stack = 0;
        do {
            top_stack = stack.back();
            stack.pop_back();
            on_stack[top_stack] = false;
            order.push_back(top_stack);
        } while (top_stack != item_idx);
        boundaries.push_back(order.size());
    }
}

} // namespace

Catalog Catalog::build(const BazaarSnapshot& bazaar, const std::vector<NeuRecipe>& recipes) {
    Catalog catalog;

    for (const BazaarProduct& product : bazaar.products) {
        catalog.set_market(catalog.intern(product.id), product.market);
    }

    for (const NeuRecipe& source : recipes) {
        Recipe recipe;
        recipe.output = catalog.intern(source.output);
        recipe.output_count = source.output_count;
        recipe.ingredients.reserve(source.ingredients.size());
        for (const ItemCount& ingredient : source.ingredients) {
            recipe.ingredients.push_back({catalog.intern(ingredient.id), ingredient.count});
        }
        catalog.add_recipe(std::move(recipe));
    }

    catalog.compute_components();
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

    order_.clear(); // components computed earlier are no longer valid
    boundaries_.clear();

    const RecipeIndex index = recipes_.size();
    items_[recipe.output].produced_by.push_back(index);
    for (const Ingredient& ingredient : recipe.ingredients) {
        items_[ingredient.item].used_in.push_back(index);
    }
    recipes_.push_back(std::move(recipe));
    return index;
}

void Catalog::compute_components() {
    const std::size_t n = items_.size();
    Tarjan tarjan{.items = items_,
                  .recipes = recipes_,
                  .index = std::vector<std::optional<std::size_t>>(n),
                  .low = std::vector<std::size_t>(n),
                  .on_stack = std::vector<bool>(n)};

    for (ItemIndex item = 0; item < n; ++item) {
        if (!tarjan.index[item]) {
            tarjan.visit(item);
        }
    }

    order_ = std::move(tarjan.order);
    boundaries_ = std::move(tarjan.boundaries);
}

} // namespace bz
