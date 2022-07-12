#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/nng.h"
#include "nng/nng_debug.h"
#include "core/nng_impl.h"


static char *rule_engine_key_arr[] = {
	"qos",
	"id",
	"topic",
	"clientid",
	"username",
	"password",
	"timestamp",
	"payload",
	"*",
	NULL,
};

int
rule_find_key(const char *str, size_t len)
{
	int i = 0;
	while (rule_engine_key_arr[i]) {
		if (strlen(rule_engine_key_arr[i]) == len) {
			if (!strncmp(rule_engine_key_arr[i], str, len)) {
				return i;
			}
		}
		i++;
	}
	return -1;
}

static int
find_as(char *str, int len, rule *info)
{
	int    i  = 0;
	char **as = info->as;
	for (; i < 8; i++) {
		if (as[i] == NULL)
			continue;
		if (strlen(as[i]) != (size_t) len)
			continue;
		if (!strncmp(as[i], str, len)) {
			return i;
		}
	}
	return -1;
}

char *
rule_get_key_arr(char *p, rule_key *key)
{
	bool is_recur = false;
	p++;
	char *p_b = p;
	while (*p != '\0' && *p != ' ' && *p != '.')
		p++;

	if (*p == '.') {
		is_recur = true;
	}

	*p            = '\0';
	char *key_str = nng_strdup(p_b);
	cvector_push_back(key->key_arr, key_str);
	if (is_recur) {
		p = rule_get_key_arr(p, key);
	}
	return p;
}

// Recursive get json payload key.
static char *
get_payload_key_arr(char *p, rule_payload *payload)
{
	bool is_recur = false;
	p++;
	char *p_b = p;
	while (*p != '\0' && *p != ' ' && *p != '.')
		p++;

	if (*p == '.') {
		is_recur = true;
	}

	*p        = '\0';
	char *key = nng_strdup(p_b);
	cvector_push_back(payload->psa, key);
	if (is_recur) {
		p = get_payload_key_arr(p, payload);
	}
	return p;
}

// Get payload field as string.
static int
get_payload_as(char *p, rule_payload *payload)
{
	if (*p == '\0') {
		return -1;
	}

	if (!strncmp("as", p, strlen("as"))) {
		p += strlen("as");
		while (*p == ' ' && *p != '\0')
			p++;
		char *p_b = p;
		if (*p_b != '\0') {
			payload->pas = nng_strdup(p_b);
		}
		return 0;
	}

	return -1;
}

static rule_payload *
rule_payload_new(void)
{
	rule_payload *payload = (rule_payload *) nni_zalloc(sizeof(rule_payload));

	payload->psa      = NULL;
	payload->pas      = NULL;
	payload->filter   = NULL;
	payload->value    = NULL;
	payload->type     = 0;
	payload->is_store = false;
	return payload;
}

// Parse payload subfield, mainly for get payload json
// subfield key array and as string. Return 0 if p is
// payload with subfield, or return -1 so parse it with
// other step.
static int
parse_payload_subfield(char *p, rule *info, bool is_store)
{
	char *p_b     = p;
	int   key_len = strlen("payload");
	int   p_len   = strlen(p);
	if (p_len <= key_len || p[key_len] != '.')
		return -1;
	if (strncmp("payload", p, key_len))
		return -1;

	p += key_len;
	rule_payload *payload = rule_payload_new();
	payload->is_store     = is_store;
	cvector_push_back(info->payload, payload);
	p = get_payload_key_arr(p, payload);
	if (p - p_b < p_len)
		p++;
	while (*p == ' ' && *p != '\0')
		p++;
	if (-1 == get_payload_as(p, payload)) {
		p        = p_b;
		int size = cvector_size(payload->psa);
		while (size - 1) {
			if (*p == '\0') {
				*p = '.';
				size--;
			}
			p++;
		}
		payload->pas = nng_strdup(p_b);
	}
	return 0;
}

