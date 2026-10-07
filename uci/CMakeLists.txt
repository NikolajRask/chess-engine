add_executable(chess-uci main.cpp)
target_link_libraries(chess-uci PRIVATE chess_engine)
target_compile_options(chess-uci PRIVATE -Wall -Wextra -Wpedantic)
