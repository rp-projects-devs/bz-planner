#include "commands.hpp"

#include "http.hpp"

#include "bz/bazaar.hpp"
#include "bz/catalog.hpp"
#include "bz/neu.hpp"

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

std::filesystem::path bazaar_cache_file(const GlobalOptions& global) {
    return global.data_dir / "cache" / "bazaar.json";
}

BazaarSnapshot load_bazaar(const GlobalOptions& global, bool force_refresh) {
    const BazaarPayload payload =
        load_bazaar_json(bazaar_cache_file(global), global.cache_max_age, force_refresh);
    if (payload.stale) {
        std::cerr << "warning: Bazaar API unreachable, using stale cached prices\n";
    }
    return parse_bazaar(payload.json);
}

Catalog load_catalog(const GlobalOptions& global) {
    const BazaarSnapshot bazaar = load_bazaar(global, /*force_refresh=*/false);
    const NeuLoadResult neu = load_neu_recipes(neu_items_dir(global));
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
    const BazaarSnapshot bazaar = load_bazaar(global, /*force_refresh=*/true);
    std::cout << "Bazaar: " << bazaar.products.size() << " products\n";

    if (!std::filesystem::is_directory(neu_items_dir(global))) {
        std::cout << "NEU:    missing\n";
    } else {
        const NeuLoadResult neu = load_neu_recipes(neu_items_dir(global));
        std::cout << "NEU:    " << neu.recipes.size() << " recipes from " << neu.files_read
                  << " files (" << neu.files_skipped << " skipped)\n";
    }
    std::cout << "NEU recipes are refreshed with: cmake --build <build-dir> --target neu-update\n";
    return EXIT_SUCCESS;
}

} // namespace bz::app
