message(STATUS "Using diagnostics: ${MOS_C_DIAGNOSTIC}")

add_library(mos)
set(HEADERS
  ${PROJECT_SOURCE_DIR}/src/mos/include/alloc.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/alloc_internal.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/dbg.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/file.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/hash.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/hashmap.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/hashmap_internal.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/mos_string.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/nodiscard.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/sexp.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/util.h
  ${PROJECT_SOURCE_DIR}/src/mos/include/vector.h
)

set(SOURCES
  src/alloc.c
  src/dbg.c
  src/hash.c
  src/file.c
  src/hashmap.c
  src/mos_string.c
  src/sexp.c
  src/sexp_parser.c
  src/vector.c
  )
target_sources(mos PRIVATE ${HEADERS} ${SOURCES})
target_compile_options(mos BEFORE PRIVATE ${MOS_C_DIAGNOSTIC})
target_include_directories(mos PUBLIC "${PROJECT_SOURCE_DIR}/src/mos/include")
# target_link_libraries(mos PRIVATE zlibstatic PUBLIC)
target_link_options(mos PRIVATE ${MOS_C_LINK_OPTIONS})

# -- embed program --

add_executable(mos_embed src/embed.c)
target_compile_options(mos_embed BEFORE PRIVATE ${MOS_C_DIAGNOSTIC})
target_include_directories(mos_embed PUBLIC "${PROJECT_SOURCE_DIR}/src/mos/include")
target_link_options(mos_embed PRIVATE ${MOS_C_LINK_OPTIONS})

# -- tests --

function(mos_create_test NAME)
  set(TEST_NAME test-${NAME})
  add_executable(${TEST_NAME} "src/test_${NAME}.c")
  target_link_libraries(${TEST_NAME} PRIVATE mos)
  target_compile_options(${TEST_NAME} PRIVATE ${MOS_C_DIAGNOSTIC} ${MOS_C_EXEC} -Wunused)
  target_link_options(${TEST_NAME} PRIVATE ${MOS_C_LINK_OPTIONS})
  add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:${TEST_NAME}>)
endfunction()

mos_create_test(alloc)
mos_create_test(hash)
mos_create_test(map)
mos_create_test(sexp)
mos_create_test(string)
mos_create_test(types)
mos_create_test(vector)

# -- doxygen --

set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
set(DOXYGEN_QUIET YES)
doxygen_add_docs(docs_mos ${HEADERS})
add_dependencies(mos docs_mos)
