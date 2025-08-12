#include "nng/nng.h"
#include "nng/supplemental/nanolib/topics.h"
#include "nng/supplemental/nanolib/log.h"

#define SET_LEVEL(s, level)                                        \
	do {                                                       \
		s->local_skip_level = s->local_save_level = level; \
	} while (0)

static int
count_slashes(const char *str, size_t len)
{
	int count = 0;
	for (size_t i = 0; i < len; ++i) {
		if (str[i] == '/')
			++count;
	}
	return count;
}

// is_sub true for downward msg (produce local topic)
// is_sub false for upward msg (produce remote topic)
void
preprocess_topics(topics *s, bool is_sub)
{
	char    *topic, *ori_topic;
	uint32_t topic_len, ori_topic_len;

	topic = (is_sub == true) ? s->local_topic : s->remote_topic;
	topic_len = (is_sub == true) ? s->local_topic_len : s->remote_topic_len;
	ori_topic = (is_sub == true) ? s->remote_topic : s->local_topic;
	ori_topic_len = (is_sub == true) ? s->remote_topic_len : s->local_topic_len;

	const char *current      = topic;
	const char *end          = topic + topic_len;
	bool        has_wildcard = false;
	SET_LEVEL(s, LOCAL_TOPIC_DEFAULT_LEVEL);

	while ((current = strpbrk(current, "+#")) != NULL) {
		has_wildcard = true;
		if (*current == '#') {
			if (current != end - 1) {
				SET_LEVEL(s, LOCAL_TOPIC_INVALID_LEVEL);
				log_error("# must be last element");
				return;
			}
		}
		current++;
	}

	current = ori_topic;
	end     = ori_topic + ori_topic_len;
	while ((current = strpbrk(current, "#")) != NULL) {
		if (*current == '#') {
			if (current != end - 1) {
				SET_LEVEL(s, LOCAL_TOPIC_INVALID_LEVEL);
				log_error("# must be last element");
				return;
			}
		}
		current++;
	}

	char *substr_pos = strstr(ori_topic, topic);
	if (has_wildcard) {
		if (!substr_pos) {
			SET_LEVEL(s, LOCAL_TOPIC_INVALID_LEVEL);
			log_error("local_topic has wildcard but it's not "
			          "substring of remote topic");
			return;
		}

		size_t prefix_length = substr_pos - ori_topic;
		s->local_skip_level =
		    count_slashes(ori_topic, prefix_length);

		size_t remaining_len = strlen(substr_pos);
		if (remaining_len == topic_len) {
			s->local_save_level = -1;
		} else if (remaining_len > topic_len) {
			s->local_save_level = 1 +
			    count_slashes(topic, topic_len);
		}
	}
}

// is_sub takes same effect like preprocess_topics()
char *
generate_repub_topic(const topics *s, char *topic, bool is_sub)
{
	const char *current_pos = topic;
	for (int i = 0; i < s->local_skip_level && current_pos; ++i) {
		current_pos = strchr(current_pos, '/');
		if (current_pos)
			++current_pos;
	}

	char *new_topic = NULL;
	const char *end_pos = current_pos;

	switch (s->local_save_level) {
	case LOCAL_TOPIC_INFINITE_LEVEL:
		new_topic = nng_strdup(current_pos ? current_pos : "");
		break;
	case LOCAL_TOPIC_DEFAULT_LEVEL:
		new_topic = (is_sub == true) ? nng_strdup(s->local_topic)
		                             : nng_strdup(s->remote_topic);
		break;
	case LOCAL_TOPIC_INVALID_LEVEL:
		new_topic = NULL;
		break;

	default:
		for (int i = 0; end_pos && i < s->local_save_level; ++i) {
			end_pos = strchr(end_pos, '/');
			if (end_pos)
				++end_pos;
		}
		new_topic = end_pos
		    ? nng_strndup(current_pos, end_pos - current_pos - 1)
		    : nng_strdup(current_pos);
		break;
	}

	log_debug("new repub topic: %s", new_topic);
	return new_topic;
}
