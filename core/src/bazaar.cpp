#include "bz/bazaar.hpp"

#include "bz/util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace bz {

namespace {

using nlohmann::json;

double number_field(const json& node, std::string_view key) {
    const auto it = node.find(key);
    return (it != node.end() && it->is_number()) ? it->get<double>() : 0.0;
}

std::int64_t integer_field(const json& node, std::string_view key) {
    const auto it = node.find(key);
    return (it != node.end() && it->is_number()) ? it->get<std::int64_t>() : 0;
}

} // namespace

BazaarSnapshot parse_bazaar(std::string_view json_text) {
    const json document = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded() || !document.is_object()) {
        throw std::runtime_error("Bazaar response is not a JSON object");
    }
    if (const auto it = document.find("success");
        it != document.end() && it->is_boolean() && !it->get<bool>()) {
        throw std::runtime_error("Bazaar API reported a failure");
    }
    const auto products = document.find("products");
    if (products == document.end() || !products->is_object()) {
        throw std::runtime_error("Bazaar response has no 'products' object");
    }

    BazaarSnapshot snapshot;
    snapshot.last_updated_ms = integer_field(document, "lastUpdated");
    snapshot.products.reserve(products->size());

    for (const auto& [product_id, product] : products->items()) {
        const auto status = product.find("quick_status");
        if (status == product.end() || !status->is_object()) {
            continue;
        }
        snapshot.products.push_back(BazaarProduct{
            .id = normalize_item_id(product_id),
            .market = MarketData{.buy_price = number_field(*status, "buyPrice"),
                                 .sell_price = number_field(*status, "sellPrice"),
                                 .buy_moving_week = integer_field(*status, "buyMovingWeek"),
                                 .sell_moving_week = integer_field(*status, "sellMovingWeek")},
        });
    }

    std::sort(snapshot.products.begin(), snapshot.products.end(),
              [](const BazaarProduct& a, const BazaarProduct& b) { return a.id < b.id; });
    return snapshot;
}

} // namespace bz
