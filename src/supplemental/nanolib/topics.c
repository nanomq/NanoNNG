#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/topics.h"


static int count_slashes(const char* str, size_t len) {
    int count = 0;
    for (size_t i = 0; i < len; ++i) {
        if (str[i] == '/') ++count;
    }
    return count;
}

static void calculate_topic_hierarchy(topics* s)
{

    char* current = s->local_topic;
    while ((current = strpbrk(current, "+#")) != NULL) {
        s->skip_level = s->save_level = -2;
        if (*current == '#') {
            if (current != s->local_topic + s->local_topic_len - 1) {
                s->skip_level = s->save_level = -3;
                // log_error
                return;
            }
        }
        current++;
    }

    char* substr_pos = strstr(s->remote_topic, s->local_topic);
    if (!substr_pos) return;

    size_t prefix_length = substr_pos - s->remote_topic;
    s->skip_level = count_slashes(s->remote_topic, prefix_length);

    size_t remaining_len = strlen(substr_pos);
    if (remaining_len == s->local_topic_len) {
        s->save_level = -1;
    } else if (remaining_len > s->local_topic_len) {
        s->save_level = 1 + count_slashes(s->local_topic, s->local_topic_len);
    }
}

void validate_and_preprocess_topics(topics* s)
{
    if (!s || !s->remote_topic || !s->local_topic) {
        fprintf(stderr, "Invalid input parameters\n");
        return;
    }
    
    s->remote_topic_len = strlen(s->remote_topic);
    s->local_topic_len = strlen(s->local_topic);

    calculate_topic_hierarchy(s);

}

void generate_optimized_topic(const topics* s, const char *topic)
{
    char* current_pos = topic;
    for (int i = 0; i < s->skip_level && current_pos; ++i) {
        current_pos = strchr(current_pos, '/');
        if (current_pos) ++current_pos;
    }

    char* new_topic = NULL;
    if (s->save_level == -1) {
        new_topic = strdup(current_pos ? current_pos : "");
    } else if (s->save_level == -2) {
        new_topic = strdup(s->local_topic);
    } else {
        char* end_pos = current_pos;
        for (int i = 0; end_pos && i < s->save_level; ++i) {
            end_pos = strchr(end_pos, '/');
            if (end_pos) ++end_pos;
        }
        new_topic = end_pos ? strndup(current_pos, end_pos - current_pos - 1) : strdup(current_pos);
    }

    printf("Optimized topic: %s\n", new_topic);
    free(new_topic);
}

