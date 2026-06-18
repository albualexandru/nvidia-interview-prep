// iov_descriptor.cpp — Exercise: NIXL-style IOV descriptor list binding.
//
// NIXL context
// ============
// At the heart of NIXL's Python API is nixlXferDList: a list of (addr, len,
// dev_id) tuples that describe scatter-gather regions for a transfer.  Python
// passes this list to C++, which hands the raw addresses directly to the UCX
// / RDMA hardware.  Two properties are non-negotiable:
//
//   1. Zero allocation overhead: the descriptor list must be buildable from a
//      Python numpy Nx3 uint64 array via a single memcpy (O(N) bytes, no
//      Python object per descriptor).
//
//   2. Zero-copy read-back: the Python side must be able to inspect the list
//      without a round-trip copy (e.g. for logging or validation).
//
// This module is a self-contained miniature of nixlXferDList.  Study the
// real binding in NIXL's src/bindings/python/nixl_bindings.cpp after
// completing this exercise.
//
// BasicDesc layout
// ================
//   struct BasicDesc { uintptr_t addr; size_t len; uint64_t dev_id; };
//
// This is a 24-byte POD struct (3 × 8 bytes on 64-bit platforms).  NIXL
// verifies at compile time that sizeof(nixlBasicDesc) == 3 * sizeof(uint64_t)
// and then uses memcpy to load an Nx3 numpy uint64 array directly into a
// vector<BasicDesc>.
//
// =============================================================================
// TASKS — implement the functions and binding marked TODO below
// =============================================================================
//
// Task 1 — DescList constructor from py::array (Nx3 uint64)
// -----------------------------------------------------------
// Validate:
//   - descs.ndim() == 2 && descs.shape(1) == 3
//   - dtype is uint64 or int64 (NIXL accepts both)
//   - array is C-contiguous (flags & py::array::c_style)
// Then:
//   descs_.resize(n);
//   std::memcpy(descs_.data(), descs.data(), n * sizeof(BasicDesc));
//
// The memcpy is safe because BasicDesc is trivially copyable and its layout
// matches the numpy array's memory layout (verified by the static_assert).
//
// Interview Q: Why check C-contiguity?
// A: A non-contiguous array (e.g. a transposed view) has gaps between rows.
//    memcpy would read the gaps as garbage descriptor values.  The check
//    forces the caller to pass numpy.ascontiguousarray(arr) if needed.
//
// Task 2 — DescList constructor from py::list of (addr, len, dev_id) tuples
// --------------------------------------------------------------------------
// Loop over the list; cast each element to py::tuple; extract three uint64s.
// This is the "safe but slower" path for small lists built in Python.
//
// Task 3 — as_numpy(): return a numpy view of the descriptor array
// ---------------------------------------------------------------
// Return a py::array_t<uint64_t> that points directly into descs_.data()
// with shape (N, 3) and strides (sizeof(BasicDesc), sizeof(uint64_t)).
// Set base = py::cast(self) so the DescList is kept alive.
//
// No copy occurs; the caller can inspect or modify descriptors through the
// returned numpy array.
//
// Task 4 — GIL release on a mock "register" operation
// -----------------------------------------------------
// Add a method register_mock() that pretends to pin the described memory
// (sleeps for 1ms per descriptor).  Release the GIL for the entire operation
// using py::call_guard<py::gil_scoped_release>() on the .def() call.
//
// This mirrors NIXL's registerMem binding pattern.
//
// Task 5 — Python smoke test (playground/python/demo_iov.py)
// -----------------------------------------------------------
// Write a small Python script that:
//   1. Builds a DescList from a numpy Nx3 uint64 array.
//   2. Calls as_numpy() and verifies the returned array shares memory
//      (numpy.shares_memory(original, returned) == True).
//   3. Modifies a descriptor through the numpy view and verifies the change
//      is visible via DescList.__getitem__.
//   4. Calls register_mock() and times it (should be N ms with GIL released,
//      Python scheduler stays live).

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

// BasicDesc — the fundamental scatter-gather descriptor.
// Layout must exactly match a row of a uint64 Nx3 C-contiguous numpy array.
struct BasicDesc {
  uintptr_t addr{0};   // virtual address of the buffer
  std::size_t len{0};  // length in bytes
  uint64_t dev_id{0};  // device identifier (0 = DRAM, 1+ = GPU device index)
};

// Compile-time layout check — same assertion NIXL makes before its memcpy.
static_assert(sizeof(BasicDesc) == 3 * sizeof(uint64_t),
              "BasicDesc layout must be 3 consecutive uint64 values.");
static_assert(alignof(BasicDesc) <= alignof(uint64_t),
              "BasicDesc alignment must not exceed uint64_t alignment.");

// DescList — a vector of BasicDesc descriptors, bindable from Python.
class DescList {
 public:
  DescList() = default;

  // Construct from a Nx3 C-contiguous uint64 numpy array (zero-copy path).
  explicit DescList(py::array descs) {
    throw std::logic_error(
        "TODO: implement DescList(py::array) — validate shape/dtype/contiguity, "
        "resize descs_, memcpy descs.data() into descs_.data().");
  }

