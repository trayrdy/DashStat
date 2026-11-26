#include "model/vehicle_state.h"

VehicleStateManager::VehicleStateManager() {
    state_ = VehicleState{};
    seq_.store(0, std::memory_order_relaxed);
}

VehicleStateManager& VehicleStateManager::instance() {
    static VehicleStateManager inst;
    return inst;
}

void VehicleStateManager::update(const VehicleState& newState) {
    // Seqlock write: odd sequence = write in progress, acq_rel fence prevents state_ hoisting before odd seq
    uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acq_rel);
    state_ = newState;
    seq_.store(s + 2, std::memory_order_release);
}

VehicleState VehicleStateManager::getSnapshot() const {
    VehicleState copy{};
    for (;;) {
        uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1U) {
            continue;
        }
        copy = state_;
        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t s2 = seq_.load(std::memory_order_relaxed);
        if (s1 == s2) {
            return copy;
        }
    }
}
