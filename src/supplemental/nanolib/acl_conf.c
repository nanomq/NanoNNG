#include "nng/supplemental/nanolib/acl_conf.h"
#include "core/nng_impl.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/log.h"

extern char *strtrim_head_tail(char *str, size_t len);

static bool
parse_json_rule(char *json, size_t id, acl_rule **rule)
{
	cJSON *obj = cJSON_Parse(json);
	if (obj == NULL) {
		return false;
	}

	acl_rule *r = nni_zalloc(sizeof(acl_rule));

	r->id          = id;
	r->action      = ACL_ALL;
	r->topic_count = 0;
	r->topics      = NULL;

	char *value;

	cJSON *permit = cJSON_GetObjectItem(obj, "permit");
	if (cJSON_IsString(permit)) {
		value = cJSON_GetStringValue(permit);
		if (strcmp(value, "allow") == 0) {
			r->permit = ACL_ALLOW;
		} else if (strcmp(value, "deny") == 0) {
			r->permit = ACL_DENY;
		} else {
			goto err;
		}
	} else {
		goto err;
	}

	cJSON *action = cJSON_GetObjectItem(obj, "action");
	if (cJSON_IsString(action)) {
		value = cJSON_GetStringValue(action);
		if (strcmp(value, "pubsub") == 0) {
			r->action = ACL_ALL;
		} else if (strcmp(value, "subscribe") == 0) {
			r->action = ACL_SUB;
		} else if (strcmp(value, "publish") == 0) {
			r->action = ACL_PUB;
		} else {
			goto err;
		}

		*rule = r;
		return true;
	}

	cJSON *topics = cJSON_GetObjectItem(obj, "topics");
	if (cJSON_IsArray(topics)) {
		int size       = cJSON_GetArraySize(topics);
		r->topic_count = size;
		r->topics      = nni_zalloc(size * sizeof(char *));
		for (int i = 0; i < size; i++) {
			cJSON *item = cJSON_GetArrayItem(topics, i);
			if (cJSON_IsString(item)) {
				r->topics[i] =
				    nni_strdup(cJSON_GetStringValue(item));
			}
		}
		goto out;
	}

	cJSON *ipaddr = cJSON_GetObjectItem(obj, "ipaddr");
	if (cJSON_IsString(ipaddr)) {
		r->rule_type    = ACL_IPADDR;
		r->content.type = ACL_RULE_SINGLE_STRING;
		r->content.value.str =
		    nni_strdup(cJSON_GetStringValue(ipaddr));
		goto out;
	}

	cJSON *ipaddrs = cJSON_GetObjectItem(obj, "ipaddrs");
	if (cJSON_IsArray(ipaddrs)) {
		int size                   = cJSON_GetArraySize(ipaddrs);
		r->rule_type               = ACL_IPADDR;
		r->content.type            = ACL_RULE_STRING_ARRAY;
		r->content.count           = size;
		r->content.value.str_array = nni_zalloc(size * sizeof(char *));
		for (int i = 0; i < size; i++) {
			cJSON *item = cJSON_GetArrayItem(ipaddrs, i);
			if (cJSON_IsString(item)) {
				r->content.value.str_array[i] =
				    nni_strdup(cJSON_GetStringValue(item));
			}
		}
		goto out;
	}

	cJSON *user = cJSON_GetObjectItem(obj, "user");
	if (cJSON_IsString(user)) {
		r->rule_type = ACL_USERNAME;
		char *name   = cJSON_GetStringValue(user);
		if (strcmp(name, "#") == 0) {
			r->content.type = ACL_RULE_ALL;
		} else {
			r->content.type      = ACL_RULE_SINGLE_STRING;
			r->content.value.str = nni_strdup(name);
		}

		goto out;
	}

	cJSON *clientid = cJSON_GetObjectItem(obj, "clientid");
	if (cJSON_IsString(clientid)) {
		r->rule_type = ACL_CLIENTID;
		char *name   = cJSON_GetStringValue(clientid);
		if (strcmp(name, "#") == 0) {
			r->content.type = ACL_RULE_ALL;
		} else {
			r->content.type      = ACL_RULE_SINGLE_STRING;
			r->content.value.str = nni_strdup(name);
		}
		goto out;
	}

	cJSON *and_op = cJSON_GetObjectItem(obj, "and");
	if (cJSON_IsArray(and_op)) {
		int size                   = cJSON_GetArraySize(and_op);
		r->rule_type               = ACL_AND;
		r->content.type            = ACL_RULE_INT_ARRAY;
		r->content.count           = size;
		r->content.value.int_array = nni_zalloc(size * sizeof(int));
		for (int i = 0; i < size; i++) {
			cJSON *item = cJSON_GetArrayItem(and_op, i);
			if (cJSON_IsNumber(item)) {
				r->content.value.int_array[i] =
				    cJSON_GetNumberValue(item);
			}
		}
		goto out;
	}

	cJSON *or_op = cJSON_GetObjectItem(obj, "or");
	if (cJSON_IsArray(or_op)) {
		int size                   = cJSON_GetArraySize(or_op);
		r->rule_type               = ACL_OR;
		r->content.type            = ACL_RULE_INT_ARRAY;
		r->content.count           = size;
		r->content.value.int_array = nni_zalloc(size * sizeof(int));
		for (int i = 0; i < size; i++) {
			cJSON *item = cJSON_GetArrayItem(or_op, i);
			if (cJSON_IsNumber(item)) {
				r->content.value.int_array[i] =
				    cJSON_GetNumberValue(item);
			}
		}
		goto out;
	}

out:
	cJSON_Delete(obj);
	*rule = r;
	return true;

err:
	cJSON_Delete(obj);
	nni_free(r, sizeof(acl_rule));
	return false;
}

