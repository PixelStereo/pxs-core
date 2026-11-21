cmake_minimum_required(VERSION 3.21)
project(pxs-core VERSION 0.1.0 LANGUAGES CXX)

add_library(pxs_core_osc STATIC src/osc_codec.cpp include/pxs/core/osc/codec.h)
add_library(pxs-core::osc ALIAS pxs_core_osc)
target_compile_features(pxs_core_osc PUBLIC cxx_std_17)
target_include_directories(pxs_core_osc PUBLIC include)
set_target_properties(pxs_core_osc PROPERTIES POSITION_INDEPENDENT_CODE ON)
