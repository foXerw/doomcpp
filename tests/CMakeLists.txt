add_executable(doomcpp_tests
    test_main.cpp
    test_smoke.cpp
)
target_link_libraries(doomcpp_tests PRIVATE doctest)
# doctest.h lives in the fetched source's doctest/ subdirectory.
target_include_directories(doomcpp_tests PRIVATE ${doctest_SOURCE_DIR}/doctest)

include(doctest)
doctest_discover_tests(doomcpp_tests)
