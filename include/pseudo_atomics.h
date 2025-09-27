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
        t_.store(t, std::memory_order_release);
        return *this;
    }

    [[nodiscard]] auto Load() const -> T {
        return t_.load(std::memory_order_acquire);
    }

    pseudoatomic(const pseudoatomic<T>&) = delete;
    pseudoatomic(pseudoatomic<T>&&) = delete;
    auto operator=(const pseudoatomic<T>&) -> pseudoatomic<T>& = delete;
    auto operator=(pseudoatomic<T>&&) -> pseudoatomic<T>& = delete;

  private:
    std::atomic<T> t_{T{}};
};
#endif
