#include "interview_playground/pool_and_atomic.h"

#include <chrono>
#include <thread>

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace {

auto simulate_transfer(std::size_t chunks, int delay_ms) -> py::dict {
  const auto start = std::chrono::steady_clock::now();

  {
    py::gil_scoped_release release;
    for (std::size_t index = 0; index < chunks; ++index) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
  }

  const auto finish = std::chrono::steady_clock::now();
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
          .count();

  py::dict result;
  result["chunks"] = chunks;
  result["delay_ms"] = delay_ms;
  result["elapsed_ms"] = elapsed_ms;
  result["note"] = "py::gil_scoped_release kept Python responsive during I/O";
  return result;
}

}  // namespace

PYBIND11_MODULE(nix_playground, module) {
  module.doc() = "Minimal NVIDIA interview playground bindings";

  module.def("simulate_transfer", &simulate_transfer, py::arg("chunks") = 4,
             py::arg("delay_ms") = 25,
             "Simulate a blocking transfer while the GIL is released.");
  module.def("parallel_increment", &interview_playground::run_atomic_counter_demo,
             py::arg("thread_count") = 4,
             py::arg("increments_per_thread") = 10'000,
             "Increment a shared atomic counter from multiple C++ threads.");
}
