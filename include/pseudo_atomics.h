#pragma once

// Thin wrapper around RP2040Atomic.hpp

#if !PICO_NO_HARDWARE
#include "third_party/RP2040_Pseudo_Atomic/Inc/RP2040Atomic.hpp"

inline void initPseudoAtomics() { patom::PseudoAtomicInit(); }

template <patom::internal::atomic_t T>
using pseudoatomic = patom::PseudoAtomic<T>;
#else
#include <atomic>
// Gutted version of the class from RP2040Atomic.hpp, all credits to the original author

inline void initPseudoAtomics() {}

template <typename T>
class pseudoatomic {
  public:
    pseudoatomic() = default;
    explicit pseudoatomic(T initial) : t_(initial) {}

    auto operator=(T t) -> pseudoatomic<T>& {
        std::atomic_thread_fence(std::memory_order_release);
        t_ = t;
        return *this;
    }

    auto Load() -> T {
        auto t = t_;
        std::atomic_thread_fence(std::memory_order_acquire);
        return t;
    }

    pseudoatomic(const pseudoatomic<T>&) = delete;
    pseudoatomic(pseudoatomic<T>&&) = delete;
    pseudoatomic<T>& operator=(const pseudoatomic<T>&) = delete;
    pseudoatomic<T>& operator=(pseudoatomic<T>&&) = delete;

  private:
    volatile T t_{};
};
#endif
