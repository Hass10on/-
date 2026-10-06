# Builds the engine-agnostic RefCore sources (shared with the Unreal module) for tests and balance runs.
#   cmake -S RefereeCareer/Tests/Core -B build/core && cmake --build build/core && ctest --test-dir build/core
cmake_minimum_required(VERSION 3.16)
project(RefCoreTests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Source/RefereeCareer)
set(DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Content/Data)
file(GLOB REFCORE_SOURCES ${MODULE_DIR}/Private/RefCore/*.cpp)

# Unreal builds with exceptions and RTTI off and treats shadowing as an error; mirror that here.
set(REFCORE_FLAGS -Wall -Wextra -Wshadow -Werror -fno-exceptions -fno-rtti)

add_library(refcore STATIC ${REFCORE_SOURCES})
target_include_directories(refcore PUBLIC ${MODULE_DIR}/Public)
target_compile_options(refcore PRIVATE ${REFCORE_FLAGS})

# Unreal compiles modules as unity (jumbo) files; this target catches name clashes between .cpp files.
set(UNITY_FILE ${CMAKE_CURRENT_BINARY_DIR}/refcore_unity.cpp)
file(WRITE ${UNITY_FILE} "")
foreach(src ${REFCORE_SOURCES})
  file(APPEND ${UNITY_FILE} "#include \"${src}\"\n")
endforeach()
add_library(refcore_unity STATIC ${UNITY_FILE})
target_include_directories(refcore_unity PUBLIC ${MODULE_DIR}/Public)
target_compile_options(refcore_unity PRIVATE ${REFCORE_FLAGS})

add_executable(refcore_tests test_core.cpp)
target_link_libraries(refcore_tests refcore)
target_compile_options(refcore_tests PRIVATE ${REFCORE_FLAGS})
target_compile_definitions(refcore_tests PRIVATE REFCORE_DATA_DIR="${DATA_DIR}")

add_executable(career_sim career_sim.cpp)
target_link_libraries(career_sim refcore)
target_compile_options(career_sim PRIVATE ${REFCORE_FLAGS})
target_compile_definitions(career_sim PRIVATE REFCORE_DATA_DIR="${DATA_DIR}")

enable_testing()
add_test(NAME refcore_tests COMMAND refcore_tests)
add_test(NAME career_balance COMMAND career_sim --careers 300 --check)