void
conf_acl_parse(conf_acl *acl, const char *path)
{
	char * line = NULL;
	size_t sz   = 0;
	FILE * fp;

	if ((fp = fopen(path, "r")) == NULL) {
		log_error("File %s open failed", path);
		return;
	}

	acl->rule_count = 0;

	while (nano_getline(&line, &sz, fp) != -1) {
		if (sz < 10) {
			continue;
		}
		size_t id    = 0;
		char * value = calloc(1, sz - 10);
		char * str   = strtrim_head_tail(line, sz);
		int    res   = sscanf(str, "acl.rule.%zu=%[^\n]", &id, value);

		if (res == 2) {
			acl->rule_count++;
			acl_rule *rule;

			if (parse_json_rule(value, id, &rule)) {
				acl->rules = realloc(acl->rules,
				    acl->rule_count * sizeof(acl_rule));
				acl->rules[acl->rule_count - 1] = rule;
			}
		}

		if (line) {
			free(line);
			line = NULL;
		}
	}

	if (line) {
		free(line);
	}

	fclose(fp);
}

void
conf_acl_init(conf_acl *acl)
{
	acl->rule_count = 0;
	acl->rules      = NULL;
}

void
conf_acl_destroy(conf_acl *acl)
{
	for (size_t i = 0; i < acl->rule_count; i++) {
		acl_rule *   rule = acl->rules[i];
		acl_rule_ct *ct   = &rule->content;
		switch (ct->type) {
		case ACL_RULE_SINGLE_STRING:
			nni_strfree(ct->value.str);
			break;

		case ACL_RULE_INT_ARRAY:
			nni_free(ct->value.int_array, ct->count * sizeof(int));
			break;

		case ACL_RULE_STRING_ARRAY:
			for (size_t i = 0; i < ct->count; i++) {
				nni_strfree(ct->value.str_array[i]);
			}
			nni_free(
			    ct->value.str_array, ct->count * sizeof(char *));
			break;

		default:
			break;
		}
		nni_free(rule, sizeof(acl_rule));
	}
	nni_free(acl->rules, acl->rule_count * sizeof(acl_rule *));
	acl->rule_count = 0;
}