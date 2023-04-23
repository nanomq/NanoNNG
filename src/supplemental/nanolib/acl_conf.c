#include "nng/supplemental/nanolib/acl_conf.h"
#include "core/nng_impl.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/log.h"

extern char *strtrim_head_tail(char *str, size_t len);

static bool
acl_parse_str_item(cJSON *obj, const char *key, acl_rule_ct *content)
{
	cJSON *item = cJSON_GetObjectItem(obj, key);
	if (cJSON_IsString(item)) {
		char *name = cJSON_GetStringValue(item);
		if (strcmp(name, "#") == 0) {
			content->type = ACL_RULE_ALL;
		} else {
			content->type      = ACL_RULE_SINGLE_STRING;
			content->value.str = nni_strdup(name);
		}
		return true;
	}
	return false;
}

static bool
acl_parse_str_array_item(cJSON *obj, const char *key, acl_rule_ct *content)
{
	cJSON *array = cJSON_GetObjectItem(obj, key);
	if (cJSON_IsArray(array)) {
		int size                 = cJSON_GetArraySize(array);
		content->type            = ACL_RULE_STRING_ARRAY;
		content->count           = size;
		content->value.str_array = nni_zalloc(size * sizeof(char *));
		for (int i = 0; i < size; i++) {
			cJSON *item = cJSON_GetArrayItem(array, i);
			if (cJSON_IsString(item)) {
				content->value.str_array[i] =
				    nni_strdup(cJSON_GetStringValue(item));
			}
		}
		return true;
	}
	return false;
}

bool
acl_parse_json_rule(cJSON *obj, size_t id, acl_rule **rule)
{
	acl_rule *r = nni_zalloc(sizeof(acl_rule));

	r->id          = id;
	r->action      = ACL_ALL;
	r->topic_count = 0;
	r->topics      = NULL;
	r->rule_type   = ACL_NONE;

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
	}

	if (acl_parse_str_item(obj, "ipaddr", &r->rule_ct.ct)) {
		r->rule_type = ACL_IPADDR;
		goto out;
	}

	if (acl_parse_str_array_item(obj, "ipaddrs", &r->rule_ct.ct)) {
		r->rule_type = ACL_IPADDR;
		goto out;
	}

	if (acl_parse_str_item(obj, "username", &r->rule_ct.ct)) {
		r->rule_type = ACL_USERNAME;
		goto out;
	}

	if (acl_parse_str_item(obj, "clientid", &r->rule_ct.ct)) {
		r->rule_type = ACL_CLIENTID;
		goto out;
	}

	cJSON *op = cJSON_GetObjectItem(obj, "and");
	if (cJSON_IsArray(op)) {
		r->rule_type = ACL_AND;
	} else {
		op = cJSON_GetObjectItem(obj, "or");
		if (cJSON_IsArray(op)) {
			r->rule_type = ACL_OR;
		}
	}

	if (r->rule_type == ACL_OR || r->rule_type == ACL_AND) {
		int                  size      = cJSON_GetArraySize(op);
		acl_sub_rules_array *rule_list = &r->rule_ct.array;
		rule_list->rules = nni_zalloc(sizeof(acl_sub_rule *) * size);
		rule_list->count = 0;
		for (int i = 0; i < size; i++) {
			cJSON *       sub_item = cJSON_GetArrayItem(op, i);
			acl_sub_rule *sub_rule =
			    nni_zalloc(sizeof(acl_sub_rule));

			if (acl_parse_str_item(
			        sub_item, "clientid", &sub_rule->rule_ct)) {
				sub_rule->rule_type = ACL_CLIENTID;
			} else if (acl_parse_str_item(sub_item, "username",
			               &sub_rule->rule_ct)) {
				sub_rule->rule_type = ACL_USERNAME;
			} else if (acl_parse_str_item(sub_item, "ipaddr",
			               &sub_rule->rule_ct) ||
			    acl_parse_str_array_item(
			        sub_item, "ipaddrs", &sub_rule->rule_ct)) {
				sub_rule->rule_type = ACL_IPADDR;
			} else {
				nni_free(
				    sub_rule, sizeof(acl_sub_rule *) * size);
				continue;
			}
			rule_list->rules[rule_list->count] = sub_rule;
			rule_list->count++;
		}
		goto out;
	}

out:
	*rule = r;
	return true;

err:
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
			acl_rule *rule;
			cJSON *   obj = cJSON_Parse(value);

			if (cJSON_IsObject(obj)) {
				if (acl_parse_json_rule(obj, id, &rule)) {
					acl->rule_count++;
					acl->rules = realloc(acl->rules,
					    acl->rule_count *
					        sizeof(acl_rule));
					acl->rules[acl->rule_count - 1] = rule;
				}
				cJSON_Delete(obj);
			} else {
				log_warn("invalid json string: %s", value);
			}
		}

		if (line) {
			free(line);
			line = NULL;
		}
		if (str) {
			free(str);
			str = NULL;
		}
		if (value) {
			free(value);
			value = NULL;
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
	acl->enable     = false;
	acl->rule_count = 0;
	acl->rules      = NULL;
}

