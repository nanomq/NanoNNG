#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H

#include "nng/nng.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/util/platform.h"

#define LOCAL_TOPIC_INVALID_LEVEL (-3)
#define LOCAL_TOPIC_DEFAULT_LEVEL (-2)
#define LOCAL_TOPIC_INFINITE_LEVEL (-1)

NNG_DECL void  preprocess_topics(topics *s, bool is_sub);

NNG_DECL char *generate_repub_topic(const topics *s, char *topic, bool is_sub);

#endif