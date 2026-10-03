# Targets
# =======

add_custom_target(${IMMER_EXAMPLES_TARGET} COMMENT "Build all examples.")
add_dependencies(${IMMER_CHECK_TARGET} ${IMMER_EXAMPLES_TARGET})

file(GLOB_RECURSE immer_examples "*.cpp")
foreach(_file IN LISTS immer_examples)
  immer_target_name_for(_target _output "${_file}")
  add_executable(${_target} EXCLUDE_FROM_ALL "${_file}")
  add_dependencies(${IMMER_EXAMPLES_TARGET} ${_target})
  set_target_properties(${_target} PROPERTIES OUTPUT_NAME ${_output})
  target_link_libraries(${_target} PUBLIC immer-dev)
  add_test("${IMMER_CTEST_PREFIX}example/${_output}" ${_output})
endforeach()
