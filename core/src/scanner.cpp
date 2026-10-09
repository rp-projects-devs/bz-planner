#include "bz/scanner.hpp"

#include <stdexcept>

namespace bz {

// TODO(v1): evaluate every craftable Bazaar item with best_paths() inside an OpenMP parallel
// loop. Path counts vary a lot between items, so compare schedule(static) and
// schedule(dynamic). Collect results per thread, then filter, sort and truncate.
std::vector<ScanResult> scan([[maybe_unused]] const Catalog& catalog,
                             [[maybe_unused]] const PricingConfig& pricing,
                             [[maybe_unused]] const ScanFilters& filters,
                             [[maybe_unused]] const PathOptions& options) {
    throw std::logic_error("bz::scan is not implemented yet");
}

} // namespace bz
