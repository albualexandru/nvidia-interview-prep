#pragma once

#include <memory>
#include <optional>
#include <stdexcept>

namespace interview_playground::exercises {

// Assignment:
// 1. Implement a Treiber stack with compare_exchange_weak.
// 2. Explain the ABA problem before coding the solution.
// 3. Add a reclamation strategy (hazard pointers, epochs, or tagged pointers).
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
    std::unique_ptr<Node> next;
  };
};

}  // namespace interview_playground::exercises
