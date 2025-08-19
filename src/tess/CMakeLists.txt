message(STATUS "Using diagnostics: ${MOS_C_DIAGNOSTIC}")

add_library(tess)
set(HEADERS
  ${PROJECT_SOURCE_DIR}/src/tess/include/ast.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/error.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/tess.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/token.h
  ${PROJECT_SOURCE_DIR}/src/tess/include/tokenizer.h
)

set(SOURCES
  src/ast.c
  src/error.c
  src/tess.c
  src/token.c
  src/tokenizer.c
  )
target_sources(tess PRIVATE ${HEADERS} ${SOURCES})
target_compile_options(tess BEFORE PRIVATE ${MOS_C_DIAGNOSTIC})
target_include_directories(tess PUBLIC "${PROJECT_SOURCE_DIR}/src/tess/include")
target_link_libraries(tess PRIVATE PUBLIC mos)
target_link_options(tess PRIVATE ${MOS_C_LINK_OPTIONS})

# -- tests --

function(tess_create_test NAME)
  set(TEST_NAME test-${NAME})
  add_executable(${TEST_NAME} "src/test_${NAME}.c")
  target_link_libraries(${TEST_NAME} PRIVATE tess)
  target_compile_options(${TEST_NAME} PRIVATE ${MOS_C_DIAGNOSTIC} ${MOS_C_EXEC} -Wunused)
  target_link_options(${TEST_NAME} PRIVATE ${MOS_C_LINK_OPTIONS})
  add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:${TEST_NAME}>)
endfunction()

tess_create_test(tess)

# -- doxygen --

set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
set(DOXYGEN_QUIET YES)
doxygen_add_docs(docs_tess ${HEADERS})
add_dependencies(tess docs_tess)
