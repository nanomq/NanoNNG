#ifndef NANOLIB_ACL_CONF_H
#define NANOLIB_ACL_CONF_H

#include "nng/nng.h"

typedef enum {
	ALLOW,
	DENY,
} acl_permit;

typedef enum {
	ACL_USERNAME,
	ACL_CLIENTID,
	ACL_IPADDR,
} acl_rule_type;

typedef struct {
	acl_rule_type type;
	bool          regex;
	char *        value;
} acl_rule;

typedef struct {
	size_t     rule_count;
	acl_rule **rules;
	enum { AND, OR } rules_logic;
} acl_rule_set;

typedef struct {
	size_t topic_count;
	char * topics;
} acl_topic_set;

typedef struct {
	acl_permit   permit;
	acl_rule_set rule_set;
	enum { PUB, SUB, ALL } act_req;
	acl_topic_set topic_set;
} acl_action;

typedef struct {
	size_t       action_count;
	acl_action **actions;
} conf_acl;

#endif /* NANOLIB_ACL_CONF_H */
