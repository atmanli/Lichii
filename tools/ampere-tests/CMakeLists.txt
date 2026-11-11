# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)
project(ampere_quick_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(READ "${REPOSITORY_ROOT}/cpmfile.json" dependency_pins)
foreach(field repo version hash)
    string(JSON catch2_${field} GET "${dependency_pins}" catch2 ${field})
    string(JSON fmt_${field} GET "${dependency_pins}" fmt ${field})
endforeach()

include(FetchContent)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Catch2
    URL "https://github.com/${catch2_repo}/archive/${catch2_version}.tar.gz"
    URL_HASH "SHA512=${catch2_hash}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fmt
    URL "https://github.com/${fmt_repo}/archive/${fmt_version}.tar.gz"
    URL_HASH "SHA512=${fmt_hash}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(Catch2 fmt)

add_executable(ampere-quick-tests
    main.cpp
    "${REPOSITORY_ROOT}/src/tests/video_core/ampere_yoshi_compute.cpp"
    "${REPOSITORY_ROOT}/src/tests/video_core/ampere_yoshi_imad.cpp"
    "${REPOSITORY_ROOT}/src/tests/video_core/ampere_geometry_exit.cpp"
    "${REPOSITORY_ROOT}/src/tests/video_core/ampere_compute_lowering.cpp")
target_compile_features(ampere-quick-tests PRIVATE cxx_std_20)
target_include_directories(ampere-quick-tests PRIVATE "${REPOSITORY_ROOT}/src")
target_link_libraries(ampere-quick-tests PRIVATE Catch2::Catch2 fmt::fmt-header-only)

enable_testing()
add_test(NAME ampere-lowering COMMAND ampere-quick-tests)
add_test(NAME ampere-empty-selection
    COMMAND "${CMAKE_COMMAND}" "-DTEST_EXECUTABLE=$<TARGET_FILE:ampere-quick-tests>"
            -P "${CMAKE_CURRENT_LIST_DIR}/verify-empty-selection.cmake")
