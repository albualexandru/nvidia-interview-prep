#pragma once

// work_stealing_deque.h — Exercise: Chase-Lev work-stealing deque.
//
// NIXL context
// ============
// NIXL's multi-backend progress architecture (useProgThread=true) uses one
// thread per backend plus the application thread.  When one backend's progress
// thread runs out of local work (no in-flight requests), it can "steal" a
// pending request from another backend's queue.  This is the classic work-
// stealing problem, and the Chase-Lev deque (2005) is the canonical solution
// used in production runtimes (TBB, Go scheduler, Java ForkJoinPool).
//
// Structure
// =========
// The deque has two ends:
//   BOTTOM — owned exclusively by the worker thread.
//             The worker pushes new tasks here and pops its own tasks here.
//             No CAS needed for push/pop when there's no contention.
//   TOP    — used by thieves to steal from this worker.
//             Thieves CAS top_ to claim a task.
//
// The backing store is a circular array (like SPSC ring buffer) that is
// resized by doubling when full.  For this exercise, use a fixed capacity.
//
// Key operations
// ==============
//   push_bottom(task):   worker only
//     1. b = bottom_.load(relaxed)
//     2. buf_[b % Capacity] = task
//     3. std::atomic_thread_fence(release)   ← publish the write
//     4. bottom_.store(b + 1, relaxed)
//
//   pop_bottom() → optional<T>:  worker only
//     1. b = bottom_.load(relaxed) - 1
//     2. bottom_.store(b, seq_cst) ← serialise with concurrent steals
//     3. t = top_.load(relaxed)
//     4. if b < t: empty; bottom_.store(t, relaxed); return nullopt
//     5. task = buf_[b % Capacity]
//     6. if b > t: return task   ← no race, only one item left otherwise
//     7. // b == t: last item; race with thieves
//        if !top_.compare_exchange_strong(t, t+1, seq_cst, relaxed):
//            bottom_.store(t+1, relaxed); return nullopt  ← thief won
//        bottom_.store(t+1, relaxed); return task
//
//   steal_top() → optional<T>:   thieves only
//     1. t = top_.load(acquire)
//     2. std::atomic_thread_fence(seq_cst) ← see the bottom_ store
//     3. b = bottom_.load(acquire)
//     4. if t >= b: return nullopt   ← empty
//     5. task = buf_[t % Capacity]
//     6. if !top_.compare_exchange_strong(t, t+1, seq_cst, relaxed):
//            return nullopt   ← another thief won; retry at call site
//        return task
//
// Assignment
// ==========
// 1. Implement push_bottom(), pop_bottom(), steal_top() following the
//    pseudocode above.
// 2. Add a size() estimate (bottom - top) — note it can be negative if
//    top advanced past bottom during a concurrent steal.
// 3. Stress test: 1 worker thread pushes 100 000 integers; 3 thief threads
//    call steal_top() in a loop.  Collect all values and verify every integer
//    0..99999 appears exactly once.
//
// Memory ordering guide
// =====================
//   push_bottom fence (release): ensures the task write is visible to
//     thieves before they see the advanced bottom_.
//   pop_bottom bottom_ store (seq_cst): serialises with steal_top's
//     fence so thieves see the decremented bottom_ before reading top_.
//   steal_top fence (seq_cst): ensures the thief sees the worker's
//     bottom_ store (seq_cst) before it reads the task — the SC fence pair
//     is necessary on weakly-ordered CPUs (ARM) to avoid seeing stale bottom_.
//   CAS in steal_top (seq_cst success, relaxed failure):
//     seq_cst on success to make the claim globally visible;
//     relaxed on failure is safe because no shared state was modified.
//
// Why not just use a mutex?
// =========================
// With N backend threads and N*M tasks, a mutex on the deque creates an N-way
// contention bottleneck.  The Chase-Lev design allows the owner to push/pop
// at O(1) amortised without any CAS; only when top == bottom - 1 (last item)
// does the owner need a CAS.  This makes work-stealing runtimes scale linearly
// with thread count in the common case.
//
// Suggested benchmarks
// ====================
// - Vary the steal ratio (fraction of tasks stolen vs self-popped).
// - Compare throughput against a mutex-protected std::deque.
// - Use perf stat to count cache-miss events with/without alignas(64) on
//   top_ and bottom_.

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace interview_playground::exercises {

template <typename T, std::size_t Capacity>
class WorkStealingDeque {
 public:
  // Push a task onto the bottom of the deque.  Called by the owner thread only.
  void push_bottom(T task) {
    throw std::logic_error(
        "TODO: implement WorkStealingDeque::push_bottom — "
        "load bottom_ (relaxed), write buf_, release fence, store bottom_+1 (relaxed).");
  }

  // Pop a task from the bottom.  Called by the owner thread only.
  // Returns nullopt if the deque is empty.
  [[nodiscard]] auto pop_bottom() -> std::optional<T> {
    throw std::logic_error(
        "TODO: implement WorkStealingDeque::pop_bottom — "
        "decrement bottom_ (seq_cst), compare with top_, "
        "CAS top_ if only one item remains.");
  }

  // Steal a task from the top.  Called by thief threads.
  // Returns nullopt if the deque is empty or another thief won the race.
  // Callers should retry on nullopt if they believe the deque is non-empty.
  [[nodiscard]] auto steal_top() -> std::optional<T> {
    throw std::logic_error(
        "TODO: implement WorkStealingDeque::steal_top — "
        "load top_ (acquire), seq_cst fence, load bottom_ (acquire), "
        "read buf_, CAS top_ (seq_cst/relaxed).");
  }

  // Returns a snapshot of the approximate number of items.
  // May be negative if a steal races with pop_bottom — treat negatives as 0.
  [[nodiscard]] auto size() const noexcept -> std::ptrdiff_t {
    const auto b = static_cast<std::ptrdiff_t>(
        bottom_.load(std::memory_order_relaxed));
    const auto t = static_cast<std::ptrdiff_t>(
        top_.load(std::memory_order_relaxed));
    return b - t;
  }

 private:
  std::array<T, Capacity> buf_{};
  // bottom_ is written only by the owner; top_ is written by thieves (and
  // owner in the last-item edge case).  Separate cache lines prevent false
  // sharing between the owner's frequent bottom_ accesses and the thieves'
  // top_ CAS operations.
  alignas(64) std::atomic<std::size_t> bottom_{0};
  alignas(64) std::atomic<std::size_t> top_{0};
};

}  // namespace interview_playground::exercises
