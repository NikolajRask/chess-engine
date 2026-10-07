add_library(chess_engine
  src/types.cpp
  src/bitboard.cpp
  src/position.cpp
  src/movegen.cpp
  src/zobrist.cpp
  src/eval.cpp
  src/search.cpp
  src/engine.cpp
)

target_include_directories(chess_engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(chess_engine PRIVATE -Wall -Wextra -Wpedantic)