static void
free_acl_content(acl_rule_ct *ct)
{
	switch (ct->type) {
	case ACL_RULE_SINGLE_STRING:
		nni_strfree(ct->value.str);
		break;

	case ACL_RULE_STRING_ARRAY:
		for (size_t j = 0; j < ct->count; j++) {
			nni_strfree(ct->value.str_array[j]);
		}
		nni_free(ct->value.str_array, ct->count * sizeof(char *));
		break;
	default:
		break;
	}
}

void
conf_acl_destroy(conf_acl *acl)
{
	for (size_t i = 0; i < acl->rule_count; i++) {
		acl_rule *rule = acl->rules[i];

		if (rule->rule_type != ACL_AND && rule->rule_type != ACL_OR) {
			free_acl_content(&rule->rule_ct.ct);
		} else {
			acl_sub_rules_array *array = &rule->rule_ct.array;
			for (size_t j = 0; j < array->count; j++) {
				acl_sub_rule *sub_rule = array->rules[j];
				free_acl_content(&sub_rule->rule_ct);
				nni_free(sub_rule, sizeof(acl_sub_rule));
			}
			nni_free(array->rules,
			    sizeof(acl_sub_rule *) * array->count);
		}
		for (size_t k = 0; k < rule->topic_count; k++) {
			nni_strfree(rule->topics[k]);
		}
		nni_free(rule->topics, rule->topic_count * sizeof(char *));
		nni_free(rule, sizeof(acl_rule));
	}

	if (acl->rule_count > 0) {
		nni_free(acl->rules, acl->rule_count * sizeof(acl_rule *));
		acl->rule_count = 0;
	}
}

void
print_acl_conf(conf_acl *acl)
{
	if (acl->enable) {
		for (size_t i = 0; i < acl->rule_count; i++) {
			acl_rule *rule = acl->rules[i];
			log_info("[%zu] permit: %s", rule->id,
			    rule->permit == ACL_ALLOW ? "allow" : "deny");

			log_info("[%zu] action: %s", rule->id,
			    rule->action == ACL_SUB       ? "subscribe"
			        : rule->action == ACL_PUB ? "publish"
			                                  : "pubsub");

			log_info("[%zu] rule_type: '%s'", rule->id,
			    rule->rule_type == ACL_CLIENTID       ? "clientid"
			        : rule->rule_type == ACL_USERNAME ? "username"
			        : rule->rule_type == ACL_IPADDR   ? "ipaddr"
			        : rule->rule_type == ACL_AND      ? "and"
			        : rule->rule_type == ACL_OR       ? "or"
			                                          : "(none)");

			log_info("[%zu] rule_content: ", rule->id);
			if (rule->rule_type != ACL_AND &&
			    rule->rule_type != ACL_OR) {
				switch (rule->rule_ct.ct.type) {
				case ACL_RULE_SINGLE_STRING:
					log_info("[%zu] \t%s", rule->id,
					    rule->rule_ct.ct.value.str);
					break;

				case ACL_RULE_STRING_ARRAY:
					for (size_t j = 0;
					     j < rule->rule_ct.ct.count; j++) {
						log_info("[%zu] \t%s",
						    rule->id,
						    rule->rule_ct.ct.value
						        .str_array[j]);
					}
					break;
				case ACL_RULE_ALL:
					log_info("[%zu] \tall", rule->id);
					break;
				default:
					break;
				}
			} else {
				acl_sub_rules_array *array =
				    &rule->rule_ct.array;
				for (size_t j = 0; j < array->count; j++) {
					acl_sub_rule *sub_rule =
					    array->rules[j];
					log_info("sub_rule type: [%d]",
					    sub_rule->rule_type);

					log_info(
					    "[%zu][%zu] sub_rule_type: '%s'",
					    rule->id, j,
					    sub_rule->rule_type == ACL_CLIENTID
					        ? "clientid"
					        : sub_rule->rule_type ==
					            ACL_USERNAME
					        ? "username"
					        : sub_rule->rule_type ==
					            ACL_IPADDR
					        ? "ipaddr"
					        : sub_rule->rule_type ==
					            ACL_AND
					        ? "and"
					        : sub_rule->rule_type == ACL_OR
					        ? "or"
					        : "(none)");

					switch (sub_rule->rule_ct.type) {
					case ACL_RULE_SINGLE_STRING:
						log_info("[%zu][%zu] \t%s",
						    rule->id, j,
						    sub_rule->rule_ct.value
						        .str);
						break;

					case ACL_RULE_STRING_ARRAY:
						for (size_t k = 0; k <
						     sub_rule->rule_ct.count;
						     k++) {
							log_info("[%zu][%zu][%"
							         "zu] \t%s",
							    rule->id, j, k,
							    sub_rule->rule_ct
							        .value
							        .str_array[k]);
						}
						break;

					case ACL_RULE_ALL:
						log_info(
						    "[%zu] \tall", rule->id);
						break;
					default:
						break;
					}
				}
			}

			log_info("[%zu] topics:", rule->id);
			for (size_t k = 0; k < rule->topic_count; k++) {
				log_info(
				    "[%zu] \t%s", rule->id, rule->topics[k]);
			}
			log_info("");
		}
	}
}