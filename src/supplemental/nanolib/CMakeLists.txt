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
  conf.c
  # conf_ver2.c
  env.c
  file.c
  hash_table.c
  mqtt_db.c
  hocon.c
  log.c
  )


nng_test(hash_test)
nng_test(dbtree_test)
nng_test(hocon_test)

if (SUPP_RULE_ENGINE)
  nng_sources(rule.c)
  nng_test(rule_test)
endif (SUPP_RULE_ENGINE)


  
