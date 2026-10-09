/**
 * @file display.hpp
 * @brief Human-readable terminal output: summaries, recipe trees and result tables.
 */
#pragma once

#include "bz/catalog.hpp"
#include "bz/planner.hpp"
#include "bz/pricing.hpp"
#include "bz/scanner.hpp"
#include "bz/solver.hpp"

#include <optional>
#include <ostream>
#include <span>
#include <string>

namespace bz::app {

/// Output styling options.
struct Style {
    bool color = true; ///< Emit ANSI color codes.
};

/**
 * @brief Formats a coin amount compactly (`950.0`, `12.35k`, `1.25M`, `3.40B`).
 */
[[nodiscard]] std::string format_coins(double coins);

/// Prints market data and graph statistics of an item.
void print_item_summary(std::ostream& out, const Catalog& catalog, ItemIndex item,
                        const PricingConfig& pricing, const Style& style);

/**
 * @brief Prints the recipes of an item as a tree.
 * @param max_depth Number of recipe levels shown below @p item.
 */
void print_recipe_tree(std::ostream& out, const Catalog& catalog, ItemIndex item, int max_depth,
                       const PricingConfig& pricing, const Style& style);

/**
 * @brief Prints ranked craft paths.
 * @param unit_revenue Sale price of the target after tax; profits are omitted when empty.
 */
void print_paths(std::ostream& out, const Catalog& catalog, std::span<const CraftPath> paths,
                 std::optional<double> unit_revenue, const Style& style);

/// Prints a plan and its order breakdown.
void print_plan(std::ostream& out, const Catalog& catalog, const Plan& plan, const Style& style);

/// Prints scan results as a table.
void print_scan(std::ostream& out, const Catalog& catalog, std::span<const ScanResult> results,
                const Style& style);

} // namespace bz::app
