// zero_copy_buffer.cpp — Exercise: pybind11 buffer protocol and zero-copy APIs.
//
// NIXL context
// ============
// NIXL's Python bindings must pass large tensor/byte buffers from Python to
// C++ (for registerMem, which takes a descriptor list with raw addresses) and
// expose transfer results back to Python without copying.  The two tools are:
//
//   py::buffer_protocol   — lets a C++ class be seen as a Python memoryview /
//                           buffer (used by numpy, bytearray, etc.)
//   py::array_t<T>        — wraps a C++ pointer as a numpy array that owns or
//                           borrows the memory.  With base=py::none() and the
//                           right strides, no copy occurs.
//   py::buffer            — accepts any Python object that supports the buffer
//                           protocol (numpy arrays, bytearray, memoryview) and
//                           extracts a raw C++ pointer without copying.
//
// Real NIXL usage (from src/bindings/python/nixl_bindings.cpp):
//   - nixlXferDList accepts a py::array (Nx3 uint64) and memcpys the raw
//     bytes directly into the C++ descriptor list — O(N) but zero Python
//     object allocations.
//   - The unchecked<T,N>() accessor on py::array_t gives a raw pointer for
//     direct indexed access without bounds-checking overhead.
//   - py::call_guard<py::gil_scoped_release>() on registerMem ensures the
//     GIL is dropped before touching UCX / RDMA memory registration, which
//     may block for milliseconds.
//
// This module exercises all three patterns in isolation so you can understand
// each independently before reading the full NIXL binding.
//
// =============================================================================
// TASKS — implement the three functions marked TODO below
// =============================================================================
//
// Task 1 — ByteBuffer: expose a C++ std::vector<uint8_t> as a Python memoryview
// ---------------------------------------------------------------------------
// A class exposing py::buffer_protocol allows Python code to do:
//   buf = zero_copy_buffer.ByteBuffer(1024)
//   mv  = memoryview(buf)           # zero copy — same memory
//   arr = numpy.frombuffer(buf, dtype=numpy.uint8)  # also zero copy
//
// What to implement:
//   PYBIND11_MODULE: bind ByteBuffer with py::buffer_protocol()
//   .def_buffer([](ByteBuffer& b) -> py::buffer_info {
//       return py::buffer_info(
//           b.data(),                         // pointer to first element
//           sizeof(uint8_t),                  // size of one element
//           py::format_descriptor<uint8_t>::format(),  // Python struct format
//           1,                                // number of dimensions
//           {b.size()},                       // shape
//           {sizeof(uint8_t)}                 // strides (bytes between elements)
//       );
//   })
//
// Interview Q: What happens if the C++ object is destroyed before the
//              memoryview is released?
// A: pybind11 keeps the C++ object alive as long as any Python buffer view
//    holds a reference (via the `base` object in py::buffer_info).  You must
//    pass `py::cast(self)` as the base to enable this lifetime extension.
//
// Task 2 — fill_view: accept any Python buffer without copying
// ---------------------------------------------------------------
// def fill_view(buf, value: int) -> None
//   Accept a writable Python buffer (numpy array, bytearray, memoryview),
//   fill every byte with `value`, and return None.  No Python-level copy.
//
// Implementation sketch:
//   py::buffer b_arg = ...; (function parameter type)
//   py::buffer_info info = b_arg.request(/* writable= */ true);
//   auto* ptr = static_cast<uint8_t*>(info.ptr);
//   std::memset(ptr, value, info.size * info.itemsize);
//
// Interview Q: What does request(writable=true) enforce?
// A: It raises BufferError if the underlying buffer is read-only
//    (e.g. a Python bytes object).  This matches NIXL's check that
//    registered memory is writable before pinning it for RDMA.
//
// Task 3 — as_numpy: return a C++ array to Python as a numpy array (no copy)
// -------------------------------------------------------------------------
// def as_numpy(buf: ByteBuffer) -> numpy.ndarray
//   Return a 1-D numpy array of float32 that directly points into ByteBuffer's
//   storage.  The numpy array must keep `buf` alive (set its base object).
//
// Implementation sketch:
//   return py::array_t<float>(
//       {buf.size() / sizeof(float)},   // shape
//       {sizeof(float)},                // strides
//       reinterpret_cast<float*>(buf.data()),
//       py::cast(buf)                   // base: keeps buf alive
//   );
//
// Interview Q: Why does the base object matter?
// A: Without a base, numpy would not know who owns the memory.  When the
//    array goes out of scope it would not decrement any reference count,
//    risking a dangling pointer if the C++ object was destroyed.  Setting
//    base=py::cast(buf) increments buf's refcount, so buf lives at least as
//    long as the returned array.
//
// Task 4 — benchmark zero-copy vs copy
// ----------------------------------------
// Add a Python test in playground/python/demo_zero_copy.py that:
//   1. Allocates a ByteBuffer of 64 MiB.
//   2. Wraps it as a numpy array via as_numpy() (zero copy).
//   3. Copies it with numpy.array(as_numpy(buf), copy=True).
//   4. Times both and prints the ratio.
//   Expected: zero-copy wrapper completes in microseconds;
//             copy completes in milliseconds.

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

