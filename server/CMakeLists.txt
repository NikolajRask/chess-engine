include(FetchContent)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.3
)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

add_executable(chess-server main.cpp)
target_link_libraries(chess-server PRIVATE chess_engine httplib::httplib nlohmann_json::nlohmann_json)
target_compile_options(chess-server PRIVATE -Wall -Wextra -Wpedantic)
