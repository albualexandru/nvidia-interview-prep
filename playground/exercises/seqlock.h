#pragma once

// seqlock.h — Exercise: sequence lock (seqlock) for read-mostly shared data.
//
// NIXL context
// ============
// NIXL agents maintain a transfer-state record per active request: current
// backend, bytes transferred, completion status.  This record is written by the
// progress thread and read by the application thread calling getXferStatus().
// A seqlock is ideal here: writes are rare (once per completion event) and
// reads are frequent (polling the status).  It allows reads with *no* CAS and
// no lock acquisition in the common case.
//
// How a seqlock works
// ====================
// A shared sequence counter starts at 0 (even = "no write in progress").
//
//   Writer:
//     1. seq_.fetch_add(1, release)   → seq is now odd  (write in progress)
//     2. Write the payload fields.
//     3. seq_.fetch_add(1, release)   → seq is now even (write done)
//        (A seq_cst fence before step 3 is safer on weakly-ordered CPUs.)
//
//   Reader:
//     1. s1 = seq_.load(acquire)      → sample before reading
//     2. If s1 is odd, spin (writer is mid-update); retry from step 1.
//     3. Read the payload into local copies.
//     4. s2 = seq_.load(acquire)      → sample after reading
//     5. If s1 != s2, a write interleaved; retry from step 1.
//     6. The local copies are consistent.
//
// Properties
// ==========
//   - Readers never block writers (no lock).
//   - Writers never block readers (reads are retried, not queued).
//   - Wait-free for writers; lock-free (but not wait-free) for readers.
//   - Unsuitable when the payload contains pointers that might be freed
//     during a read retry — use hazard pointers or RCU instead.
//   - Suitable for plain-old-data (POD) payloads: counters, status flags,
//     timestamps — exactly the kind of data in a NIXL transfer-state record.
//
// Assignment
// ==========
// Part 1 — Implement SeqLock<T> with read() and write():
//   - T must be trivially copyable (assert or static_assert this).
//   - write(const T& value): writer protocol above.
//   - read() → T: reader protocol above; return a consistent copy.
//
// Part 2 — Stress test:
//   - 1 writer thread: write a struct {uint64_t a; uint64_t b;} with a==b
//     continuously, incrementing both together.
//   - N reader threads: call read() in a tight loop; assert a==b on every
//     returned copy.  Any torn read will produce a!=b.
//   - Run for 1 second and count read iterations.
//
// Part 3 — Compare throughput against std::shared_mutex (read lock):
//   - Replace SeqLock with shared_mutex + shared_lock in the reader,
//     unique_lock in the writer.
//   - Compare reader iteration counts.  On a modern CPU with many readers,
//     SeqLock is typically 5–20× faster.
//
// Memory ordering guide
// =====================
//   seq_ write (odd):  memory_order_release — the "write started" signal
//                      must not be reordered after the payload writes.
//   seq_ write (even): memory_order_release — the "write done" signal
//                      must not be reordered before the payload writes.
//   seq_ read:         memory_order_acquire — both samples must acquire
//                      the corresponding releases so payload reads are
//                      visible.
//   On x86-64 (TSO), acquire/release on loads/stores is free (they compile to
//   plain MOV).  On ARM, they emit load-acquire / store-release instructions.
//
// Suggested test
// ==============
//   clang++ -std=c++20 -O2 -fsanitize=thread seqlock_test.cpp -o seqlock_test
//   ThreadSanitizer will report data races if the ordering is wrong.
//   Note: TSan instruments memory accesses; the seqlock *intentionally* races
//   on the payload — TSan may flag this.  Use volatile or TSan suppressions,
//   or use std::atomic<T> with relaxed accesses for the payload in the TSan
//   build.

#include <atomic>
#include <cassert>
#include <cstring>
#include <type_traits>

namespace interview_playground::exercises {

// SeqLock<T> — a sequence lock protecting a trivially-copyable payload T.
//
// T must be trivially copyable so that a memcpy snapshot is well-defined.
// For larger T, consider breaking it into independently seqlocked sub-fields.
template <typename T>
class SeqLock {
  static_assert(std::is_trivially_copyable_v<T>,
                "SeqLock requires a trivially copyable payload (safe to memcpy).");

 public:
  explicit SeqLock(T initial = T{}) {
    std::memcpy(&payload_, &initial, sizeof(T));
  }

  // write — update the payload.  Called by at most one writer at a time.
  // (If multiple writers are possible, protect this with a mutex externally.)
  void write(const T& value) {
    throw std::logic_error(
        "TODO: implement SeqLock::write — "
        "fetch_add(1, release) [odd], memcpy payload, fetch_add(1, release) [even]. "
        "Add a std::atomic_thread_fence(seq_cst) before the second fetch_add "
        "on weakly-ordered architectures.");
  }

  // read — return a consistent snapshot of the payload.
  // May spin if a write is in progress; otherwise returns immediately.
  [[nodiscard]] auto read() const -> T {
    throw std::logic_error(
        "TODO: implement SeqLock::read — "
        "load seq_ (acquire), check odd (spin if so), "
        "memcpy payload to local, load seq_ again (acquire), "
        "if changed retry, else return local copy.");
  }

 private:
  // Separate the sequence counter from the payload to avoid false sharing
  // if T is small: the counter is on its own cache line so readers writing
  // their own local copy don't invalidate the counter's line.
  alignas(64) std::atomic<std::size_t> seq_{0};

  // The payload is not atomic — we rely on the seqlock protocol for safety.
  // On most architectures, individual word reads/writes are atomic at the
  // hardware level even without std::atomic; the seqlock's acquire/release
  // pairs ensure the compiler doesn't reorder accesses across the barriers.
  alignas(64) T payload_{};
};

}  // namespace interview_playground::exercises
