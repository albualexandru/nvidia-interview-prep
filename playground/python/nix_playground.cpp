// nix_playground.cpp — pybind11 bindings for the NVIDIA interview playground.
//
// NIXL context: GIL management
// =============================
// Python's Global Interpreter Lock (GIL) serialises all Python bytecode
// execution.  When a C extension calls a blocking operation (I/O, network,
// GPU kernel wait) without releasing the GIL, every other Python thread is
// frozen for the duration — including asyncio event loops and other workers.
//
// NIXL releases the GIL around *every* blocking call in its pybind11 bindings:
//   registerMem    → py::call_guard<py::gil_scoped_release>()
//   postXferReq    → py::call_guard<py::gil_scoped_release>()
//   getXferStatus  → py::call_guard<py::gil_scoped_release>()
//   makeXferReq    → explicit py::gil_scoped_release inside the lambda
//
// This lets an inference framework's Python scheduler keep dispatching new
// requests while a previous transfer is in flight over UCX / NVLink / GDS.
//
// Two GIL-release patterns are shown below:
//   1. py::gil_scoped_release RAII guard inside a function body  — fine-grained
//   2. py::call_guard<py::gil_scoped_release>() on .def()        — whole function
//
// Use pattern 1 when you need the GIL back partway through (e.g. to build a
// py::dict result); use pattern 2 for pure-C++ functions that never touch
// Python objects.
//
// Interview discussion points:
//   Q: What happens if you forget to release the GIL in a long transfer?
//   A: The entire Python process serialises on that transfer; no other Python
//      thread can make progress, including the event loop that might cancel it.
//
//   Q: When must you re-acquire the GIL inside a py::gil_scoped_release block?
//   A: Any time you touch a Python object (py::dict, py::list, py::object).
//      Building the result dict below happens after the release guard destructs,
//      i.e. after the GIL is re-acquired automatically.

#include "interview_playground/pool_and_atomic.h"

#include <chrono>
#include <thread>

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace {

// simulate_transfer
// -----------------
// Mimics the latency profile of a NIXL transfer: a sequence of blocking
// "chunk" operations (e.g. DMA transfers, NVMe reads) each taking delay_ms.
//
// Pattern 1 GIL release: the RAII guard releases the GIL for the entire
// blocking section and re-acquires it before we touch any Python object.
// This matches NIXL's postXferReq / getXferStatus binding pattern.
auto simulate_transfer(std::size_t chunks, int delay_ms) -> py::dict {
  const auto start = std::chrono::steady_clock::now();

  {
    // py::gil_scoped_release: destructor re-acquires the GIL at end of block.
    // Any Python thread can run freely while we are "in flight" here.
    py::gil_scoped_release release;
    for (std::size_t index = 0; index < chunks; ++index) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    // GIL re-acquired here when `release` destructs.
  }

  const auto finish = std::chrono::steady_clock::now();
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
          .count();

  // We now hold the GIL again — safe to construct Python objects.
  py::dict result;
  result["chunks"] = chunks;
  result["delay_ms"] = delay_ms;
  result["elapsed_ms"] = elapsed_ms;
  result["note"] = "py::gil_scoped_release kept Python responsive during I/O";
  return result;
}

}  // namespace

PYBIND11_MODULE(nix_playground, module) {
  module.doc() =
      "Minimal NVIDIA interview playground bindings.\n\n"
      "Demonstrates:\n"
      "  simulate_transfer  — GIL release around blocking native work\n"
      "  parallel_increment — C++ thread pool called from Python\n\n"
      "See also: zero_copy_buffer (buffer protocol) and\n"
      "          iov_descriptor (NIXL-style descriptor list binding).";

  // py::arg() names let callers use keyword arguments from Python.
  module.def("simulate_transfer", &simulate_transfer,
             py::arg("chunks") = 4,
             py::arg("delay_ms") = 25,
             "Simulate a blocking transfer while the GIL is released.\n"
             "Returns a dict with timing data.  Pattern 1 GIL release.");

  // Pattern 2: py::call_guard<py::gil_scoped_release>() — the concise form
  // when the entire C++ function is GIL-free (no Python objects constructed
  // inside run_atomic_counter_demo).
  module.def("parallel_increment",
             &interview_playground::run_atomic_counter_demo,
             py::arg("thread_count") = 4,
             py::arg("increments_per_thread") = 10'000,
             py::call_guard<py::gil_scoped_release>(),
             "Increment a shared atomic counter from multiple C++ threads.\n"
             "Uses memory_order_relaxed — safe because jthread join provides\n"
             "the happens-before edge.  Pattern 2 (whole-function) GIL release.");
}
