#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H

#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"
#include "nng/supplemental/nanolib/conf.h"


NNG_DECL void validate_and_preprocess_topics(topics* s);
NNG_DECL void generate_repub_topic(const topics* s);


#endif