#pragma once

// treiber_stack.h — Exercise: lock-free stack (Treiber 1986).
//
// NIXL context
// ============
// NIXL's transfer request free-list is conceptually a Treiber stack: completed
// nixlXferReqH objects are pushed back onto a per-agent free-list so the next
// createXferReq can reuse them without touching the allocator.  The ABA hazard
// described below is why NIXL uses epoch-based reclamation or tagged pointers
// rather than a naive CAS loop over raw Node*.
//
// Assignment
// ==========
// 1. Implement push() with a CAS loop.
//    Pseudocode:
//      node->next = head_.load(acquire)
//      loop: compare_exchange_weak(node->next, node, release, relaxed)
//
// 2. Implement pop() with a CAS loop.
//    Pseudocode:
//      old_head = head_.load(acquire)
//      loop:
//        if old_head == nullptr: return nullopt
//        compare_exchange_weak(old_head, old_head->next, acq_rel, acquire)
//
// 3. Explain the ABA problem in a comment before your pop() implementation:
//    Thread T1 reads head_ = A.  T1 is descheduled.
//    Thread T2 pops A, pops B, pushes A back (A->next now points somewhere
//    invalid).  T1 wakes up, CAS succeeds (head_ is still A) but A->next is
//    corrupt — UB.
//
// 4. Add one reclamation strategy.  Three options, easiest to hardest:
//
//    OPTION A — Tagged pointers (128-bit CAS on x86-64):
//      Pack a generation counter into the high bits of the pointer.
//      struct TaggedPtr { Node* ptr; uintptr_t tag; };
//      std::atomic<TaggedPtr> head_;
//      Increment tag on every pop; ABA requires the tag to wrap (2^64 pops).
//      Needs std::atomic<__int128> or compiler-specific support.
//
//    OPTION B — Hazard pointers (see hazard_pointer.h exercise):
//      Before dereferencing a Node*, publish it in a thread-local hazard slot.
//      A node is only freed when no hazard slot holds its address.
//      Lock-free but requires scanning all hazard slots on every reclaim.
//
//    OPTION C — Epoch-based reclamation (EBR):
//      Threads register entry into a critical section by incrementing a local
//      epoch.  Retired nodes are placed in per-epoch retire lists.
//      A node is safe to free once all threads have passed through a new epoch.
//      Used by folly::hazptr and most production lock-free libraries.
//
// Memory ordering guide
// =====================
//   push: store node->next with relaxed (no one else reads it yet), then
//         CAS with release (publish the new head to other threads).
//   pop:  load head_ with acquire (see the released push), CAS with acq_rel
//         (acquire the node's contents, release the new head pointer).
//   compare_exchange_weak vs strong:
//     _weak may spuriously fail on LL/SC architectures (ARM, RISC-V) but
//     generates tighter code in a retry loop.  Use _weak in loops.
//
// Suggested test
// ==============
// 4 threads each push 10 000 ints; 4 threads each pop until empty.
// Verify that every pushed value is popped exactly once.
// ThreadSanitizer: clang++ -fsanitize=thread
// AddressSanitizer: clang++ -fsanitize=address  (catches use-after-free)

#include <atomic>
#include <optional>
#include <stdexcept>

namespace interview_playground::exercises {

template <typename T>
class TreiberStack {
 public:
  void push(T value) {
    throw std::logic_error("TODO: implement TreiberStack::push");
  }

  auto pop() -> std::optional<T> {
    throw std::logic_error("TODO: implement TreiberStack::pop");
  }

 private:
  struct Node {
    T value;
    Node* next{nullptr};
  };

  // TODO: consider replacing Node* with a tagged-pointer struct to solve ABA.
  std::atomic<Node*> head_{nullptr};
};

}  // namespace interview_playground::exercises
