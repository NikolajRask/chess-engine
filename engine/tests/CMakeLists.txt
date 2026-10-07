include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(engine_tests perft_test.cpp eval_test.cpp features_test.cpp)
target_link_libraries(engine_tests PRIVATE chess_engine GTest::gtest_main)
target_compile_options(engine_tests PRIVATE -Wall -Wextra)

include(GoogleTest)
gtest_discover_tests(engine_tests)
