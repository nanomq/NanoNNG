#
# Copyright 2021 Staysail Systems, Inc. <info@staystail.tech>
#
# This software is supplied under the terms of the MIT License, a copy of which
# should be located in the distribution where this file was obtained
# (LICENSE.txt).  A copy of the license may also be found online at
# https://opensource.org/licenses/MIT.
#

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
  scanner.c
  parser.c
  hocon.c
  log.c
  utils.c
  md5.c
  topics.c
  )


nng_test(hash_test)
nng_test(dbtree_test)
nng_test(cmd_test)
nng_test(conf_test)
nng_test(env_test)
nng_test(rule_test)
nng_test(lib_base64_test)
nng_test(topics_test)

if (SUPP_RULE_ENGINE)
  nng_sources(rule.c)
  nng_test(rule_test)
endif (SUPP_RULE_ENGINE)

add_subdirectory(linkedlist)
add_subdirectory(ringbuffer)
add_subdirectory(parquet)
add_subdirectory(blf)