// ByteBuffer — a simple heap buffer that will expose the buffer protocol.
// In real NIXL, the equivalent is a registered memory region: a pinned slab
// whose virtual address is passed to the RDMA hardware.
class ByteBuffer {
 public:
  explicit ByteBuffer(std::size_t size) : data_(size, 0) {}

  [[nodiscard]] auto data() noexcept -> uint8_t* { return data_.data(); }
  [[nodiscard]] auto data() const noexcept -> const uint8_t* { return data_.data(); }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return data_.size(); }

 private:
  std::vector<uint8_t> data_;
};

// fill_view — fills any writable Python buffer with a constant byte value.
// Demonstrates accepting py::buffer (the zero-copy input side).
static void fill_view(py::buffer buf, uint8_t value) {
  throw std::logic_error(
      "TODO: implement fill_view — "
      "call buf.request(true), cast info.ptr to uint8_t*, "
      "call std::memset, return void.");
}

// as_numpy — wraps ByteBuffer's storage as a numpy float32 array (no copy).
// Demonstrates returning py::array_t with a base object for lifetime safety.
static auto as_numpy(ByteBuffer& buf) -> py::array_t<float> {
  throw std::logic_error(
      "TODO: implement as_numpy — "
      "construct py::array_t<float> with shape, stride, pointer, "
      "and base=py::cast(buf) to keep buf alive.");
}

PYBIND11_MODULE(zero_copy_buffer, module) {
  module.doc() =
      "Zero-copy buffer protocol exercise.\n\n"
      "Implements:\n"
      "  ByteBuffer        — C++ buffer exposed as Python memoryview\n"
      "  fill_view(buf, v) — write into any Python buffer without copying\n"
      "  as_numpy(buf)     — wrap C++ memory as numpy array without copying\n\n"
      "See module docstrings and comments for the NIXL connection.";

  // TODO: bind ByteBuffer with py::buffer_protocol() so that
  //   memoryview(ByteBuffer(n)) and numpy.frombuffer(ByteBuffer(n)) work.
  //
  // Skeleton:
  //   py::class_<ByteBuffer>(module, "ByteBuffer", py::buffer_protocol())
  //       .def(py::init<std::size_t>(), py::arg("size"),
  //            "Allocate a zero-initialised buffer of `size` bytes.")
  //       .def("size", &ByteBuffer::size)
  //       .def_buffer([](ByteBuffer& b) -> py::buffer_info { ... });
  throw std::logic_error(
      "TODO: add py::class_<ByteBuffer> binding with .def_buffer(...).");

  module.def("fill_view", &fill_view,
             py::arg("buf"), py::arg("value"),
             "Fill every byte of a writable Python buffer with `value`.\n"
             "Accepts numpy arrays, bytearray, memoryview — no copy.");

  module.def("as_numpy", &as_numpy,
             py::arg("buf"),
             "Return a numpy float32 view of ByteBuffer's storage.\n"
             "The returned array points into the same memory — no copy.\n"
             "The ByteBuffer is kept alive as long as the array is live.");
}
