#include "bz/neu.hpp"

#include "bz/util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace bz {

namespace {

using nlohmann::json;

constexpr std::array<std::string_view, 9> kGridSlots{"A1", "A2", "A3", "B1", "B2",
                                                     "B3", "C1", "C2", "C3"};

std::optional<std::string_view> string_field(const json& node, std::string_view key) {
    const auto it = node.find(key);
    if (it == node.end() || !it->is_string()) {
        return std::nullopt;
    }
    return std::string_view{it->get_ref<const std::string&>()};
}

/// Reads an optional positive count stored either as a number or as a numeric string.
std::int64_t count_field(const json& node, std::string_view key) {
    const auto it = node.find(key);
    if (it == node.end()) {
        return 1;
    }
    if (it->is_number_integer()) {
        return it->get<std::int64_t>();
    }
    if (const auto text = string_field(node, key)) {
        std::int64_t value = 0;
        const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
        if (error == std::errc{} && end == text->data() + text->size()) {
            return value;
        }
    }
    return 1;
}

void merge_ingredient(std::vector<ItemCount>& ingredients, ItemCount entry) {
    const auto it =
        std::find_if(ingredients.begin(), ingredients.end(),
                     [&](const ItemCount& existing) { return existing.id == entry.id; });
    if (it != ingredients.end()) {
        it->count += entry.count;
    } else {
        ingredients.push_back(std::move(entry));
    }
}

std::optional<NeuRecipe> parse_crafting(const json& node, std::string_view default_output) {
    NeuRecipe recipe;
    recipe.kind = RecipeKind::Crafting;
    recipe.output =
        normalize_item_id(string_field(node, "overrideOutputId").value_or(default_output));
    recipe.output_count = count_field(node, "count");

    for (const std::string_view slot : kGridSlots) {
        if (const auto entry = string_field(node, slot)) {
            if (auto parsed = parse_item_count(*entry)) {
                merge_ingredient(recipe.ingredients, std::move(*parsed));
            }
        }
    }

    if (recipe.output.empty() || recipe.output_count <= 0 || recipe.ingredients.empty()) {
        return std::nullopt;
    }
    return recipe;
}

std::optional<NeuRecipe> parse_npc_shop(const json& node) {
    const auto result_text = string_field(node, "result");
    const auto result = result_text ? parse_item_count(*result_text) : std::nullopt;
    const auto costs = node.find("cost");
    if (!result || costs == node.end() || !costs->is_array()) {
        return std::nullopt;
    }

    NeuRecipe recipe;
    recipe.kind = RecipeKind::NpcShop;
    recipe.output = result->id;
    recipe.output_count = result->count;

    for (const json& cost : *costs) {
        if (!cost.is_string()) {
            continue;
        }
        auto parsed = parse_item_count(cost.get_ref<const std::string&>());
        if (!parsed) {
            continue;
        }
        if (parsed->id == kCoinId) {
            recipe.coin_cost += static_cast<double>(parsed->count);
        } else {
            merge_ingredient(recipe.ingredients, std::move(*parsed));
        }
    }

    if (recipe.coin_cost <= 0.0 && recipe.ingredients.empty()) {
        return std::nullopt;
    }
    return recipe;
}

} // namespace

std::optional<ItemCount> parse_item_count(std::string_view entry) {
    entry = trim(entry);
    std::string_view id = entry;
    std::int64_t count = 1;

    if (const auto separator = entry.rfind(':'); separator != std::string_view::npos) {
        const std::string_view suffix = entry.substr(separator + 1);
        std::int64_t value = 0;
        const auto [end, error] =
            std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
        if (!suffix.empty() && error == std::errc{} && end == suffix.data() + suffix.size()) {
            id = entry.substr(0, separator);
            count = value;
        }
    }

    std::string normalized = normalize_item_id(id);
    if (normalized.empty() || count <= 0) {
        return std::nullopt;
    }
    return ItemCount{std::move(normalized), count};
}

std::vector<NeuRecipe> parse_neu_item(std::string_view json_text) {
    const json document = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded() || !document.is_object()) {
        throw std::runtime_error("NEU item is not a JSON object");
    }

    const std::string_view internal_name = string_field(document, "internalname").value_or("");
    std::vector<NeuRecipe> recipes;

    if (const auto it = document.find("recipe"); it != document.end() && it->is_object()) {
        if (auto recipe = parse_crafting(*it, internal_name)) {
            recipes.push_back(std::move(*recipe));
        }
    }

    if (const auto it = document.find("recipes"); it != document.end() && it->is_array()) {
        for (const json& node : *it) {
            if (!node.is_object()) {
                continue;
            }
            const std::string_view type = string_field(node, "type").value_or("crafting");
            std::optional<NeuRecipe> recipe;
            if (type == "crafting") {
                recipe = parse_crafting(node, internal_name);
            } else if (type == "npc_shop") {
                recipe = parse_npc_shop(node);
            }
            if (recipe) {
                recipes.push_back(std::move(*recipe));
            }
        }
    }

    return recipes;
}

NeuLoadResult load_neu_recipes(const std::filesystem::path& items_dir) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(items_dir)) {
        throw std::runtime_error("NEU items directory not found: " + items_dir.string());
    }

    // Sorting keeps item and recipe indices stable from one run to the next.
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator{items_dir}) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    NeuLoadResult result;
    for (const fs::path& file : files) {
        try {
            auto recipes = parse_neu_item(read_text_file(file));
            std::move(recipes.begin(), recipes.end(), std::back_inserter(result.recipes));
            ++result.files_read;
        } catch (const std::runtime_error&) {
            ++result.files_skipped;
        }
    }
    return result;
}

} // namespace bz
