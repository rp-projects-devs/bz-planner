#include "display.hpp"

#include <cmath>
#include <format>
#include <string_view>
#include <vector>

namespace bz::app {

namespace {

enum class Color { Red, Green, Yellow, Dim, Bold };

std::string paint(std::string_view text, Color color, const Style& style) {
    if (!style.color) {
        return std::string{text};
    }
    std::string_view code;
    switch (color) {
    case Color::Red:
        code = "\033[31m";
        break;
    case Color::Green:
        code = "\033[32m";
        break;
    case Color::Yellow:
        code = "\033[33m";
        break;
    case Color::Dim:
        code = "\033[2m";
        break;
    case Color::Bold:
        code = "\033[1m";
        break;
    }
    return std::format("{}{}\033[0m", code, text);
}

/// Colors an amount by sign; padding is applied before coloring so columns stay aligned.
std::string signed_coins(double coins, const Style& style, std::size_t width = 0) {
    return paint(std::format("{:>{}}", format_coins(coins), width),
                 coins >= 0.0 ? Color::Green : Color::Red, style);
}

std::string price_tag(const Item& item, const PricingConfig& pricing, const Style& style) {
    if (!item.market) {
        return paint("[not on Bazaar]", Color::Dim, style);
    }
    const auto cost = unit_buy_cost(item, pricing.buy);
    const auto revenue = unit_sell_revenue(item, pricing.sell, pricing.tax_rate);
    return paint(std::format("[buy {} | sell {}]", cost ? format_coins(*cost) : "-",
                             revenue ? format_coins(*revenue) : "-"),
                 Color::Dim, style);
}

std::string_view limit_name(Limit limit) {
    switch (limit) {
    case Limit::OrderSlots:
        return "order slots";
    case Limit::Budget:
        return "budget";
    case Limit::Liquidity:
        return "liquidity";
    }
    return "unknown";
}

/// Appends the items crafted by @p step, ingredients before the items made from them.
void crafted_items(const Catalog& catalog, const CraftStep& step, std::vector<std::string>& ids) {
    for (const CraftStep& input : step.inputs) {
        crafted_items(catalog, input, ids);
    }
    ids.push_back(catalog.item(catalog.recipe(step.recipe).output).id);
}

void print_tree_level(std::ostream& out, const Catalog& catalog, ItemIndex item,
                      const std::string& prefix, int depth_left, std::vector<bool>& on_branch,
                      const PricingConfig& pricing, const Style& style) {
    const auto& recipes = catalog.item(item).produced_by;
    on_branch[item] = true;

    for (std::size_t r = 0; r < recipes.size(); ++r) {
        const Recipe& recipe = catalog.recipe(recipes[r]);
        const bool last_recipe = r + 1 == recipes.size();

        out << prefix << (last_recipe ? "└── " : "├── ")
            << paint(std::format("craft (yields {})", recipe.output_count), Color::Yellow, style)
            << '\n';

        const std::string child_prefix = prefix + (last_recipe ? "    " : "│   ");
        const std::size_t lines = recipe.ingredients.size();
        std::size_t line = 0;

        for (const Ingredient& ingredient : recipe.ingredients) {
            const bool last = ++line == lines;
            const Item& child = catalog.item(ingredient.item);
            out << child_prefix << (last ? "└── " : "├── ") << ingredient.count << " × " << child.id
                << ' ' << price_tag(child, pricing, style);

            if (on_branch[ingredient.item]) {
                out << ' ' << paint("(cycle)", Color::Dim, style) << '\n';
                continue;
            }
            out << '\n';
            if (depth_left > 1) {
                print_tree_level(out, catalog, ingredient.item,
                                 child_prefix + (last ? "    " : "│   "), depth_left - 1, on_branch,
                                 pricing, style);
            }
        }
    }

    on_branch[item] = false;
}

} // namespace

std::string format_coins(double coins) {
    const double magnitude = std::abs(coins);
    if (magnitude >= 1e9) {
        return std::format("{:.2f}B", coins / 1e9);
    }
    if (magnitude >= 1e6) {
        return std::format("{:.2f}M", coins / 1e6);
    }
    if (magnitude >= 1e3) {
        return std::format("{:.2f}k", coins / 1e3);
    }
    return std::format("{:.1f}", coins);
}

void print_item_summary(std::ostream& out, const Catalog& catalog, ItemIndex item,
                        const PricingConfig& pricing, const Style& style) {
    const Item& data = catalog.item(item);
    out << paint(data.id, Color::Bold, style) << '\n';

    if (data.market) {
        const MarketData& market = *data.market;
        out << std::format("  Instant buy   {:>12}   Instant sell  {:>12}\n",
                           format_coins(market.buy_price), format_coins(market.sell_price))
            << std::format("  Bought / week {:>12}   Sold / week   {:>12}\n",
                           market.buy_moving_week, market.sell_moving_week);
        const auto cost = unit_buy_cost(market, pricing.buy);
        const auto revenue = unit_sell_revenue(market, pricing.sell, pricing.tax_rate);
        out << std::format("  Your buy cost {:>12}   Your revenue  {:>12}\n",
                           cost ? format_coins(*cost) : "-",
                           revenue ? format_coins(*revenue) : "-");
    } else {
        out << "  " << paint("Not sold on the Bazaar", Color::Dim, style) << '\n';
    }
    out << std::format("  Recipes: {}   Used in: {} recipes\n", data.produced_by.size(),
                       data.used_in.size());
}

void print_recipe_tree(std::ostream& out, const Catalog& catalog, ItemIndex item, int max_depth,
                       const PricingConfig& pricing, const Style& style) {
    out << catalog.item(item).id << ' ' << price_tag(catalog.item(item), pricing, style) << '\n';
    if (catalog.item(item).produced_by.empty()) {
        out << "└── " << paint("no known recipe", Color::Dim, style) << '\n';
        return;
    }
    std::vector<bool> on_branch(catalog.items().size(), false);
    print_tree_level(out, catalog, item, "", max_depth, on_branch, pricing, style);
}

void print_paths(std::ostream& out, const Catalog& catalog, std::span<const CraftPath> paths,
                 std::optional<double> unit_revenue, const Style& style) {
    if (paths.empty()) {
        out << paint("No craft path found.", Color::Red, style) << '\n';
        return;
    }
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const CraftPath& path = paths[i];
        out << paint(std::format("#{}", i + 1), Color::Bold, style) << "  cost "
            << format_coins(path.unit_cost);
        if (unit_revenue) {
            const double profit = *unit_revenue - path.unit_cost;
            out << "  profit " << signed_coins(profit, style);
            if (path.unit_cost > 0.0) {
                out << std::format("  margin {:.1f}%", 100.0 * profit / path.unit_cost);
            }
        }
        out << '\n';
        for (const Purchase& purchase : path.purchases) {
            out << std::format("    {:>10.2f} × {}\n", purchase.amount,
                               catalog.item(purchase.item).id);
        }
        if (path.craft) {
            std::vector<std::string> ids;
            crafted_items(catalog, *path.craft, ids);
            std::string chain;
            for (const std::string& id : ids) {
                chain += (chain.empty() ? "" : " → ") + id;
            }
            out << "    " << paint("craft " + chain, Color::Dim, style) << '\n';
        }
    }
}