// Set info parse from select.
static int
set_select_info(char *p_b, rule *info)
{
	int   key_len = 0;
	int   rc      = 0;
	char *p       = p_b;

	if (0 == parse_payload_subfield(p_b, info, true)) {
		info->flag[RULE_PAYLOAD_FIELD] = 1;
		goto finish;
	}

	while (*p != '\0' && *p != ' ')
		p++;

	if (-1 != (rc = rule_find_key(p_b, p - p_b))) {
		if (rc == 8) {
			// if find '*', set all field is true
			memset(info->flag, 1, rc);
		} else {
			info->flag[rc] = 1;
		}

		p_b = p;
		while (p_b[key_len] == ' ' && p_b[key_len] != '\0')
			key_len++;
		if (p_b[key_len] != '\0') {
			p_b += key_len;
			if (!strncmp("as", p_b, strlen("as"))) {
				p_b += strlen("as");
				while (*p_b == ' ' && *p_b != '\0')
					p_b++;
				if (*p_b != '\0') {
					info->as[rc] = nng_strdup(p_b);
				}
			}
		}
	} else {
		return -1;
	}


finish:
	return 0;
}

static int
parse_select(const char *select, rule *info)
{
	char *p       = (char *) select;
	char *p_b     = (char *) select;
	info->payload = NULL;

	while ((p = strchr(p, ','))) {
		*p = '\0';
		while (*p_b == ' ' && *p_b != '\0')
			p_b++;
		if (-1 == set_select_info(p_b, info)) {
			debug_msg("Invalid sql field");
			return -1;
		}
		p++;

		while (*p == ' ' && *p != '\0')
			p++;
		p_b = p;
	}

	if (-1 == set_select_info(p_b, info)) {
		debug_msg("Invalid sql field");
		return -1;
	}
	return 0;
}

static int
parse_from(char *from, rule *info)
{
	while (*from != '\0' && *from == ' ')
		from++;
	if (from[0] == '\"') {
		from++;
		char *p = from;
		while (*p != '\"' && *p != '\0')
			p++;
		*p = '\0';
	}
	info->topic = nng_strdup(from);
	return 0;
}

static int
find_payload_as(char *str, int len, rule_payload **payloads)
{
	for (size_t i = 0; i < cvector_size(payloads); i++) {
		if (payloads[i]->pas == NULL)
			continue;
		if (strlen(payloads[i]->pas) != (size_t) len)
			continue;
		if (!strncmp(payloads[i]->pas, str, len)) {
			return i;
		}
	}
	return -1;
}

static int
set_payload_filter(char *str, rule_payload *payload)
{
	payload->filter = nng_strdup(str);
    return 0;
}

static char *
pick_value(char *p)
{
	while (' ' == *p || '=' == *p || '!' == *p || '<' == *p || '>' == *p) {
		p++;
	}

	char *str = NULL;

	if (*p == '\'') {
		p++;
		str = p;
		while (*p != '\'' && *p != '\0')
			p++;
		*p = '\0';
	} else {
		str = p;
		while (*p != ' ' && *p != '\0')
			p++;
		*p = '\0';
	}

	return str;
}

static rule_cmp_type
get_rule_cmp_type(char **str)
{
	char *        p        = *str;
	rule_cmp_type cmp_type = RULE_CMP_NONE;
	while (' ' != *p) {
		switch (*p) {
		case '=':
			cmp_type = RULE_CMP_EQUAL;
			break;
		case '!':
			if ('=' == *(p + 1)) {
				cmp_type = RULE_CMP_UNEQUAL;
			}
			break;
		case '>':
			if ('=' == *(p + 1)) {
				cmp_type = RULE_CMP_GREATER_AND_EQUAL;
			} else {
				cmp_type = RULE_CMP_GREATER;
			}
			break;
		case '<':
			if ('>' == *(p + 1)) {
				cmp_type = RULE_CMP_UNEQUAL;
			} else if ('=' == *(p + 1)) {
				cmp_type = RULE_CMP_LESS_AND_EQUAL;
			} else {
				cmp_type = RULE_CMP_LESS;
			}
			break;
		default:
			break;
		}
		if (RULE_CMP_NONE != cmp_type) {
			break;
		}
		p++;
	}

	*str = p;

	return cmp_type;
}

