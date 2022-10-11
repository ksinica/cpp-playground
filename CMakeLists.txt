cmake_minimum_required(VERSION 3.2)
project(
    btgw
    DESCRIPTION "C++ Playground"
    HOMEPAGE_URL "https://github.com/ksinica/cpp-playground"
    LANGUAGES CXX
)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type provided, default to RELEASE.")
  set(CMAKE_BUILD_TYPE "RELEASE")
endif ()

include_directories(vendor/asio/asio/include)

add_executable(asio_channels)
add_executable(asio_observable)
target_sources(asio_channels PRIVATE src/asio_channels.cpp)
target_sources(asio_observable PRIVATE src/asio_observable.cpp)
