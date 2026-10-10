#include "bz/solver.hpp"

#include <stdexcept>

namespace bz {

// TODO(v1): initialize every entry with the Bazaar buy cost, then repeatedly
// relax each recipe (sum of ingredient costs) / output_count until no entry
// improves.
CostTable compute_min_costs([[maybe_unused]] const Catalog& catalog,
                            [[maybe_unused]] const PricingConfig& pricing,
                            [[maybe_unused]] int max_iterations) {
    throw std::logic_error("bz::best_paths is not implemented yet");
}

// TODO(v1): expand the target's recipes recursively, keeping only the top_k
// cheapest partial paths per item and depth, and skipping items already on the
// current branch (cycles).
std::vector<CraftPath> best_paths([[maybe_unused]] const Catalog& catalog,
                                  [[maybe_unused]] ItemIndex target,
                                  [[maybe_unused]] const PricingConfig& pricing,
                                  [[maybe_unused]] const PathOptions& options) {
    throw std::logic_error("bz::best_paths is not implemented yet");
}

} // namespace bz
