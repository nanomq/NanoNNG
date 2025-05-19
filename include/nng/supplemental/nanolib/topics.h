#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H

#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"
#include "nng/supplemental/nanolib/conf.h"


NNG_DECL void preprocess_topics(topics* s);
NNG_DECL void generate_repub_topic(const topics* s, const char* topic);

#endif