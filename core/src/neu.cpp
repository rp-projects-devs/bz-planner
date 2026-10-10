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
