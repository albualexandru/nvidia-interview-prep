#include "interview_playground/pool_and_atomic.h"

#include <iostream>
#include <string_view>

namespace {

auto is_self_test(int argc, char** argv) -> bool {
  return argc > 1 && std::string_view(argv[1]) == "--self-test";
}

}  // namespace

int main(int argc, char** argv) {
  using interview_playground::FixedPool;
  using interview_playground::Packet;

  constexpr std::size_t kThreadCount = 4;
  constexpr std::size_t kIterations = 10'000;

  FixedPool<Packet, 2> pool;
  auto first = pool.make_unique(Packet{1, "frame-1"});
  auto second = pool.make_unique(Packet{2, "frame-2"});
  const auto* recycled_address = first.get();
  first.reset();
  auto recycled = pool.make_unique(Packet{3, "frame-3"});

  const auto final_count =
      interview_playground::run_atomic_counter_demo(kThreadCount, kIterations);
  const auto passed = recycled.get() == recycled_address &&
                      final_count == kThreadCount * kIterations &&
                      pool.available() == 0;

  if (is_self_test(argc, argv)) {
    return passed ? 0 : 1;
  }

  std::cout << "== NVIDIA interview playground ==\n";
  std::cout << "Pool capacity: " << pool.capacity()
            << ", free slots after reuse: " << pool.available() << '\n';
  std::cout << "Recycled packet address reused: "
            << (recycled.get() == recycled_address ? "yes" : "no") << '\n';
  std::cout << "Atomic increments observed: " << final_count << '\n';
  std::cout << "\nExercises to complete next:\n";
  std::cout << "  - playground/exercises/spsc_ring_buffer.h\n";
  std::cout << "  - playground/exercises/treiber_stack.h\n";
  std::cout << "  - playground/exercises/mpmc_ticket_queue.h\n";
  return passed ? 0 : 1;
}
