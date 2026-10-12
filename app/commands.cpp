#include "commands.hpp"

#include "http.hpp"

#include "bz/bazaar.hpp"
#include "bz/catalog.hpp"
#include "bz/neu.hpp"
#include "bz/util.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>

namespace bz::app {

namespace {

std::filesystem::path neu_items_dir(const GlobalOptions& global) {
    return global.data_dir / "neu" / "items";
}

/// Recipes parsed from the NEU repository (see serialize_recipe_cache()). The `neu-update`
/// build target deletes and rebuilds this file, so keep its path in sync with FetchNeu.cmake.
std::filesystem::path recipe_cache_file(const GlobalOptions& global) {
    return global.data_dir / "cache" / "neu-recipes.json";
}

/// Parses the whole NEU repository and saves the result to the recipe cache.
NeuLoadResult rebuild_recipe_cache(const GlobalOptions& global) {
    if (!std::filesystem::is_directory(neu_items_dir(global))) {
        throw std::runtime_error("NEU repository not found in " + neu_items_dir(global).string() +
                                 ", run: cmake --build <build-dir> --target neu-update");
    }
    NeuLoadResult neu = load_neu_recipes(neu_items_dir(global));
    write_text_file(recipe_cache_file(global), serialize_recipe_cache(neu));
    return neu;
}

/// Reads the recipe cache, rebuilding it first if it is missing, unreadable or written by
/// another version.
NeuLoadResult load_recipes(const GlobalOptions& global) {
    const std::filesystem::path file = recipe_cache_file(global);
    if (std::filesystem::exists(file)) {
        try {
            return parse_recipe_cache(read_text_file(file));
        } catch (const std::runtime_error& error) {
            std::cerr << "warning: " << error.what() << ", rebuilding it\n";
        }
    }
    return rebuild_recipe_cache(global);
}

void print_recipe_stats(const NeuLoadResult& neu) {
    std::cout << "NEU:    " << neu.recipes.size() << " recipes from " << neu.files_read
              << " files (" << neu.files_skipped << " skipped)\n";
}

/// Compact copy of the last Bazaar response, written by `bz update` (see serialize_bazaar()).
std::filesystem::path bazaar_cache_file(const GlobalOptions& global) {
    return global.data_dir / "cache" / "bazaar-prices.json";
}

/// Reads the cached prices without any network access.
BazaarSnapshot load_cached_bazaar(const GlobalOptions& global) {
    const std::filesystem::path file = bazaar_cache_file(global);
    if (!std::filesystem::exists(file)) {
        throw std::runtime_error("no cached Bazaar prices in " + file.string() +
                                 ", run 'bz update' first");
    }
    BazaarSnapshot snapshot = parse_bazaar(read_text_file(file));

    if (snapshot.last_updated_ms > 0) {
        const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();
        const std::chrono::seconds age{(now_ms - snapshot.last_updated_ms) / 1000};
        if (age > global.cache_max_age) {
            std::cerr << "warning: cached prices are " << age.count() / 60
                      << " min old, run 'bz update' to refresh\n";
        }
    }
    return snapshot;
}

Catalog load_catalog(const GlobalOptions& global) {
    const BazaarSnapshot bazaar = load_cached_bazaar(global);
    const NeuLoadResult neu = load_recipes(global);
    return Catalog::build(bazaar, neu.recipes);
}

ItemIndex require_item(const Catalog& catalog, const std::string& id) {
    const auto index = catalog.find(id);
    if (!index) {
        throw std::invalid_argument("unknown item id: " + id);
    }
    return *index;
}

} // namespace

int run_scan(const GlobalOptions& global, const ScanOptions& options) {
    const Catalog catalog = load_catalog(global);
    const auto results = scan(catalog, global.pricing, options.filters, options.paths);
    print_scan(std::cout, catalog, results, global.style);
    return EXIT_SUCCESS;
}

int run_item(const GlobalOptions& global, const ItemOptions& options) {
    const Catalog catalog = load_catalog(global);
    const ItemIndex item = require_item(catalog, options.id);

    print_item_summary(std::cout, catalog, item, global.pricing, global.style);
    std::cout << '\n';
    print_recipe_tree(std::cout, catalog, item, options.paths.max_depth, global.pricing,
                      global.style);
    std::cout << '\n';

    const auto paths = best_paths(catalog, item, global.pricing, options.paths);
    const auto revenue =
        unit_sell_revenue(catalog.item(item), global.pricing.sell, global.pricing.tax_rate);
    print_paths(std::cout, catalog, paths, revenue, global.style);
    return EXIT_SUCCESS;
}

int run_plan(const GlobalOptions& global, const PlanOptions& options) {
    const Catalog catalog = load_catalog(global);
    const ItemIndex item = require_item(catalog, options.id);
    if (!unit_sell_revenue(catalog.item(item), global.pricing.sell, global.pricing.tax_rate)) {
        std::cerr << catalog.item(item).id << " cannot be sold on the Bazaar\n";
        return EXIT_FAILURE;
    }

    const auto paths = best_paths(catalog, item, global.pricing, options.paths);
    if (paths.empty()) {
        std::cerr << "no craft path found for " << catalog.item(item).id << '\n';
        return EXIT_FAILURE;
    }

    // The cheapest path per unit is not always the best under limits: a path with more
    // distinct ingredients uses more order slots. Like get_best_paths_limited() in the
    // reference, size every path and keep the one with the highest total profit.
    std::optional<Plan> best;
    for (const CraftPath& path : paths) {
        Plan plan = make_plan(catalog, item, path, global.pricing, options.constraints);
        if (plan.quantity > 0 && (!best || plan.profit > best->profit)) {
            best = std::move(plan);
        }
    }
    if (!best) {
        std::cerr << "no feasible plan for " << catalog.item(item).id
                  << " within the given limits\n";
        return EXIT_FAILURE;
    }
    print_plan(std::cout, catalog, *best, global.style);
    return EXIT_SUCCESS;
}

int run_update(const GlobalOptions& global) {
    // The only network access of the program. The full response is parsed once and only
    // the fields in use are cached, so other commands read a much smaller file.
    const BazaarSnapshot bazaar = parse_bazaar(fetch_bazaar_json());
    write_text_file(bazaar_cache_file(global), serialize_bazaar(bazaar));
    std::cout << "Bazaar: " << bazaar.products.size() << " products, saved to "
              << bazaar_cache_file(global).string() << '\n';

    if (!std::filesystem::exists(recipe_cache_file(global)) &&
        !std::filesystem::is_directory(neu_items_dir(global))) {
        std::cout << "NEU:    missing\n";
    } else {
        print_recipe_stats(load_recipes(global));
    }
    std::cout << "NEU recipes are refreshed with: cmake --build <build-dir> --target neu-update\n";
    return EXIT_SUCCESS;
}

int run_neu_cache(const GlobalOptions& global) {
    print_recipe_stats(rebuild_recipe_cache(global));
    std::cout << "Recipe cache saved to " << recipe_cache_file(global).string() << '\n';
    return EXIT_SUCCESS;
}

} // namespace bz::app
