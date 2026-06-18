#pragma once

// pool_and_atomic.h — reference implementations used in the NVIDIA interview playground.
//
// NIXL context
// ============
// NIXL (NVIDIA Inference Xfer Library) preallocates and pins memory regions once at
// startup, then reuses them for every transfer without going back to the allocator.
// This avoids malloc/free jitter on the hot path and keeps pointer identity stable,
// which is necessary because RDMA hardware works with registered (pinned) VA ranges.
// FixedPool models that pattern in miniature: a compile-time-sized slab of pre-typed
// slots, each guarded by a single atomic bool.
//
// Key C++ concepts demonstrated here
// ====================================
//  1. Placement new + manual destructor calls: live objects inside raw storage
//  2. std::launder: after placement new the compiler does not know the storage
//     was reused; launder() informs it and re-establishes pointer provenance.
//  3. compare_exchange_strong with acq_rel/acquire memory ordering:
//     - acq_rel on success: the store (true) releases the "slot is mine" write
//       *and* the load (false) acquires any prior release from a returning thread.
//     - acquire on failure: we must still acquire to re-read in_use_ consistently
//       after another thread may have written it.
//  4. memory_order_release on the release() store: any destructor side-effects
//     that preceded this store become visible to the next thread that acquires
//     the slot via compare_exchange.
//  5. alignas(T) Slot: the raw byte array must honour T's alignment or placement
//     new triggers UB on platforms with strict alignment requirements (e.g. SIMD).
//  6. memory_order_relaxed on fetch_add / counter load: safe here because the
//     jthread destructors provide a sequenced-before fence — the load happens
//     after all increments are done.

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

// A trivial payload representative of a network frame descriptor.
// In NIXL the equivalent is nixlBasicDesc {addr, len, devId}.
struct Packet {
  int id;
  std::string payload;
};

// FixedPool<T, Capacity>
// ----------------------
// A bump-free, lock-free object pool backed by a fixed-size in-place array.
//
// Allocation: O(Capacity) scan with one CAS per free slot attempt.
// Deallocation: O(Capacity) scan to locate the returning pointer.
//
// These are intentionally simple complexities; in NIXL, the pools are either
// small (the number of in-flight transfers is bounded) or backed by a slab
// allocator with O(1) free-list operations.
//
// Interview discussion points:
//  Q: Why not use a free-list (stack of free indices) instead of scanning?
//  A: A lock-free free-list with CAS is the natural next step.  The scan keeps
//     the code readable for illustration; see treiber_stack.h for the upgrade.
//
//  Q: Why store std::atomic_bool separately from the storage slots?
//  A: Keeping hot metadata (the atomic flags) in a dense array avoids the
//     false-sharing penalty that would arise if each flag were padded to a
//     cache line while embedded inside a large Slot structure.
template <typename T, std::size_t Capacity>
class FixedPool {
 public:
  // Custom deleter: when the unique_ptr goes out of scope it calls release()
  // on the owning pool rather than operator delete.  This is the same idiom
  // NIXL uses internally for buffer recycling — the destructor never touches
  // the allocator, only the pool's bookkeeping.
  struct Deleter {
    FixedPool* pool{};

    void operator()(T* pointer) const noexcept {
      if (pool != nullptr && pointer != nullptr) {
        pool->release(pointer);
      }
    }
  };

  FixedPool() = default;

  // Non-copyable: the pool owns the backing storage.
  FixedPool(const FixedPool&) = delete;
  FixedPool& operator=(const FixedPool&) = delete;

