# nvidia-interview-prep

This repository now includes a small C++20 + Python playground aimed at NVIDIA-style systems interviews, especially for topics that show up in projects like NIX:

- smart pointers and custom allocators
- atomics and lock-free programming
- pybind11 bindings
- correct GIL release around blocking native work

## Repository layout

```text
playground/
├── cpp/
│   ├── include/interview_playground/pool_and_atomic.h
│   └── src/core_demo.cpp
├── exercises/
│   ├── mpmc_ticket_queue.h
│   ├── spsc_ring_buffer.h
│   └── treiber_stack.h
└── python/
    ├── demo.py
    └── nix_playground.cpp
```

## What is included

### 1. C++20 core demo

`playground/cpp/src/core_demo.cpp` demonstrates:

- a fixed-size preallocated pool that returns `std::unique_ptr` with a custom deleter
- a multi-threaded atomic counter using `std::atomic` and relaxed increments

### 2. Python bindings demo

`playground/python/nix_playground.cpp` exposes:

- `simulate_transfer(...)` — simulates blocking I/O while using `py::gil_scoped_release`
- `parallel_increment(...)` — calls the C++ atomic demo from Python

### 3. Interview exercises to finish later

The following files are intentionally incomplete and throw `std::logic_error` until you implement them:

- `playground/exercises/spsc_ring_buffer.h`
- `playground/exercises/treiber_stack.h`
- `playground/exercises/mpmc_ticket_queue.h`

Each file includes the problem framing and implementation goals.

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
python -m pip install pybind11==2.13.6
```

## Build the playground

From the repository root:

```bash
cmake -S /absolute/path/to/nvidia-interview-prep -B /absolute/path/to/nvidia-interview-prep/build -GNinja -DPython3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build /absolute/path/to/nvidia-interview-prep/build
```

If you are already in the repository root after cloning, the shorter form is:

```bash
cmake -S . -B build -GNinja -DPython3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build build
```

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

1. Build and run the reference demos.
2. Open one exercise header in `playground/exercises/`.
3. Implement it in-place or copy it into your own scratch file.
4. Add your own tests or benchmark harnesses as you practice.
5. Re-run `ctest --test-dir build --output-on-failure` after every change.