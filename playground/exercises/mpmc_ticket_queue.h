#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace interview_playground::exercises {

// Assignment:
// 1. Implement a bounded MPMC queue with per-slot sequence numbers.
// 2. Keep producers and consumers lock-free under contention.
// 3. Measure how false sharing changes throughput on your machine.
template <typename T, std::size_t Capacity>
class MpmcTicketQueue {
 public:
  auto enqueue(T value) -> bool {
    throw std::logic_error("TODO: implement MpmcTicketQueue::enqueue");
  }

  auto dequeue() -> std::optional<T> {
    throw std::logic_error("TODO: implement MpmcTicketQueue::dequeue");
  }

 private:
  struct alignas(64) Slot {
    std::atomic<std::size_t> sequence{0};
    std::optional<T> value;
  };

  std::array<Slot, Capacity> slots_{};
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace interview_playground::exercises