void print_plan(std::ostream& out, const Catalog& catalog, const Plan& plan, const Style& style) {
    out << std::format("Quantity       {}\n", plan.quantity)
        << std::format("Orders used    {}\n", plan.slots_used)
        << std::format("Total cost     {}\n", format_coins(plan.total_cost))
        << std::format("Total revenue  {}\n", format_coins(plan.total_revenue)) << "Profit         "
        << signed_coins(plan.profit, style) << '\n';

    out << "Limited by     ";
    if (plan.binding_limits.empty()) {
        out << "nothing";
    }
    for (std::size_t i = 0; i < plan.binding_limits.size(); ++i) {
        out << (i > 0 ? ", " : "") << limit_name(plan.binding_limits[i]);
    }
    out << "\n\n" << paint("Orders", Color::Bold, style) << '\n';

    for (const OrderLine& line : plan.orders) {
        out << std::format("  {:<32} {:>12} units  {:>3} orders  @ {:>10}  = {:>10}\n",
                           catalog.item(line.item).id, line.quantity, line.slot_amounts.size(),
                           format_coins(line.unit_price), format_coins(line.total_cost));
    }

    if (!plan.crafts.empty()) {
        out << '\n' << paint("Crafts", Color::Bold, style) << '\n';
        for (const CraftLine& line : plan.crafts) {
            const Recipe& recipe = catalog.recipe(line.recipe);
            out << std::format("  {:<32} {:>12} crafts  → {} units\n",
                               catalog.item(recipe.output).id, line.crafts,
                               line.crafts * recipe.output_count);
        }
    }
}

void print_scan(std::ostream& out, const Catalog& catalog, std::span<const ScanResult> results,
                const Style& style) {
    if (results.empty()) {
        out << paint("No profitable craft matches the filters.", Color::Red, style) << '\n';
        return;
    }
    if (!results.front().plan) {
        out << paint(std::format("{:>4}  {:<36} {:>10} {:>10} {:>10} {:>8}", "#", "Item", "Cost",
                                 "Revenue", "Profit", "Margin"),
                     Color::Bold, style)
            << '\n';
        for (std::size_t i = 0; i < results.size(); ++i) {
            const ScanResult& result = results[i];
            out << std::format("{:>4}  {:<36} {:>10} {:>10} ", i + 1, catalog.item(result.item).id,
                               format_coins(result.path.unit_cost),
                               format_coins(result.unit_revenue))
                << signed_coins(result.unit_profit, style, 10)
                << std::format(" {:>7.1f}%\n", 100.0 * result.margin);
        }
        return;
    }

    out << paint(std::format("{:>4}  {:<36} {:>9} {:>6} {:>10} {:>10} {:>8}  {}", "#", "Item",
                             "Quantity", "Orders", "Invested", "Profit", "Margin", "Limited by"),
                 Color::Bold, style)
        << '\n';
    for (std::size_t i = 0; i < results.size(); ++i) {
        const ScanResult& result = results[i];
        const Plan& plan = *result.plan;
        std::string limits;
        for (Limit limit : plan.binding_limits) {
            limits += (limits.empty() ? "" : ", ") + std::string{limit_name(limit)};
        }
        out << std::format("{:>4}  {:<36} {:>9} {:>6} {:>10} ", i + 1, catalog.item(result.item).id,
                           plan.quantity, plan.slots_used, format_coins(plan.total_cost))
            << signed_coins(plan.profit, style, 10)
            << std::format(" {:>7.1f}%  {}\n", 100.0 * result.margin,
                           limits.empty() ? "nothing" : limits);
    }
}

} // namespace bz::app
