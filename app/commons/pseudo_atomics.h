// pseudo_atomics.h - Lightweight helpers offering atomic-like semantics for cross-core data.
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
    auto operator=(T t) -> pseudoatomic<T>& {
        // To-do: Need a barrier
        t_ = t;
        return *this;
    }

    auto Load() -> T {
        // To-do: Need a barrier
        auto t = t_;
        return t;
    }

    pseudoatomic() = default;

    pseudoatomic(const pseudoatomic<T>&) = delete;
    pseudoatomic(pseudoatomic<T>&&) = delete;
    pseudoatomic<T>& operator=(const pseudoatomic<T>&) = delete;
    pseudoatomic<T>& operator=(pseudoatomic<T>&&) = delete;

  private:
    volatile T t_;
};
#endif
