# CMakeLists.txt - builds the portable (OS-independent) core and its host tests.
#
# The Windows-only components (IddCx driver, WPF control app, Windows HID
# transport) are built with their own toolchains; see docs/build.md.
#
# Usage:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build
#   ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(AcemagicS1Display CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra)

# --- Portable core library -------------------------------------------------
add_library(s1_core STATIC
  shared/src/s1_protocol.cpp
  shared/src/s1_log.cpp
  usb/src/s1_device.cpp
  usb/src/s1_transport_mock.cpp
  renderer/src/frame_pipeline.cpp
)
target_include_directories(s1_core PUBLIC
  shared/include
  usb/include
  renderer/include
)

# --- Tests -----------------------------------------------------------------
enable_testing()

function(s1_add_test name src)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE s1_core)
  target_include_directories(${name} PRIVATE shared/tests)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

s1_add_test(test_protocol       shared/tests/test_protocol.cpp)
s1_add_test(test_ipc_layout     shared/tests/test_ipc_layout.cpp)
s1_add_test(test_s1_device      usb/tests/test_s1_device.cpp)
s1_add_test(test_frame_pipeline renderer/tests/test_frame_pipeline.cpp)
