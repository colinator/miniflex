cmake_minimum_required(VERSION 3.14)
project(miniflex C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(MINIFLEX_BUILD_CONFORMANCE "Build opt-in official FlexBuffers conformance tests" OFF)

enable_testing()

add_library(miniflex SHARED src/miniflex.c)
add_library(miniflex_static STATIC src/miniflex.c)

target_include_directories(miniflex
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(miniflex_static
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(miniflex_static PROPERTIES OUTPUT_NAME miniflex)
set_target_properties(miniflex PROPERTIES EXPORT_NAME miniflex)
set_target_properties(miniflex_static PROPERTIES EXPORT_NAME miniflex_static)

add_library(miniflex::miniflex ALIAS miniflex)
add_library(miniflex::miniflex_static ALIAS miniflex_static)

add_executable(test_miniflex test/test_miniflex.c)
target_link_libraries(test_miniflex miniflex)
add_test(NAME test_miniflex COMMAND test_miniflex)

install(
  TARGETS miniflex miniflex_static
  EXPORT miniflexTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT miniflexTargets
  NAMESPACE miniflex::
  FILE miniflexTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/miniflex
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/miniflexConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/miniflexConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/miniflex
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/miniflexConfigVersion.cmake
  VERSION 0.1.0
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/miniflexConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/miniflexConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/miniflex
)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/test/install_smoke/run_install_smoke.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/run_install_smoke.cmake
  @ONLY
)

add_test(
  NAME test_miniflex_install_smoke
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/run_install_smoke.cmake
)

if(MINIFLEX_BUILD_CONFORMANCE)
  find_program(MINIFLEX_CONFORMANCE_PYTHON NAMES python3 python3.11 REQUIRED)
  execute_process(
    COMMAND ${MINIFLEX_CONFORMANCE_PYTHON} -c "from flatbuffers import flexbuffers"
    RESULT_VARIABLE MINIFLEX_FLATBUFFERS_IMPORT_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(NOT MINIFLEX_FLATBUFFERS_IMPORT_RESULT EQUAL 0)
    message(FATAL_ERROR "MINIFLEX_BUILD_CONFORMANCE requires the official Python flatbuffers package for the selected interpreter")
  endif()

  add_executable(miniflex_conformance_cases test/conformance/miniflex_cases.c)
  set_target_properties(miniflex_conformance_cases PROPERTIES OUTPUT_NAME miniflex_conformance_cases)
  target_link_libraries(miniflex_conformance_cases miniflex)
  target_include_directories(miniflex_conformance_cases PRIVATE include)

  add_test(
    NAME test_miniflex_conformance
    COMMAND ${MINIFLEX_CONFORMANCE_PYTHON}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/conformance/run_conformance.py
            $<TARGET_FILE:miniflex_conformance_cases>
  )
endif()
