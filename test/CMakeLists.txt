set(TEST_SOURCES
    test_types.cpp
    test_crc16.cpp
    test_sha256.cpp
    test_hmac.cpp
    test_message_header.cpp
    test_e2e_protection.cpp
    test_diagnostic_counters.cpp
    test_method_dispatcher.cpp
    test_event_manager.cpp
    test_request_tracker.cpp
    test_message_authenticator.cpp
    test_sd_payloads.cpp
    test_service_discovery.cpp
    test_service_event_handler.cpp
    test_runtime.cpp
)

add_executable(sero_tests ${TEST_SOURCES})
target_link_libraries(sero_tests PRIVATE sero GTest::gtest_main)
target_compile_options(sero_tests PRIVATE -Wall -Wextra -Wpedantic)

if(CODE_COVERAGE)
    target_compile_options(sero_tests PRIVATE --coverage -O0 -g)
    target_link_options(sero_tests PRIVATE --coverage)
endif()

include(GoogleTest)
gtest_discover_tests(sero_tests)

# ---------- coverage target (requires CODE_COVERAGE=ON + lcov) ---
if(CODE_COVERAGE)
    find_program(LCOV_BIN  lcov   REQUIRED)
    find_program(GENHTML_BIN genhtml REQUIRED)

    set(COVERAGE_DIR "${CMAKE_BINARY_DIR}/coverage")

    add_custom_target(coverage
        COMMENT "Generating HTML coverage report in ${COVERAGE_DIR}/"

        # 1. Wipe old counters
        COMMAND ${LCOV_BIN} --zerocounters --directory ${CMAKE_BINARY_DIR}

        # 2. Run tests
        COMMAND $<TARGET_FILE:sero_tests>

        # 3. Capture counters
        COMMAND ${LCOV_BIN}
                --capture
                --directory ${CMAKE_BINARY_DIR}
                --output-file ${CMAKE_BINARY_DIR}/coverage.info
                --rc branch_coverage=1
                --ignore-errors mismatch,source

        # 4. Keep only our own headers; strip googletest + system paths
        COMMAND ${LCOV_BIN}
                --extract ${CMAKE_BINARY_DIR}/coverage.info
                "${CMAKE_SOURCE_DIR}/include/sero/*"
                "${CMAKE_SOURCE_DIR}/include/sero/core/*"
                "${CMAKE_SOURCE_DIR}/include/sero/security/*"
                "${CMAKE_SOURCE_DIR}/include/sero/service/*"
                --output-file ${CMAKE_BINARY_DIR}/coverage_filtered.info
                --rc branch_coverage=1
                --ignore-errors mismatch,source,unused

        # 5. Generate HTML (run via shell so warning exit-codes don't abort make)
        COMMAND bash -c
            "${GENHTML_BIN}\
                ${CMAKE_BINARY_DIR}/coverage_filtered.info\
                --output-directory ${COVERAGE_DIR}\
                --branch-coverage\
                --demangle-cpp\
                --title 'sero coverage'\
                --ignore-errors source || true"

        COMMAND ${CMAKE_COMMAND} -E echo
            "Coverage report: file://${COVERAGE_DIR}/index.html"

        DEPENDS sero_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()
