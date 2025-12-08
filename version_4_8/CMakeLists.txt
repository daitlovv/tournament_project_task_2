cmake_minimum_required(VERSION 3.10)
project(Tournament_Mutex)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra -O2 -D_DEFAULT_SOURCE -pthread")

add_executable(tournament tournament.c)
target_link_libraries(tournament pthread)