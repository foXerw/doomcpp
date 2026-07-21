add_executable(doomcpp_tests
    test_main.cpp
    test_smoke.cpp
    test_i_system.cpp
    test_m_fixed.cpp
    test_m_random.cpp
    test_wadfile.cpp
    test_p_setup.cpp
    test_r_draw.cpp
    test_r_data.cpp
    test_r_plane.cpp
)
target_link_libraries(doomcpp_tests PRIVATE doctest doomcore)
target_include_directories(doomcpp_tests PRIVATE ${doctest_SOURCE_DIR}/doctest)

include(doctest)
doctest_discover_tests(doomcpp_tests)
