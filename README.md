# nvidia-interview-prep

This repository is an interview preparation playground for a role on the NVIDIA NIXL team
([github.com/ai-dynamo/nixl](https://github.com/ai-dynamo/nixl)).  NIXL is the
**NVIDIA Inference Xfer Library** — a high-performance, zero-copy data-movement library
used inside NVIDIA Dynamo to move tensors between DRAM, VRAM, NVMe, and remote nodes
via UCX / NVLink / GDS.

The playground covers the two skill areas that appear most in NIXL's codebase:

| Area | What NIXL does | What this playground covers |
|------|----------------|-----------------------------|
| **Lock-free C++** | Per-agent transfer-request free-list (Treiber stack), MPMC dispatch queue, progress-thread coordination | `pool_and_atomic.h` (reference), 5 exercise headers |
| **Zero-copy Python↔C++** | pybind11 bindings accepting Nx3 numpy descriptors via memcpy, buffer-protocol views, GIL release on all blocking calls | `nix_playground.cpp` (reference), 2 exercise modules |

## Repository layout

```text
playground/
├── cpp/
│   ├── include/interview_playground/
│   │   └── pool_and_atomic.h          ← documented reference: FixedPool + atomic counter
│   └── src/
│       └── core_demo.cpp              ← runnable demo
├── exercises/                         ← IMPLEMENT THESE
│   ├── spsc_ring_buffer.h             ← SPSC queue (acquire/release ordering)
│   ├── treiber_stack.h                ← lock-free stack (CAS, ABA problem)
│   ├── mpmc_ticket_queue.h            ← bounded MPMC queue (sequence numbers)
│   ├── hazard_pointer.h               ← safe memory reclamation
│   ├── seqlock.h                      ← sequence lock (read-mostly data)
│   └── work_stealing_deque.h          ← Chase-Lev deque (NIXL-style dispatch)
└── python/
    ├── demo.py                        ← runs nix_playground bindings
    ├── nix_playground.cpp             ← documented reference: GIL release patterns
    ├── zero_copy_buffer.cpp           ← IMPLEMENT: py::buffer_protocol, py::array_t
    └── iov_descriptor.cpp             ← IMPLEMENT: NIXL-style IOV descriptor binding
```

## What is included

### 1. C++20 core demo (documented reference)

`playground/cpp/include/interview_playground/pool_and_atomic.h` demonstrates and explains:

- A fixed-size preallocated pool returning `std::unique_ptr` with a custom deleter
  (mirrors NIXL's registered-memory reuse pattern)
- `compare_exchange_strong` with `acq_rel/acquire` — exact memory-ordering rationale
- Placement new, `std::launder`, `alignas(T)` — the low-level primitives
- Multi-threaded atomic counter with `memory_order_relaxed` — with explanation of
  why jthread join makes this correct

### 2. Python bindings demo (documented reference)

`playground/python/nix_playground.cpp` demonstrates and explains:

- **Pattern 1** — `py::gil_scoped_release` RAII guard inside a function body
  (fine-grained: GIL released only during blocking work, re-acquired to build result)
- **Pattern 2** — `py::call_guard<py::gil_scoped_release>()` on `.def()`
  (whole-function release for pure-C++ functions)
- When each pattern applies, and what breaks if you omit the release

### 3. Lock-free exercises to implement

| File | Topic | Key concepts |
|------|-------|--------------|
| `spsc_ring_buffer.h` | SPSC queue | acquire/release on head_/tail_, false sharing, cache-line alignment |
| `treiber_stack.h` | Lock-free stack | `compare_exchange_weak`, ABA problem, 3 reclamation strategies |
| `mpmc_ticket_queue.h` | MPMC bounded queue | Per-slot sequence numbers, acq_rel on fetch_add, false-sharing benchmark |
| `hazard_pointer.h` | Memory reclamation | Hazard slots, validate-after-publish loop, seq_cst fence, retire list |
| `seqlock.h` | Read-mostly shared data | Sequence counter, odd/even writer protocol, memcpy snapshot |
| `work_stealing_deque.h` | Chase-Lev deque | Bottom/top split ownership, seq_cst fence pair, steal CAS |

### 4. Zero-copy pybind11 exercises to implement

| File | Topic | Key concepts |
|------|-------|--------------|
| `zero_copy_buffer.cpp` | Buffer protocol | `py::buffer_protocol`, `def_buffer`, `py::buffer`, `py::array_t` with base |
| `iov_descriptor.cpp` | NIXL IOV descriptors | Nx3 numpy→C++ memcpy, numpy view with lifetime base, GIL release on register |

## macOS setup

### Prerequisites

Install the Apple command line tools first:

```bash
xcode-select --install
```

Install build tools with Homebrew:

```bash
brew install cmake ninja python
```

Create a local virtual environment and install pybind11:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install pybind11==2.13.6 numpy
```

## Build the playground

From the repository root:

```bash
cmake -S . -B build -GNinja -DPython3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build build
```

> **Note:** The two exercise pybind11 modules (`zero_copy_buffer`, `iov_descriptor`)
> will compile successfully even with TODOs present — the TODOs throw at runtime,
> not at compile time.  Implement them and watch the tests pass.

## Run everything

Run the C++ demo:

```bash
./build/nix_interview_core
```

Run the Python demo:

```bash
python playground/python/demo.py --module-dir build/python
```

Run the smoke tests:

```bash
ctest --test-dir build --output-on-failure
```

## Suggested practice flow

### Lock-free C++ track

1. Read `pool_and_atomic.h` top-to-bottom — all comments are part of the exercise.
2. Implement `spsc_ring_buffer.h` (easiest — only two atomic variables).
3. Implement `treiber_stack.h` — write out the ABA scenario as a comment first.
4. Implement `mpmc_ticket_queue.h` — benchmark false sharing before/after `alignas(64)`.
5. Implement `hazard_pointer.h` — wire it into your TreiberStack::pop().
6. Implement `seqlock.h` — stress-test with 1 writer + N readers.
7. Implement `work_stealing_deque.h` — stress-test with 1 owner + 3 thieves.

Run every implementation under ThreadSanitizer:
```bash
clang++ -std=c++20 -O1 -fsanitize=thread -I playground/cpp/include \
        playground/cpp/src/core_demo.cpp -o demo_tsan && ./demo_tsan
```

### Zero-copy Python↔C++ track

1. Read `nix_playground.cpp` — understand the two GIL-release patterns.
2. Implement `zero_copy_buffer.cpp` Task 1 (ByteBuffer + buffer protocol).
3. Verify: `python -c "import zero_copy_buffer as z; import numpy as np; b = z.ByteBuffer(64); print(np.frombuffer(b).shape)"`.
4. Implement Task 2 (fill_view) and Task 3 (as_numpy with lifetime base).
5. Implement `iov_descriptor.cpp` Task 1 (numpy constructor).
6. Write the smoke test (Task 5) and verify `numpy.shares_memory()` returns True.
7. Implement Task 4 (register_mock) and confirm the GIL stays released (run a Python timer in a background thread).

### Interview prep questions (discuss with yourself out loud)

**Lock-free C++**
- What is the ABA problem and when does `compare_exchange` not protect against it?
- When is `memory_order_relaxed` safe on a `fetch_add`?  Give a concrete example.
- What is false sharing?  How do `alignas(64)` and `std::hardware_destructive_interference_size` help?
- Compare hazard pointers, epoch-based reclamation, and tagged pointers: trade-offs?
- Why does a seqlock require `seq_cst` fences on the write side on ARM but not on x86?
- In the Chase-Lev deque, why is the `pop_bottom` store of `bottom_` `seq_cst`?

**Zero-copy Python↔C++**
- What is the Python buffer protocol?  Name three Python types that implement it.
- Explain `py::buffer_info`: what are `ptr`, `format`, `shape`, `strides`?
- Why must you set a `base` object when returning `py::array_t` that wraps external memory?
- When is `py::call_guard<py::gil_scoped_release>()` not enough and you need the RAII guard instead?
- In NIXL's nixlXferDList binding, why is a `memcpy` from numpy considered "zero-copy"?
- What does `py::array::unchecked<T, N>()` give you and when is it preferred over `at()`?
