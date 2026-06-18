#pragma once

// hazard_pointer.h — Exercise: hazard-pointer memory reclamation.
//
// NIXL context
// ============
// NIXL's lock-free transfer-request free-list (conceptually a Treiber stack)
// needs a reclamation strategy so that a thread popping a node doesn't free
// it while another thread has just loaded the same pointer.  The production
// answer in NIXL is epoch-based reclamation (EBR), but hazard pointers are the
// canonical interview topic because they make the safety argument explicit and
// mechanical.
//
// What is a hazard pointer?
// =========================
// Before a thread dereferences a shared atomic pointer P, it "publishes" P's
// value in a dedicated per-thread hazard slot.  No other thread may free a
// node whose address appears in any hazard slot.  When a thread retires a
// node (removes it from a data structure and wants to free it), it first scans
// all hazard slots.  If the node's address appears in any slot, the node is
// kept on a private retire list and the scan is retried later.  Once no slot
// holds the address, the node is freed.
//
// Complexity
// ==========
//   Hazard slots     : O(K * T) where K = max pointers per thread, T = threads
//   Retire-list scan : O(T)  per reclamation attempt
//   Amortisation     : batch reclamation once retire list reaches R > T items
//
// Assignment
// ==========
// Part 1 — Build the HazardDomain (shared registry):
//   - A fixed array of atomic<void*> hazard slots, one per thread.
//     (Use a compile-time constant MaxThreads = 16 for simplicity.)
//   - A method acquire_slot(size_t thread_id) → slot reference.
//   - A method scan() → set of all currently hazardous addresses.
//
// Part 2 — Build the HazardGuard (RAII per-pointer protection):
//   - Constructor: publish the pointer value in the thread's hazard slot.
//   - Destructor:  clear the slot (write nullptr).
//   - get():       return the protected pointer.
//   - Key: after storing the pointer in the hazard slot, re-load the shared
//     atomic to verify it hasn't changed.  If it changed, retry.  This
//     closing of the "check-then-act" window is the core correctness argument.
//
// Part 3 — Add retire() to a HazardPool<T>:
//   - Add a retired node to a thread-local retire list.
//   - When the list reaches a threshold (e.g. 2 * MaxThreads), call reclaim():
//       hazardous = domain.scan()
//       for each node in retire_list:
//           if node not in hazardous: delete node
//           else: keep in list
//
// Part 4 — Wire hazard pointers into TreiberStack::pop():
//   Replace the naive load-and-dereference pattern with:
//     HazardGuard<Node> guard(domain_, head_, thread_id);
//     Node* old_head = guard.get();
//     if old_head == nullptr: return nullopt
//     if CAS(head_, old_head, old_head->next): guard.clear(); retire(old_head)
//     else: retry
//
// Memory ordering guide
// =====================
//   Hazard slot store : memory_order_seq_cst (ensures the store is globally
//                       visible before we re-load the shared pointer).
//                       Some implementations use release + seq_cst fence;
//                       interview answer: the fence must be seq_cst to prevent
//                       store-load reordering on x86 and on TSO architectures.
//   Hazard slot load (in scan()): memory_order_acquire — pairs with the
//                       seq_cst store so we see the most recent protected ptrs.
//   retire list manipulation: no atomics needed (thread-local).
//
// Suggested test
// ==============
// Run TreiberStack with hazard-pointer reclamation under AddressSanitizer.
// Without reclamation, ASAN should report use-after-free; with reclamation it
// should be clean.
//   clang++ -fsanitize=address,thread -std=c++20 -o test_hazard ...

#include <array>
#include <atomic>
#include <cstddef>
#include <unordered_set>
#include <vector>

namespace interview_playground::exercises {

// Maximum number of concurrent threads this domain supports.
// In production (e.g. folly) this is dynamic; fixed here for clarity.
inline constexpr std::size_t kMaxHazardThreads = 16;

// HazardDomain — shared registry of hazard slots.
//
// One domain instance is shared across all threads using the same data
// structure.  Typically stored as a static inside the data structure or passed
// by reference.
class HazardDomain {
 public:
  // Returns a reference to the hazard slot owned by `thread_id`.
  // Call this once per thread; keep the reference for the lifetime of the slot.
  auto& slot(std::size_t thread_id) noexcept {
    return slots_[thread_id % kMaxHazardThreads];
  }

