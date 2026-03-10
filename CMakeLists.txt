cmake_minimum_required(VERSION 3.14)
project(sero
    VERSION 0.2.0
    DESCRIPTION "Sero: Embedded-first realtime service-oriented communication"
    LANGUAGES CXX
)

# ---------- header-only interface library ----------
add_library(sero INTERFACE)
target_include_directories(sero INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(sero INTERFACE cxx_std_17)

# ---------- build verification executable ----------
add_executable(verify_build src/verify_build.cpp)
target_link_libraries(verify_build PRIVATE sero)
target_compile_options(verify_build PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)

# ---------- example executables ----------
add_executable(example_server examples/server.cpp)
target_link_libraries(example_server PRIVATE sero)
target_include_directories(example_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
target_compile_options(example_server PRIVATE -Wall -Wextra -Wpedantic)

add_executable(example_client examples/client.cpp)
target_link_libraries(example_client PRIVATE sero)
target_include_directories(example_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
target_compile_options(example_client PRIVATE -Wall -Wextra -Wpedantic)

add_executable(desktop_peer examples/desktop_peer.cpp)
target_link_libraries(desktop_peer PRIVATE sero)
target_include_directories(desktop_peer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
target_compile_options(desktop_peer PRIVATE -Wall -Wextra -Wpedantic)

add_executable(diag_scanner examples/diag_scanner.cpp)
target_link_libraries(diag_scanner PRIVATE sero)
target_include_directories(diag_scanner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
target_compile_options(diag_scanner PRIVATE -Wall -Wextra -Wpedantic)

# ---------- code coverage ----------------------------------------
option(CODE_COVERAGE "Enable gcov/lcov code coverage" OFF)

# ---------- unit tests (Google Test via FetchContent) ----------
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    # Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(test)
endif()
