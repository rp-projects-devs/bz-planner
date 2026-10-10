#include "http.hpp"

#include <httplib.h>

#include <stdexcept>

namespace bz::app {

std::string fetch_bazaar_json(std::chrono::seconds timeout) {
    httplib::Client client{std::string{kApiBaseUrl}};
    client.set_connection_timeout(timeout);
    client.set_read_timeout(timeout);
    client.set_follow_location(true);

    const httplib::Headers headers{{"User-Agent", "bz-planner/" BZ_VERSION}};
    auto response = client.Get(std::string{kBazaarPath}, headers);
    if (!response) {
        throw std::runtime_error("Bazaar request failed: " + httplib::to_string(response.error()));
    }
    if (response->status != 200) {
        throw std::runtime_error("Bazaar request returned HTTP " +
                                 std::to_string(response->status));
    }
    return std::move(response->body);
}

} // namespace bz::app
