#include "http.hpp"

#include "bz/util.hpp"

#include <httplib.h>

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace bz::app {

namespace {

namespace fs = std::filesystem;

bool is_fresh(const fs::path& file, std::chrono::seconds max_age) {
    std::error_code error;
    const auto modified = fs::last_write_time(file, error);
    if (error) {
        return false;
    }
    return fs::file_time_type::clock::now() - modified <= max_age;
}

/// Writes to a temporary file first so readers never observe a partial cache.
void write_file_atomically(const fs::path& file, const std::string& content) {
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path());
    }
    fs::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output) {
            throw std::runtime_error("cannot write cache file: " + temporary.string());
        }
    }
    fs::rename(temporary, file);
}

} // namespace

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

BazaarPayload load_bazaar_json(const fs::path& cache_file, std::chrono::seconds max_age,
                               bool force_refresh) {
    if (!force_refresh && is_fresh(cache_file, max_age)) {
        return {.json = read_text_file(cache_file), .from_cache = true, .stale = false};
    }

    try {
        std::string body = fetch_bazaar_json();
        write_file_atomically(cache_file, body);
        return {.json = std::move(body), .from_cache = false, .stale = false};
    } catch (const std::runtime_error&) {
        if (!fs::exists(cache_file)) {
            throw;
        }
        return {.json = read_text_file(cache_file), .from_cache = true, .stale = true};
    }
}

} // namespace bz::app