static int
set_where_info(char *str, size_t len, rule *info)
{
	char *p  = str;
	int   rc = 0;
    NNI_ARG_UNUSED(len);

	rule_cmp_type cmp_type = get_rule_cmp_type(&p);

	int key_len = p - str;
	if (RULE_CMP_NONE == cmp_type) {
		p++;
		cmp_type = get_rule_cmp_type(&p);
	}

	if (-1 == (rc = rule_find_key(str, key_len))) {
		if (-1 == (rc = find_as(str, key_len, info))) {
			if (-1 !=
			    (rc = find_payload_as(
			         str, key_len, info->payload))) {
				set_payload_filter(
				    pick_value(p), info->payload[rc]);
				info->payload[rc]->cmp_type = cmp_type;
			} else {
				*p = '\0';
				if (-1 !=
				    parse_payload_subfield(str, info, false)) {
					int size = cvector_size(info->payload);
					set_payload_filter(pick_value(++p),
					    info->payload[size - 1]);
					info->payload[size - 1]->cmp_type =
					    cmp_type;
				} else {
					debug_msg("Invalid field");
					return -1;
				}
			}
			return 0;
		}
	}

	info->filter[rc]   = nng_strdup(pick_value(p));
	info->cmp_type[rc] = cmp_type;

	return 0;
}

static int
parse_where(char *where, rule *info)
{
	char *p   = where;
	char *p_b = where;

	info->filter = (char **) nni_zalloc(sizeof(char *) * 8);
	memset(info->filter, 0, 8 * sizeof(char *));

	while ((p = strstr(p, "and"))) {
		if (-1 == set_where_info(p_b, p - p_b, info)) {
			return -1;
		}
		p += 3;
		while (*p == ' ')
			p++;
		p_b = p;
	}
	if (-1 == set_where_info(p_b, strlen(p_b), info)) {
		return -1;
	}
	return 0;
}

bool
rule_sql_parse(conf_rule *cr, char *sql)
{
	if (NULL == sql) {
		debug_msg("Sql is NULL!");
		return false;
	}

	char *srt = strstr(sql, "SELECT");
	if (NULL != srt) {
		int   len_srt, len_mid, len_end;
		char *mid = strstr(srt, "FROM");
		char *end = strstr(mid, "WHERE");

		rule re;
		memset(&re, 0, sizeof(re));

		// function select parser.
		len_srt = mid - srt;
		srt += strlen("SELECT ");
		len_srt -= strlen("SELECT ");
		char select[len_srt];
		memcpy(select, srt, len_srt);
		select[len_srt - 1] = '\0';
		if (-1 == parse_select(select, &re)) {
			return false;
		}

		// function from parser
		if (mid != NULL && end != NULL) {
			len_mid = end - mid;
		} else {
			char *p = mid;
			while (*p != '\n' && *p != '\0')
				p++;
			len_mid = p - mid + 1;
		}

		mid += strlen("FROM ");
		len_mid -= strlen("FROM ");

		char from[len_mid];
		memcpy(from, mid, len_mid);
		from[len_mid - 1] = '\0';
		parse_from(from, &re);

		// function where parser
		if (end != NULL) {
			char *p = end;
			while (*p != '\n' && *p != '\0')
				p++;
			len_end = p - end + 1;
			end += strlen("WHERE ");
			len_end -= strlen("WHERE ");

			char where[len_end];
			memcpy(where, end, len_end);
			where[len_end - 1] = '\0';
			if (-1 == parse_where(where, &re)) {
				
				return false;
			}
		}

		cvector_push_back(cr->rules, re);
	}

	return true;
}