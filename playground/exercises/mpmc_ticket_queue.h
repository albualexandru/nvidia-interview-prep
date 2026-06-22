#pragma once

// mpmc_ticket_queue.h — Exercise: bounded MPMC queue with per-slot sequences.
//
// NIXL context
// ============
// NIXL's multi-backend dispatch uses a work queue that multiple backend threads
// dequeue from concurrently.  The MPMC design here matches the real-world
// pattern: a fixed-size ring where producers compete for tail slots and
// consumers compete for head slots, all without a mutex.
//
// The design (Dmitry Vyukov, 2010 — basis for folly::MPMCQueue)
// ==============================================================
// Each slot carries a sequence number initialised to its index.
// The sequence number acts as a per-slot version counter.
//
//   enqueue (producer):
//     1. pos = tail_.fetch_add(1, acq_rel)       — claim a slot
//     2. slot = slots_[pos % Capacity]
//     3. Wait (spin) until slot.sequence == pos  — slot is free
//        (this is the only spin; it's bounded by Capacity)
//     4. Write value into slot.
//     5. slot.sequence.store(pos + 1, release)   — publish to consumers
//
//   dequeue (consumer):
//     1. pos = head_.fetch_add(1, acq_rel)       — claim a slot
//     2. slot = slots_[pos % Capacity]
//     3. Wait (spin) until slot.sequence == pos + 1  — data is ready
//     4. Read and clear value from slot.
//     5. slot.sequence.store(pos + Capacity, release) — recycle slot
//
// Why this works without ABA
// ==========================
// The sequence number encodes both the generation (which lap around the ring)
// and the ready state.  ABA cannot occur because a slot is never reused until
// its sequence is advanced by exactly Capacity, making the old value
// permanently distinguishable.
//
// Assignment
// ==========
// 1. Implement enqueue() and dequeue() following the algorithm above.
//    Return false / nullopt immediately if the queue is full / empty
//    (i.e. replace the spin-wait with a check-and-return for a non-blocking
//    variant).
// 2. Initialise slots_: each slots_[i].sequence should start at i.
//    Hint: do this in the constructor, or use a designated-initialiser trick.
// 3. Measure false sharing:
//    - First: remove alignas(64) from Slot.
//    - Benchmark with 4 producers + 4 consumers on a multi-core machine.
//    - Re-add alignas(64) and compare.
//    On an x86-64 CPU with 64-byte cache lines, expect 2–4× throughput
//    improvement when each Slot occupies its own cache line.
//
// Memory ordering guide
// =====================
//   tail_.fetch_add  → acq_rel: claim is a read-modify-write; acquire sees
//                      prior consumer recycles; release publishes the claim.
//   sequence store (publish)  → release: pairs with the acquire in dequeue.
//   sequence load (check)     → acquire: pairs with the release in enqueue.
//   head_.fetch_add  → acq_rel: same reasoning as tail_.
//   sequence store (recycle)  → release: pairs with the acquire in enqueue.
//
// Suggested test
// ==============
// 4 producers each enqueue N unique integers (use producer_id * N + i).
// 4 consumers collect all results into a shared set under a mutex.
// Verify: set size == 4*N, no duplicates, no missing values.
// ThreadSanitizer: clang++ -fsanitize=thread

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace interview_playground::exercises {

template <typename T, std::size_t Capacity>
class MpmcTicketQueue {
 public:
  MpmcTicketQueue() {
    // TODO: initialise each slots_[i].sequence to i so enqueue() can
    // detect a fresh slot by comparing sequence == pos.
    for (std::size_t i = 0; i < Capacity; ++i) {
      slots_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  // Returns false if the queue is full or another attempt at getting a slot should be made (non-blocking).
  auto enqueue(T value) -> bool {
    auto t = tail.load(std::memory_order_relaxed);
    
    for (;;) {
      auto seq = slots_[t % Capacity].sequence.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(t);

      if (diff == 0) {
        // Slot is empty and matches our ticket. Try to claim it.
        if (tail.compare_exchange_weak(t, t + 1, std::memory_order_relaxed)) {
          break; // Success! We own ticket 't'. Break the loop.
        }
        // If CAS fails, 't' is updated. Loop restarts to check the new 't'.
      } 
      else if (diff < 0) {
        // The sequence is behind our ticket. The queue is full.
        return false; 
      } 
      else {
        // diff > 0. We are out of sync (another thread just updated the sequence).
        // Reload tail and try again.
        t = tail.load(std::memory_order_relaxed);
      }
    }

    // We only reach here if we successfully claimed ticket 't'
    slots_[t % Capacity].value = std::move(value);
    slots_[t % Capacity].sequence.store(t + 1, std::memory_order_release);

    return true;
  }

  // Returns nullopt if the queue is empty (non-blocking).
  auto dequeue() -> std::optional<T> {
    auto h = head.load(std::memory_order_relaxed);

    while (true) {
      auto seq = slots_[h % Capacity].sequence.load(std::memory_order_acquire);
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(h+1);

      if (diff == 0) {
        // Slot is full and matches our ticket. Try to claim it.
        if (head.compare_exchange_weak(h, h + 1, std::memory_order_relaxed)) {
          break; // Success! We own ticket 'h'. Break the loop.
        }
      } else if (diff < 0) {
        // The sequence is behind our ticket. The queue is empty.
        return std::nullopt; 
      } else {
        // diff > 0. We are out of sync (another thread just updated the sequence).
        // Reload head and try again.
        h = head.load(std::memory_order_relaxed);
        continue;
      }

    }

    // We only reach here if we successfully claimed ticket 'h'
    auto value = std::move(slots_[h % Capacity].value);
    slots_[h % Capacity].sequence.store(h + Capacity, std::memory_order_release);
    
    return value;
  }

 private:
  // alignas(64): each Slot on its own cache line prevents producers/consumers
  // from invalidating each other's cache entries (false sharing).
  // Remove this and re-benchmark to see the penalty.
  struct alignas(64) Slot {
    std::atomic<std::size_t> sequence{0};
    std::optional<T> value;
  };

  std::array<Slot, Capacity> slots_{};
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace interview_playground::exercises
