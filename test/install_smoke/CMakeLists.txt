cmake_minimum_required(VERSION 3.14)
project(miniflex_install_smoke C)

find_package(miniflex REQUIRED CONFIG)

add_executable(miniflex_install_smoke main.c)
target_link_libraries(miniflex_install_smoke PRIVATE miniflex::miniflex)
