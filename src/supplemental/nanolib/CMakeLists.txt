#
# Copyright 2021 Staysail Systems, Inc. <info@staystail.tech>
#
# This software is supplied under the terms of the MIT License, a copy of which
# should be located in the distribution where this file was obtained
# (LICENSE.txt).  A copy of the license may also be found online at
# https://opensource.org/licenses/MIT.
#


set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PARSER_DIR "${CMAKE_CURRENT_BINARY_DIR}")

find_package(FLEX 2.6 REQUIRED)
find_package(BISON 3.0 REQUIRED)

set(LEXER_OUT "${SRC_DIR}/scanner.c")
set(PARSER_OUT "${SRC_DIR}/parser.c")

bison_target(PARSER "parser.y" "${PARSER_OUT}" DEFINES_FILE "${SRC_DIR}/parser.h")
flex_target(LEXER "scanner.l" "${LEXER_OUT}" DEFINES_FILE "${SRC_DIR}/scanner.h")
add_flex_bison_dependency(LEXER PARSER)

add_library(hocon STATIC "${LEXER_OUT}" "${PARSER_OUT}" "cJSON.c" "hocon.c")
target_include_directories(hocon PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(nng PRIVATE ${PARSER_DIR}/libhocon.a)
target_link_libraries(nng_testing PRIVATE ${PARSER_DIR}/libhocon.a)



nng_sources(
  base64.c
  cJSON.c
  cmd.c
  rule.c
  acl_conf.c
  conf.c
  conf_ver2.c
  env.c
  file.c
  hash_table.c
  mqtt_db.c
  hocon.c
  log.c
  )


nng_test(hash_test)
nng_test(dbtree_test)

if (SUPP_RULE_ENGINE)
  nng_sources(rule.c)
  nng_test(rule_test)
endif (SUPP_RULE_ENGINE)


  
