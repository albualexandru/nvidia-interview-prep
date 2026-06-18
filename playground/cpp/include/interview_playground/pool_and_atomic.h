#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace interview_playground {

struct Packet {
  int id;
  std::string payload;
};

template <typename T, std::size_t Capacity>
class FixedPool {
 public:
  struct Deleter {
    FixedPool* pool{};

    void operator()(T* pointer) const noexcept {
      if (pool != nullptr && pointer != nullptr) {
        pool->release(pointer);
      }
    }
  };

  FixedPool() = default;

  FixedPool(const FixedPool&) = delete;
  FixedPool& operator=(const FixedPool&) = delete;

  template <typename... Args>
  [[nodiscard]] auto make_unique(Args&&... args) -> std::unique_ptr<T, Deleter> {
    for (std::size_t index = 0; index < Capacity; ++index) {
      bool expected = false;
      if (!in_use_[index].compare_exchange_strong(
              expected, true, std::memory_order_acq_rel,
              std::memory_order_relaxed)) {
        continue;
      }

      try {
        auto* instance =
            ::new (static_cast<void*>(storage_[index].storage.data()))
                T(std::forward<Args>(args)...);
        return std::unique_ptr<T, Deleter>(instance, Deleter{this});
      } catch (...) {
        in_use_[index].store(false, std::memory_order_release);
        throw;
      }
    }

    throw std::runtime_error("fixed pool exhausted");
  }

  [[nodiscard]] auto available() const noexcept -> std::size_t {
    std::size_t free_slots = 0;
    for (const auto& busy : in_use_) {
      free_slots += busy.load(std::memory_order_acquire) ? 0U : 1U;
    }
    return free_slots;
  }

  [[nodiscard]] constexpr auto capacity() const noexcept -> std::size_t {
    return Capacity;
  }

 private:
  struct alignas(T) Slot {
    std::array<std::byte, sizeof(T)> storage{};
  };

  void release(T* pointer) noexcept {
    for (std::size_t index = 0; index < Capacity; ++index) {
      if (slot_ptr(index) != pointer) {
        continue;
      }

      pointer->~T();
      in_use_[index].store(false, std::memory_order_release);
      return;
    }
  }

  [[nodiscard]] auto slot_ptr(std::size_t index) noexcept -> T* {
    return std::launder(reinterpret_cast<T*>(storage_[index].storage.data()));
  }

  std::array<Slot, Capacity> storage_{};
  std::array<std::atomic_bool, Capacity> in_use_{};
};

inline auto run_atomic_counter_demo(std::size_t thread_count,
                                    std::size_t increments_per_thread)
    -> std::uint64_t {
  std::atomic<std::uint64_t> counter{0};
  {
    std::vector<std::jthread> workers;
    workers.reserve(thread_count);

    for (std::size_t worker = 0; worker < thread_count; ++worker) {
      workers.emplace_back([&counter, increments_per_thread] {
        for (std::size_t iteration = 0; iteration < increments_per_thread;
             ++iteration) {
          counter.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
  }

  return counter.load(std::memory_order_relaxed);
}

}  // namespace interview_playground
