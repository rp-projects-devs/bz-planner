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

/// Recipe type accepted in the `recipes` array; an entry without `type` is a crafting grid.
constexpr std::string_view kCraftingType = "crafting";

/// Text present in every file holding a `recipe` or `recipes` key. Files without it are
/// skipped before JSON parsing, which avoids parsing about 60 % of the NEU repository.
constexpr std::string_view kRecipeMarker = "\"recipe";

std::optional<std::string_view> string_field(const json& node, std::string_view key) {
    const auto it = node.find(key);
    if (it == node.end() || !it->is_string()) {
        return std::nullopt;
    }
    return std::string_view{it->get_ref<const std::string&>()};
}

/**
 * Reads the output count like the reference: `int(raw.get("count", 1) or 1)`.
 * Missing, null, zero or unparsable values give 1; floats are truncated. Negative values,
 * which the reference would keep and turn into nonsense, are also clamped to 1.
 */
std::int64_t output_count_field(const json& node) {
    const auto it = node.find("count");
    if (it == node.end()) {
        return 1;
    }
    std::int64_t value = 0;
    if (it->is_number_integer()) {
        value = it->get<std::int64_t>();
    } else if (it->is_number_float()) {
        value = static_cast<std::int64_t>(it->get<double>());
    } else if (const auto text = string_field(node, "count")) {
        const std::string_view trimmed = trim(*text);
        const auto [end, error] =
            std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
        if (error != std::errc{} || end != trimmed.data() + trimmed.size()) {
            value = 0;
        }
    }
    return value > 0 ? value : 1;
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

/// Reads a crafting grid. The output is the item file itself, never another id.
std::optional<NeuRecipe> parse_crafting_grid(const json& node, const std::string& output) {
    NeuRecipe recipe;
    recipe.output = output;
    recipe.output_count = output_count_field(node);

    for (const std::string_view slot : kGridSlots) {
        if (const auto entry = string_field(node, slot)) {
            if (auto parsed = parse_item_count(*entry)) {
                merge_ingredient(recipe.ingredients, std::move(*parsed));
            }
        }
    }

    if (recipe.ingredients.empty()) {
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
        // Digits only, like Python's str.isdigit(): "ITEM:-3" is an id, not a count of -3.
        const bool all_digits =
            !suffix.empty() &&
            std::all_of(suffix.begin(), suffix.end(), [](char c) { return c >= '0' && c <= '9'; });
        std::int64_t value = 0;
        if (all_digits) {
            const auto [end, error] =
                std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
            if (error == std::errc{} && end == suffix.data() + suffix.size()) {
                id = entry.substr(0, separator);
                count = value;
            }
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

    const std::string output =
        normalize_item_id(string_field(document, "internalname").value_or(""));
    std::vector<NeuRecipe> recipes;
    if (output.empty()) {
        return recipes;
    }

    if (const auto it = document.find("recipe"); it != document.end() && it->is_object()) {
        if (auto recipe = parse_crafting_grid(*it, output)) {
            recipes.push_back(std::move(*recipe));
        }
    }

    if (const auto it = document.find("recipes"); it != document.end() && it->is_array()) {
        for (const json& node : *it) {
            if (!node.is_object()) {
                continue;
            }
            // npc_shop, forge, katgrade, trade, drops...: not a crafting grid, never priced.
            if (string_field(node, "type").value_or(kCraftingType) != kCraftingType) {
                continue;
            }
            if (auto recipe = parse_crafting_grid(node, output)) {
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

    // One result slot per file: each iteration writes only to its own slot.
    std::vector<std::vector<NeuRecipe>> per_file(files.size());
    std::size_t read = 0;
    std::size_t skipped = 0;

    // Most iterations are cheap once filtered, so files are handed out in small batches
    // to limit scheduling overhead while keeping the load balanced.
#pragma omp parallel for schedule(dynamic, 16) reduction(+ : read, skipped)
    for (std::size_t i = 0; i < files.size(); ++i) {
        try {
            const std::string text = read_text_file(files[i]);
            if (text.find(kRecipeMarker) != std::string::npos) {
                per_file[i] = parse_neu_item(text);
            }
            ++read;
        } catch (...) { // no exception may escape an OpenMP region
            ++skipped;
        }
    }

    // Concatenate in file order, so the result matches the sequential version.
    std::size_t total = 0;
    for (const std::vector<NeuRecipe>& recipes : per_file) {
        total += recipes.size();
    }

    NeuLoadResult result;
    result.files_read = read;
    result.files_skipped = skipped;
    result.recipes.reserve(total);
    for (std::vector<NeuRecipe>& recipes : per_file) {
        std::move(recipes.begin(), recipes.end(), std::back_inserter(result.recipes));
    }
    return result;
}

std::string serialize_recipe_cache(const NeuLoadResult& result) {
    json recipes = json::object();
    for (const NeuRecipe& recipe : result.recipes) {
        json ingredients = json::object();
        for (const ItemCount& ingredient : recipe.ingredients) {
            ingredients[ingredient.id] = ingredient.count;
        }
        recipes[recipe.output].push_back(
            {{"count", recipe.output_count}, {"ingredients", std::move(ingredients)}});
    }

    const json document = {{"format", kRecipeCacheFormat},
                           {"files_read", result.files_read},
                           {"files_skipped", result.files_skipped},
                           {"recipes", std::move(recipes)}};
    return document.dump(1);
}

NeuLoadResult parse_recipe_cache(std::string_view json_text) {
    const json document = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded() || !document.is_object()) {
        throw std::runtime_error("recipe cache is not a JSON object");
    }

    try {
        if (document.at("format").get<int>() != kRecipeCacheFormat) {
            throw std::runtime_error("recipe cache was written by another version");
        }

        NeuLoadResult result;
        result.files_read = document.at("files_read").get<std::size_t>();
        result.files_skipped = document.at("files_skipped").get<std::size_t>();

        for (const auto& [output, recipes] : document.at("recipes").items()) {
            for (const json& node : recipes) {
                NeuRecipe recipe;
                recipe.output = output;
                recipe.output_count = node.at("count").get<std::int64_t>();
                for (const auto& [id, count] : node.at("ingredients").items()) {
                    recipe.ingredients.push_back({id, count.get<std::int64_t>()});
                }
                const bool valid =
                    !recipe.output.empty() && recipe.output_count > 0 &&
                    !recipe.ingredients.empty() &&
                    std::ranges::all_of(recipe.ingredients, [](const ItemCount& ingredient) {
                        return !ingredient.id.empty() && ingredient.count > 0;
                    });
                if (!valid) {
                    throw std::runtime_error("recipe cache holds an invalid recipe for " + output);
                }
                result.recipes.push_back(std::move(recipe));
            }
        }
        return result;
    } catch (const json::exception& error) {
        throw std::runtime_error(std::string{"recipe cache is malformed: "} + error.what());
    }
}

} // namespace bz