  // make_unique — acquire a slot and construct T in place.
  //
  // Memory ordering detail:
  //   compare_exchange_strong(expected=false, desired=true,
  //                           success=acq_rel, failure=acquire)
  //
  //   On success (acq_rel):
  //     - The acquire half: any release-store that marked this slot free
  //       (i.e. the previous owner's release()) happens-before this code.
  //     - The release half: the in-place construction and all stores that
  //       follow happen-after the CAS, visible to anyone who next acquires.
  //   On failure (acquire):
  //     - We still need acquire to see the latest state of in_use_[index]
  //       from the thread that last wrote it, avoiding stale-cache reads.
  template <typename... Args>
  [[nodiscard]] auto make_unique(Args&&... args) -> std::unique_ptr<T, Deleter> {
    for (std::size_t index = 0; index < Capacity; ++index) {
      bool expected = false;
      if (!in_use_[index].compare_exchange_strong(
              expected, true, std::memory_order_acq_rel,
              std::memory_order_acquire)) {
        continue;
      }

      try {
        // Placement new: construct T directly inside the aligned byte array.
        // This avoids a heap allocation and keeps the object at a stable,
        // pinnable address — a prerequisite for RDMA memory registration.
        auto* instance =
            ::new (static_cast<void*>(storage_[index].storage.data()))
                T(std::forward<Args>(args)...);
        return std::unique_ptr<T, Deleter>(instance, Deleter{this});
      } catch (...) {
        // Construction failed; restore the slot to free so the pool remains
        // consistent.  Release ordering ensures the store is visible.
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
  // Slot: raw aligned storage for one T.
  //
  // alignas(T) ensures that reinterpret_cast<T*>(storage.data()) is
  // well-defined and that SIMD / hardware alignment requirements are met.
  // Without it, placing a 16-byte-aligned SSE type here would be UB.
  struct alignas(T) Slot {
    std::array<std::byte, sizeof(T)> storage{};
  };

  // release — called by the Deleter when the unique_ptr is destroyed.
  //
  // We call ~T() manually (paired with the placement-new in make_unique)
  // then mark the slot free with a release store so the next make_unique
  // on this slot sees a consistent object state (acquire on CAS success).
  void release(T* pointer) noexcept {
    for (std::size_t index = 0; index < Capacity; ++index) {
      if (slot_ptr(index) != pointer) {
        continue;
      }

      pointer->~T();
      // memory_order_release: the destructor's side-effects are published
      // before any thread that next acquires this slot via CAS.
      in_use_[index].store(false, std::memory_order_release);
      return;
    }
  }

  // slot_ptr — recover a T* from the raw byte storage.
  //
  // std::launder is required here: after placement new, the compiler treats
  // the original storage pointer as pointing to bytes, not T.  launder()
  // tells it "there really is a live T here" and re-establishes provenance,
  // allowing value-reads through this pointer to see the constructed object.
  [[nodiscard]] auto slot_ptr(std::size_t index) noexcept -> T* {
    return std::launder(reinterpret_cast<T*>(storage_[index].storage.data()));
  }

  std::array<Slot, Capacity> storage_{};
  // in_use_ is kept in its own dense array (not embedded in Slot) so all
  // atomic flags fit on as few cache lines as possible, reducing the cost
  // of scanning them under contention.
  std::array<std::atomic_bool, Capacity> in_use_{};
};

// run_atomic_counter_demo
// -----------------------
// Spawns `thread_count` std::jthread workers, each incrementing a shared
// atomic<uint64_t> `increments_per_thread` times, then returns the total.
//
// Memory ordering note — why relaxed is correct here:
//   Each fetch_add uses memory_order_relaxed.  Relaxed atomics do *not*
//   guarantee ordering relative to other memory operations, but they do
//   guarantee that the atomic object itself is modified atomically (no torn
//   writes, no lost updates).  Because all we care about is the *total count*
//   (not which thread's increment comes first), relaxed is sufficient.
//
//   The final load also uses relaxed: std::jthread's destructor acts as a
//   synchronization point — it joins the thread, which creates a
//   happens-before edge from the last fetch_add to the load.  Any
//   strengthened ordering on either operation would be redundant.
//
// NIXL analogy:
//   NIXL tracks in-flight transfer counts with atomics.  The per-transfer
//   completion is noted with a relaxed decrement; only the transition to zero
//   (checked with a compare_exchange or load-acquire) requires stronger
//   ordering to publish completion state to the caller.
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
    // jthread destructors run here, joining all workers before we return
    // — this is the synchronization point that makes the relaxed load safe.
  }

  return counter.load(std::memory_order_relaxed);
}

}  // namespace interview_playground
