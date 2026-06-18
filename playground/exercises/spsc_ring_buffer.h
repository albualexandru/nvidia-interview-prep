#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace interview_playground::exercises {

// Assignment:
// 1. Implement a single-producer/single-consumer ring buffer using only atomics.
// 2. Avoid std::mutex. Use acquire/release memory ordering intentionally.
// 3. Decide how to signal full vs empty without data races.
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
 public:
  auto push(T value) -> bool {
    throw std::logic_error("TODO: implement SpscRingBuffer::push");
  }

  auto pop() -> std::optional<T> {
    throw std::logic_error("TODO: implement SpscRingBuffer::pop");
  }

 private:
  std::array<std::optional<T>, Capacity> slots_{};
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace interview_playground::exercises
