# What a consumer does after `cmake --install`: find_package and link. Built by CI against the installed package.
cmake_minimum_required(VERSION 3.21)
project(pxs-core-package-test LANGUAGES CXX)
find_package(pxs-core 0.9 REQUIRED)
add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE pxs-core::osc pxs-core::dmx pxs-core::websocket pxs-core::midi pxs-core::feed pxs-core::value pxs-core::tree pxs-core::c)
enable_testing()
add_test(NAME consumer COMMAND consumer)
