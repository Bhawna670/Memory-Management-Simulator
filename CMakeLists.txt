cmake_minimum_required(VERSION 3.13)
project(MemoryManagementSimulator VERSION 0.10.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Sanitizers are opt-in (not available on MinGW): cmake -DMEMSIM_SANITIZERS=ON ..
option(MEMSIM_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

function(memsim_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(MEMSIM_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

# ---- Core library: every module except main.cpp --------------------------
# Later checkpoints append their sources to this list.
add_library(memsim_core STATIC
    MemoryManagementSimulator/src/MemoryBlock.cpp
    MemoryManagementSimulator/src/ProcessRegistry.cpp
    MemoryManagementSimulator/src/PlacementStrategy.cpp
    MemoryManagementSimulator/src/FirstFit.cpp
    MemoryManagementSimulator/src/BestFit.cpp
    MemoryManagementSimulator/src/WorstFit.cpp
    MemoryManagementSimulator/src/MemoryAllocator.cpp
    MemoryManagementSimulator/src/FragmentationAnalyzer.cpp
    MemoryManagementSimulator/src/PageReplacement.cpp
    MemoryManagementSimulator/src/FIFO.cpp
    MemoryManagementSimulator/src/LRU.cpp
    MemoryManagementSimulator/src/Optimal.cpp
    MemoryManagementSimulator/src/PageReplacementSimulator.cpp
    MemoryManagementSimulator/src/PerformanceAnalyzer.cpp
    MemoryManagementSimulator/src/InputValidator.cpp
    MemoryManagementSimulator/src/TablePrinter.cpp
    MemoryManagementSimulator/src/CliApp.cpp
)
target_include_directories(memsim_core PUBLIC include)
memsim_configure_target(memsim_core)

# ---- Application ---------------------------------------------------------
add_executable(memsim MemoryManagementSimulator/src/main.cpp)
target_link_libraries(memsim PRIVATE memsim_core)
memsim_configure_target(memsim)

# ---- Tests ---------------------------------------------------------------
enable_testing()

add_executable(allocation_tests tests/allocation_tests.cpp)
target_include_directories(allocation_tests PRIVATE tests)
target_link_libraries(allocation_tests PRIVATE memsim_core)
memsim_configure_target(allocation_tests)
add_test(NAME allocation_tests COMMAND allocation_tests)

add_executable(placement_tests tests/placement_tests.cpp)
target_include_directories(placement_tests PRIVATE tests)
target_link_libraries(placement_tests PRIVATE memsim_core)
memsim_configure_target(placement_tests)
add_test(NAME placement_tests COMMAND placement_tests)

add_executable(allocator_tests tests/allocator_tests.cpp)
target_include_directories(allocator_tests PRIVATE tests)
target_link_libraries(allocator_tests PRIVATE memsim_core)
memsim_configure_target(allocator_tests)
add_test(NAME allocator_tests COMMAND allocator_tests)

add_executable(page_replacement_tests tests/page_replacement_tests.cpp)
target_include_directories(page_replacement_tests PRIVATE tests)
target_link_libraries(page_replacement_tests PRIVATE memsim_core)
memsim_configure_target(page_replacement_tests)
add_test(NAME page_replacement_tests COMMAND page_replacement_tests)

add_executable(performance_tests tests/performance_tests.cpp)
target_include_directories(performance_tests PRIVATE tests)
target_link_libraries(performance_tests PRIVATE memsim_core)
memsim_configure_target(performance_tests)
add_test(NAME performance_tests COMMAND performance_tests)

add_executable(cli_tests tests/cli_tests.cpp)
target_include_directories(cli_tests PRIVATE tests)
target_link_libraries(cli_tests PRIVATE memsim_core)
memsim_configure_target(cli_tests)
add_test(NAME cli_tests COMMAND cli_tests)

add_executable(edge_case_tests tests/edge_case_tests.cpp)
target_include_directories(edge_case_tests PRIVATE tests)
target_link_libraries(edge_case_tests PRIVATE memsim_core)
memsim_configure_target(edge_case_tests)
add_test(NAME edge_case_tests COMMAND edge_case_tests)


