cmake_minimum_required(VERSION 3.20)
project(nvidia_interview_prep LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

execute_process(
  COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
  OUTPUT_VARIABLE pybind11_DIR
  ERROR_VARIABLE pybind11_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE pybind11_RESULT
)

if(NOT pybind11_RESULT EQUAL 0)
  message(FATAL_ERROR
    "pybind11 is required. Install it with `${Python3_EXECUTABLE} -m pip install pybind11==2.13.6`.\n"
    "${pybind11_ERROR}"
  )
endif()

list(APPEND CMAKE_PREFIX_PATH "${pybind11_DIR}")
find_package(pybind11 CONFIG REQUIRED)

add_library(interview_playground_headers INTERFACE)
target_include_directories(interview_playground_headers INTERFACE
  "${CMAKE_SOURCE_DIR}/playground/cpp/include"
)

add_executable(nix_interview_core
  playground/cpp/src/core_demo.cpp
)
target_link_libraries(nix_interview_core PRIVATE interview_playground_headers)

pybind11_add_module(nix_playground MODULE playground/python/nix_playground.cpp)
target_link_libraries(nix_playground PRIVATE interview_playground_headers)
set_target_properties(nix_playground PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
)

if(BUILD_TESTING)
  add_test(NAME core_smoke COMMAND nix_interview_core --self-test)
  add_test(
    NAME python_smoke
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/playground/python/demo.py"
      --module-dir
      "${CMAKE_BINARY_DIR}/python"
      --chunks
      4
      --delay-ms
      5
  )
endif()