  // Construct from a Python list of (addr, len, dev_id) tuples.
  explicit DescList(py::list descs) {
    throw std::logic_error(
        "TODO: implement DescList(py::list) — iterate, cast each element to "
        "py::tuple, extract three uint64s into descs_.emplace_back(...).");
  }

  [[nodiscard]] auto count() const noexcept -> std::size_t {
    return descs_.size();
  }

  // get_item — return one descriptor as a Python tuple (addr, len, dev_id).
  [[nodiscard]] auto get_item(std::size_t i) const -> py::tuple {
    if (i >= descs_.size()) {
      throw py::index_error("descriptor index out of range");
    }
    const auto& d = descs_[i];
    return py::make_tuple(d.addr, d.len, d.dev_id);
  }

  // set_item — update one descriptor from a (addr, len, dev_id) tuple.
  void set_item(std::size_t i, py::tuple t) {
    if (i >= descs_.size()) {
      throw py::index_error("descriptor index out of range");
    }
    if (t.size() != 3) {
      throw py::value_error("descriptor tuple must have exactly 3 elements");
    }
    descs_[i] = BasicDesc{
        t[0].cast<uintptr_t>(),
        t[1].cast<std::size_t>(),
        t[2].cast<uint64_t>()
    };
  }

  // as_numpy — return a numpy uint64 view of the descriptor array (no copy).
  //
  // The returned array has shape (N, 3) and strides
  // (sizeof(BasicDesc), sizeof(uint64_t)) so element [i][0] == descs_[i].addr,
  // [i][1] == descs_[i].len, [i][2] == descs_[i].dev_id.
  //
  // base=py::cast(*this) keeps this DescList alive as long as the array exists.
  [[nodiscard]] auto as_numpy(py::object self) -> py::array_t<uint64_t> {
    throw std::logic_error(
        "TODO: implement DescList::as_numpy — "
        "construct py::array_t<uint64_t> with shape {count(),3}, "
        "strides {sizeof(BasicDesc), sizeof(uint64_t)}, "
        "pointer descs_.data(), base=self.");
  }

  // register_mock — simulate the latency of NIXL's registerMem (memory
  // pinning for RDMA).  In real NIXL this calls ibv_reg_mr / cuMemHostRegister
  // which may block for 100µs–10ms per region.
  // The GIL must be released before calling this from Python.
  void register_mock() const {
    // GIL is already released by py::call_guard on the .def() — do not touch
    // any Python objects here.
    for (std::size_t i = 0; i < descs_.size(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  [[nodiscard]] auto data() noexcept -> BasicDesc* { return descs_.data(); }
  [[nodiscard]] auto data() const noexcept -> const BasicDesc* {
    return descs_.data();
  }

 private:
  std::vector<BasicDesc> descs_;
};

PYBIND11_MODULE(iov_descriptor, module) {
  module.doc() =
      "NIXL-style IOV descriptor list binding exercise.\n\n"
      "Demonstrates:\n"
      "  DescList(numpy_array)  — build from Nx3 uint64 numpy (zero-copy)\n"
      "  DescList(list_of_tuples) — build from Python list\n"
      "  as_numpy()             — expose descriptor memory as numpy (no copy)\n"
      "  register_mock()        — blocking call with GIL released\n\n"
      "Study alongside src/bindings/python/nixl_bindings.cpp in the NIXL repo.";

  // TODO: bind BasicDesc as a named tuple or dataclass for introspection.
  // Hint: py::class_<BasicDesc>(module, "BasicDesc")
  //           .def_readwrite("addr", &BasicDesc::addr)
  //           .def_readwrite("len", &BasicDesc::len)
  //           .def_readwrite("dev_id", &BasicDesc::dev_id);

  py::class_<DescList>(module, "DescList")
      // TODO: add py::init constructors for both numpy-array and list-of-tuples paths.
      // Hint: use .noconvert() on the numpy overload so pybind11 won't silently
      //       convert a list to numpy — matching NIXL's exact overload resolution.
      //   .def(py::init<py::array>(), py::arg("descs").noconvert())
      //   .def(py::init<py::list>(), py::arg("descs"))
      .def("count", &DescList::count,
           "Return the number of descriptors.")
      .def("__len__", &DescList::count)
      .def("__getitem__", &DescList::get_item, py::arg("i"),
           "Return descriptor i as a (addr, len, dev_id) tuple.")
      .def("__setitem__", &DescList::set_item, py::arg("i"), py::arg("desc"),
           "Update descriptor i from a (addr, len, dev_id) tuple.")
      .def("as_numpy",
           [](DescList& self) {
             return self.as_numpy(py::cast(self));
           },
           "Return a numpy uint64 view of the descriptor array (shape Nx3).\n"
           "No copy — shares memory with the DescList.\n"
           "The DescList is kept alive as long as the array is held.")
      .def("register_mock", &DescList::register_mock,
           py::call_guard<py::gil_scoped_release>(),
           "Simulate NIXL registerMem: sleep 1ms per descriptor.\n"
           "GIL is released for the entire duration.");
}
