#pragma once

// spsc_ring_buffer.h — Exercise: single-producer / single-consumer ring buffer.
//
// NIXL context
// ============
// Inside NIXL's progress thread (useProgThread=true), completions are fed from
// one thread (the UCX poller) to another (the application callback thread)
// through a bounded SPSC queue.  NIXL uses this pattern because it is the
// simplest lock-free design and the threading model is fixed at construction.
//
// Why SPSC is special
// ====================
// With exactly one producer and one consumer, head_ and tail_ are each written
// by only one thread.  This eliminates the need for compare-exchange: a simple
// load + store suffices, provided the memory ordering is correct.
//
// Assignment
// ==========
// 1. Implement push() and pop() using only atomics — no std::mutex.
// 2. Use acquire/release ordering intentionally:
//    - push()  stores tail_ with memory_order_release after writing the slot.
//    - pop()   loads  tail_ with memory_order_acquire before reading the slot.
//    - push()  loads  head_ with memory_order_acquire to check for space.
//    - pop()   stores head_ with memory_order_release after consuming the slot.
// 3. Decide on a full/empty representation:
//    OPTION A — waste one slot: full when (tail+1)%Capacity == head.
//               Simple; capacity is effectively Capacity-1.
//    OPTION B — separate count: add an atomic<size_t> size_ and CAS it.
//               Allows full Capacity usage but adds a third atomic update.
//
// Key insight on memory ordering
// ==============================
// The release on tail_ in push() "publishes" the written slot value.
// The acquire on tail_ in pop() "subscribes" to that publication.
// Without this pair the consumer could observe a stale slot value even though
// tail_ already moved — a data race that's invisible without a race detector.
//
// False sharing
// =============
// head_ and tail_ are on separate cache lines (alignas(64)) because the
// producer writes tail_ and reads head_, while the consumer does the opposite.
// If they shared a cache line, every write to either would bounce the line
// between CPU cores — the "false sharing" anti-pattern.  Measure the
// throughput difference with and without alignas(64) as an exercise.
//
// Suggested test
// ==============
// Producer thread: push 0..N-1, then push a sentinel value.
// Consumer thread: pop until sentinel, verify in-order receipt.
// Run under ThreadSanitizer: clang++ -fsanitize=thread

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace interview_playground::exercises {

/** 
producer:  slots_[head] = value          ← write the data
           head_.store(release)          ← publish it

consumer:  head_.load(acquire)           ← MUST be acquire to see the data
           read slots_[tail]

*/

template <typename T, std::size_t Capacity>
class SpscRingBuffer {
 public:
  // Returns false if the buffer is full (non-blocking).
  auto push(T value) -> bool {
    auto head = head_.load(std::memory_order_relaxed);
    auto tail = tail_.load(std::memory_order_acquire);
    if((head + 1) % Capacity == tail) {
      return false;
    }
    slots_[head] = std::move(value);
    head_.store((head + 1) % Capacity, std::memory_order_release);
    return true;

  }

  // Returns nullopt if the buffer is empty (non-blocking).
  auto pop() -> std::optional<T> {
    auto head = head_.load(std::memory_order_acquire);
    auto tail = tail_.load(std::memory_order_relaxed);
    if (head == tail) {
      return std::nullopt;
    }
    auto value = std::move(slots_[tail]);
    tail_.store((tail + 1) % Capacity, std::memory_order_release);
    return value;
  }

 private:
  std::array<std::optional<T>, Capacity> slots_{};
  // alignas(64): place head_ and tail_ on separate cache lines to prevent
  // false sharing between the producer (writes tail_) and consumer (writes head_).
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace interview_playground::exercises
