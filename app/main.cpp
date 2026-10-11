/**
 * @file main.cpp
 * @brief Entry point of the `bz` command-line tool: argument parsing and dispatch.
 */
#include "commands.hpp"

#include "bz/util.hpp"

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <string>

namespace {

/// Validates amounts such as `50m` or `2.5b` (see bz::parse_coins()).
const CLI::Validator kCoinAmount{[](std::string& value) {
                                     return bz::parse_coins(value)
                                                ? std::string{}
                                                : "invalid coin amount: " + value;
                                 },
                                 "AMOUNT"};

void add_path_options(CLI::App& command, bz::PathOptions& paths) {
    command.add_option("--depth", paths.max_depth, "Recipe levels explored below the target")
        ->check(CLI::Range(1, 8))
        ->capture_default_str();
    command.add_option("--paths", paths.top_k, "Paths kept per item")
        ->check(CLI::Range(1, 1000))
        ->capture_default_str();
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"Hypixel SkyBlock Bazaar craft-flip planner", "bz"};
    app.set_version_flag("--version", BZ_VERSION);
    app.require_subcommand(1);

    bz::app::GlobalOptions global;
    std::string data_dir = BZ_DEFAULT_DATA_DIR;
    bool no_color = false;
    int cache_seconds = static_cast<int>(global.cache_max_age.count());

    const std::map<std::string, bz::BuyMode> buy_modes{{"instant", bz::BuyMode::Instant},
                                                       {"order", bz::BuyMode::Order}};
    const std::map<std::string, bz::SellMode> sell_modes{{"instant", bz::SellMode::Instant},
                                                         {"offer", bz::SellMode::Offer}};

    app.add_option("--data-dir", data_dir, "Directory holding NEU data and caches")
        ->capture_default_str();
    app.add_option("--buy", global.pricing.buy, "How ingredients are bought: instant | order")
        ->transform(CLI::CheckedTransformer(buy_modes, CLI::ignore_case).description(""))
        ->type_name("MODE")
        ->default_str("order");
    app.add_option("--sell", global.pricing.sell, "How the output is sold: instant | offer")
        ->transform(CLI::CheckedTransformer(sell_modes, CLI::ignore_case).description(""))
        ->type_name("MODE")
        ->default_str("offer");
    app.add_option("--tax", global.pricing.tax_rate, "Bazaar sales tax as a fraction")
        ->check(CLI::Range(0.0, 0.5))
        ->capture_default_str();
    app.add_option("--cache-age", cache_seconds,
                   "Warn when cached prices are older than this, in seconds")
        ->check(CLI::NonNegativeNumber)
        ->capture_default_str();
    app.add_flag("--no-color", no_color, "Disable colored output (also honors NO_COLOR)");

    // bz scan
    bz::app::ScanOptions scan_options;
    double min_margin_percent = 0.0;
    std::optional<std::string> max_cost;
    CLI::App* scan = app.add_subcommand("scan", "Rank the most profitable crafts");
    scan->add_option("--min-volume", scan_options.filters.min_weekly_volume,
                     "Minimum weekly volume bought of the crafted item")
        ->check(CLI::NonNegativeNumber)
        ->capture_default_str();
    scan->add_option("--min-margin", min_margin_percent, "Minimum profit margin, in percent")
        ->capture_default_str();
    scan->add_option("--max-cost", max_cost, "Maximum cost of one craft (e.g. 2.5m)")
        ->check(kCoinAmount);
    scan->add_option("--top", scan_options.filters.top, "Number of results")
        ->check(CLI::Range(1, 1000))
        ->capture_default_str();
    add_path_options(*scan, scan_options.paths);

    // bz item <ID>
    bz::app::ItemOptions item_options;
    CLI::App* item = app.add_subcommand("item", "Show prices, recipe tree and craft paths");
    item->add_option("id", item_options.id, "Item id (e.g. ENCHANTED_COCOA)")->required();
    add_path_options(*item, item_options.paths);

    // bz plan <ID>
    bz::app::PlanOptions plan_options;
    std::optional<int> slots;
    std::optional<std::string> budget;
    std::optional<double> liquidity_percent;
    CLI::App* plan =
        app.add_subcommand("plan", "Size a craft under slot, budget and volume limits");
    plan->add_option("id", plan_options.id, "Item id (e.g. ENCHANTED_COCOA)")->required();
    plan->add_option("--slots", slots,
                     "Bazaar order slots available (21 if no limit is given at all)")
        ->check(CLI::Range(1, 100));
    plan->add_option("--budget", budget, "Coins available (e.g. 50m)")->check(kCoinAmount);
    plan->add_option("--liquidity", liquidity_percent,
                     "Max share of the item's weekly volume, in percent")
        ->check(CLI::Range(0.0, 100.0));
    add_path_options(*plan, plan_options.paths);

    // bz update
    CLI::App* update = app.add_subcommand("update", "Download fresh Bazaar prices");

    CLI::App* neu_cache =
        app.add_subcommand("neu-cache", "Rebuild the recipe cache from the NEU repository");

    CLI11_PARSE(app, argc, argv);

    global.data_dir = data_dir;
    global.cache_max_age = std::chrono::seconds{cache_seconds};
    global.style.color = !no_color && std::getenv("NO_COLOR") == nullptr;

    scan_options.filters.min_margin = min_margin_percent / 100.0;
    if (max_cost) {
        scan_options.filters.max_unit_cost = bz::parse_coins(*max_cost);
    }
    // Like the reference, each limit is optional: a budget alone must not be capped by slots.
    plan_options.constraints.order_slots = slots;
    if (budget) {
        plan_options.constraints.budget = bz::parse_coins(*budget);
    }
    if (liquidity_percent) {
        plan_options.constraints.liquidity_share = *liquidity_percent / 100.0;
    }
    if (!slots && !budget && !liquidity_percent) {
        plan_options.constraints.order_slots = bz::kDefaultOrderSlots;
    }

    try {
        if (*scan) {
            return bz::app::run_scan(global, scan_options);
        }
        if (*item) {
            return bz::app::run_item(global, item_options);
        }
        if (*plan) {
            return bz::app::run_plan(global, plan_options);
        }
        if (*update) {
            return bz::app::run_update(global);
        }
        if (*neu_cache) {
            return bz::app::run_neu_cache(global);
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}
