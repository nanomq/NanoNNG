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

void
preprocess_topics(topics *s)
{
	const char *current      = s->local_topic;
	const char *end          = s->local_topic + s->local_topic_len;
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

	current = s->remote_topic;
	end     = s->remote_topic + s->remote_topic_len;
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

	char *substr_pos = strstr(s->remote_topic, s->local_topic);
	if (has_wildcard) {
		if (!substr_pos) {
			SET_LEVEL(s, LOCAL_TOPIC_INVALID_LEVEL);
			log_error("local_topic has wildcard but it's not "
			          "substring of remote topic");
			return;
		}

		size_t prefix_length = substr_pos - s->remote_topic;
		s->local_skip_level =
		    count_slashes(s->remote_topic, prefix_length);

		size_t remaining_len = strlen(substr_pos);
		if (remaining_len == s->local_topic_len) {
			s->local_save_level = -1;
		} else if (remaining_len > s->local_topic_len) {
			s->local_save_level = 1 +
			    count_slashes(s->local_topic, s->local_topic_len);
		}
	}
}

char *
generate_repub_topic(const topics *s, char *topic)
{
	const char *current_pos = topic;
	for (int i = 0; i < s->local_skip_level && current_pos; ++i) {
		current_pos = strchr(current_pos, '/');
		if (current_pos)
			++current_pos;
	}

	char *new_topic = NULL;

	switch (s->local_save_level) {
	case LOCAL_TOPIC_INFINITE_LEVEL:
		new_topic = strdup(current_pos ? current_pos : "");
		break;
	case LOCAL_TOPIC_DEFAULT_LEVEL:
		new_topic = strdup(s->local_topic);
		break;
	case LOCAL_TOPIC_INVALID_LEVEL:
		new_topic = NULL;
		break;

	default:
		const char *end_pos = current_pos;
		for (int i = 0; end_pos && i < s->local_save_level; ++i) {
			end_pos = strchr(end_pos, '/');
			if (end_pos)
				++end_pos;
		}
		new_topic = end_pos
		    ? strndup(current_pos, end_pos - current_pos - 1)
		    : strdup(current_pos);
		break;
	}

	log_debug("new repub topic: %s", new_topic);
	return new_topic;
}
