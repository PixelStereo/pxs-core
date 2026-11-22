cmake_minimum_required(VERSION 3.21)
project(pxs-core VERSION 0.9.0 LANGUAGES CXX
        DESCRIPTION "Portable wire-format codecs shared by Pixel Stereo applications (no Qt, no platform API)")

# A parent that already pulled pxs-core in (pxs-qt, then an application that also adds it directly) must not define the
# targets twice.
if(TARGET pxs-core::osc)
    return()
endif()

option(PXS_CORE_BUILD_TESTS "Build the pxs-core tests" ${PROJECT_IS_TOP_LEVEL})
option(PXS_CORE_BUILD_FUZZERS "Build the libFuzzer targets (clang)" OFF)
option(PXS_CORE_INSTALL "Install pxs-core and its CMake package" ${PROJECT_IS_TOP_LEVEL})
option(PXS_CORE_WARNINGS_AS_ERRORS "Treat warnings of pxs-core's own sources as errors (CI)" OFF)
option(PXS_CORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC, Clang)" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

configure_file(include/pxs/core/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/pxs/core/version.h @ONLY)

# Warnings and sanitizers apply to pxs-core's own targets only, never to a consumer (they are PRIVATE).
function(pxs_core_harden target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- $<$<BOOL:${PXS_CORE_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
                               $<$<BOOL:${PXS_CORE_WARNINGS_AS_ERRORS}>:-Werror>)
        if(PXS_CORE_SANITIZE)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined
                                   -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

# One static library per module; every module is independent except for the shared build-time version header.
function(pxs_core_module name)
    cmake_parse_arguments(M "" "" "SOURCES;HEADERS" ${ARGN})
    add_library(pxs_core_${name} STATIC ${M_SOURCES} ${M_HEADERS})
    add_library(pxs-core::${name} ALIAS pxs_core_${name})
    set_target_properties(pxs_core_${name} PROPERTIES EXPORT_NAME ${name} POSITION_INDEPENDENT_CODE ON
                          CXX_EXTENSIONS OFF)
    target_compile_features(pxs_core_${name} PUBLIC cxx_std_17)
    target_include_directories(pxs_core_${name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    pxs_core_harden(pxs_core_${name})
endfunction()

pxs_core_module(osc
    SOURCES src/osc_codec.cpp src/osc_names.cpp
    HEADERS include/pxs/core/osc/codec.h include/pxs/core/osc/names.h)
pxs_core_module(dmx
    SOURCES src/dmx_frame.cpp
    HEADERS include/pxs/core/dmx/frame.h)
pxs_core_module(websocket
    SOURCES src/websocket_frame.cpp src/detail/sha1.cpp src/detail/base64.cpp
    HEADERS include/pxs/core/websocket/frame.h src/detail/sha1.h src/detail/base64.h)
target_include_directories(pxs_core_websocket PRIVATE src)

pxs_core_module(value
    SOURCES src/value.cpp
    HEADERS include/pxs/core/value.h)

pxs_core_module(tree
    SOURCES src/tree.cpp src/tree_parameter.cpp src/tree_osc_bridge.cpp src/tree_oscquery.cpp src/tree_oscquery_server.cpp src/tree_preset.cpp
    HEADERS include/pxs/core/tree/tree.h include/pxs/core/tree/parameter.h include/pxs/core/tree/osc_bridge.h
            include/pxs/core/tree/oscquery.h include/pxs/core/tree/oscquery_server.h include/pxs/core/tree/preset.h)
# nlohmann/json is used inside the library only: its include path is private and none of its types is in a public header.
target_include_directories(pxs_core_tree SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(pxs_core_tree PUBLIC pxs-core::osc pxs-core::value pxs-core::feed)

pxs_core_module(midi
    SOURCES src/midi_message.cpp
    HEADERS include/pxs/core/midi/message.h)

# The C interface (a Flutter application through dart:ffi, another language's runtime): every module behind plain functions. The static
# library always; a shared one (libpxs_c) on request, with only the pxs_* functions exported.
add_library(pxs_core_c STATIC src/c_api.cpp include/pxs/c/pxs.h)
add_library(pxs-core::c ALIAS pxs_core_c)
set_target_properties(pxs_core_c PROPERTIES EXPORT_NAME c POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
target_compile_features(pxs_core_c PUBLIC cxx_std_17)
target_include_directories(pxs_core_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(pxs_core_c PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pxs_core_c PUBLIC pxs-core::tree pxs-core::dmx pxs-core::midi pxs-core::websocket pxs-core::osc pxs-core::value)
pxs_core_harden(pxs_core_c)
option(PXS_CORE_BUILD_SHARED_C "Also build libpxs_c, the C interface as a shared library (for dart:ffi, Android, desktop)" OFF)
if(PXS_CORE_BUILD_SHARED_C)
    add_library(pxs_c SHARED src/c_api.cpp include/pxs/c/pxs.h)
    set_target_properties(pxs_c PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON CXX_EXTENSIONS OFF)
    target_compile_features(pxs_c PUBLIC cxx_std_17)
    target_compile_definitions(pxs_c PUBLIC PXS_C_SHARED PRIVATE PXS_C_BUILDING)
    target_include_directories(pxs_c PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_include_directories(pxs_c PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(pxs_c PRIVATE pxs-core::tree pxs-core::dmx pxs-core::midi pxs-core::websocket pxs-core::osc pxs-core::value)
    pxs_core_harden(pxs_c)
    # Only the pxs_* functions are exported, not the C++ of the modules behind them
    if(APPLE)
        target_link_options(pxs_c PRIVATE -Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/src/pxs_c.exp)
    elseif(UNIX)
        target_link_options(pxs_c PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/pxs_c.map)
    endif()
endif()

# Header-only: the observer of changes (the OSC feedback).
find_package(Threads REQUIRED)
add_library(pxs_core_feed INTERFACE)
add_library(pxs-core::feed ALIAS pxs_core_feed)
set_target_properties(pxs_core_feed PROPERTIES EXPORT_NAME feed)
target_compile_features(pxs_core_feed INTERFACE cxx_std_17)
target_include_directories(pxs_core_feed INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(pxs_core_feed INTERFACE Threads::Threads)

if(PXS_CORE_BUILD_TESTS)
    enable_language(C) # the C interface's header is compiled as C as well
    enable_testing()
    add_subdirectory(test)
endif()
if(PXS_CORE_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

if(PXS_CORE_INSTALL)
    install(TARGETS pxs_core_osc pxs_core_dmx pxs_core_websocket pxs_core_midi pxs_core_feed pxs_core_value pxs_core_tree pxs_core_c EXPORT pxs-core-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/pxs DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/pxs/core/version.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pxs/core)
    install(EXPORT pxs-core-targets NAMESPACE pxs-core:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pxs-core)
    configure_package_config_file(cmake/pxs-core-config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/pxs-core-config.cmake
                                  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pxs-core)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/pxs-core-config-version.cmake
                                     VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pxs-core-config.cmake
                  ${CMAKE_CURRENT_BINARY_DIR}/pxs-core-config-version.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pxs-core)
endif()