  // Returns the set of all currently protected addresses.
  // Callers hold this snapshot while deciding whether to free retired nodes.
  auto scan() const -> std::unordered_set<void*> {
    throw std::logic_error("TODO: implement HazardDomain::scan — "
                           "load each slot with memory_order_acquire and "
                           "insert non-null values into the result set.");
  }

 private:
  // Align to cache line to avoid false sharing between thread slots.
  struct alignas(64) Slot {
    std::atomic<void*> ptr{nullptr};
  };
  std::array<Slot, kMaxHazardThreads> slots_{};
};

// HazardGuard<T> — RAII protection for a single pointer dereference.
//
// Usage:
//   HazardGuard<Node> guard(domain, shared_head_atomic, my_thread_id);
//   Node* p = guard.get();  // nullptr if the pointer was null
//   if (p) { ... use *p safely ... }
//   // destructor clears the hazard slot
template <typename T>
class HazardGuard {
 public:
  // Protect `shared` — the atomic whose value we want to dereference.
  //
  // Algorithm (the "validate-after-publish" loop):
  //   do {
  //     p = shared.load(relaxed or acquire)
  //     slot.store(p, seq_cst)       // publish to reclamers
  //     fence(seq_cst)               // close the window
  //   } while (p != shared.load(seq_cst))
  //
  // The loop is necessary because between the first load and the hazard store,
  // another thread could retire and free `p`.  Re-validating after the store
  // (with a seq_cst fence/load) ensures that if the retirement preceded our
  // hazard store, we will see the updated shared pointer.
  HazardGuard(HazardDomain& domain,
              std::atomic<T*>& shared,
              std::size_t thread_id)
      : slot_(domain.slot(thread_id).ptr) {
    throw std::logic_error("TODO: implement HazardGuard constructor — "
                           "publish pointer in slot_ with validate-after-publish loop.");
  }

  ~HazardGuard() {
    // Clear the hazard slot; this pointer is no longer in use.
    slot_.store(nullptr, std::memory_order_release);
  }

  // Non-copyable, non-movable: a guard is tied to a specific hazard slot.
  HazardGuard(const HazardGuard&) = delete;
  HazardGuard& operator=(const HazardGuard&) = delete;

  [[nodiscard]] auto get() const noexcept -> T* { return protected_; }

  // Explicit early release (e.g. after a successful CAS that removed the node).
  void clear() noexcept {
    protected_ = nullptr;
    slot_.store(nullptr, std::memory_order_release);
  }

 private:
  std::atomic<void*>& slot_;
  T* protected_{nullptr};
};

// HazardPool<T> — wraps a domain and provides retire() + reclaim().
//
// Each thread that uses this pool must call retire() for every node it removes
// from the data structure.  reclaim() is called automatically when the retire
// list grows large enough to amortise the scan cost.
template <typename T>
class HazardPool {
 public:
  explicit HazardPool(HazardDomain& domain) : domain_(domain) {}

  // Mark `node` as retired.  Triggers reclaim() when the list is large enough.
  void retire(T* node, std::size_t thread_id) {
    throw std::logic_error(
        "TODO: implement HazardPool::retire — "
        "add node to retire_list_; if size > 2*kMaxHazardThreads call reclaim().");
  }

 private:
  // Walk the retire list and free nodes whose address is not in the hazard set.
  void reclaim() {
    throw std::logic_error(
        "TODO: implement HazardPool::reclaim — "
        "call domain_.scan(), iterate retire_list_, "
        "delete safe nodes, keep protected ones.");
  }

  HazardDomain& domain_;
  // Thread-local retire list.  In a production library this would be
  // thread_local; here it's a member for simplicity (single-thread retire).
  std::vector<T*> retire_list_;
};

}  // namespace interview_playground::exercises
