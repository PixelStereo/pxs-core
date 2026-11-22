# libFuzzer targets (clang): cmake -DCMAKE_CXX_COMPILER=clang++ -DPXS_CORE_BUILD_FUZZERS=ON, then run for instance
#   ./fuzz/fuzz_osc -max_total_time=60 fuzz/corpus/osc
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "PXS_CORE_BUILD_FUZZERS needs clang (libFuzzer)")
endif()
foreach(target osc dmx websocket midi value tree)
    add_executable(fuzz_${target} fuzz_${target}.cpp)
    target_link_libraries(fuzz_${target} PRIVATE pxs-core::${target})
    target_compile_options(fuzz_${target} PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)
    target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer,address,undefined)
endforeach()
target_link_libraries(fuzz_osc PRIVATE pxs-core::osc)
