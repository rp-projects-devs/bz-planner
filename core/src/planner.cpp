#include "bz/planner.hpp"

#include <stdexcept>

namespace bz {

// TODO(v1): binary search on the quantity. A quantity is feasible when the rounded-up
// ingredient orders fit in order_slots, their cost plus coins fits in budget, and it does not
// exceed liquidity_share × buy_moving_week of the target.
std::int64_t max_quantity([[maybe_unused]] const Catalog& catalog,
                          [[maybe_unused]] ItemIndex target, [[maybe_unused]] const CraftPath& path,
                          [[maybe_unused]] const PricingConfig& pricing,
                          [[maybe_unused]] const Constraints& constraints) {
    throw std::logic_error("bz::max_quantity is not implemented yet");
}

// TODO(v1): size the plan with max_quantity(), split each purchase into orders of at most
// kMaxItemsPerOrder units, and report which limits block quantity + 1.
Plan make_plan([[maybe_unused]] const Catalog& catalog, [[maybe_unused]] ItemIndex target,
               [[maybe_unused]] const CraftPath& path,
               [[maybe_unused]] const PricingConfig& pricing,
               [[maybe_unused]] const Constraints& constraints) {
    throw std::logic_error("bz::make_plan is not implemented yet");
}

} // namespace bz
