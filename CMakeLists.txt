cmake_minimum_required(VERSION 3.21)

project(
  hwlib_worker
  VERSION 0.2.0
  LANGUAGES CXX)

add_library(hwlib_worker INTERFACE)
add_library(Hwlib::worker ALIAS hwlib_worker)

target_include_directories(hwlib_worker
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(hwlib_worker INTERFACE cxx_std_20)

# Dependants read the version from this property rather than from a variable: a
# variable set here is invisible in a sibling directory, and a stale one left in
# the parent scope would be read instead — a version check that silently passes.
set_target_properties(hwlib_worker PROPERTIES HWLIB_VERSION ${PROJECT_VERSION})

if(PROJECT_IS_TOP_LEVEL)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2)
  FetchContent_MakeAvailable(googletest)

  include(GoogleTest)
  enable_testing()

  add_executable(worker-test tests/worker_test.cpp)
  target_link_libraries(worker-test PRIVATE Hwlib::worker GTest::gtest_main)
  gtest_discover_tests(worker-test)
endif()
