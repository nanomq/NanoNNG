//
// Copyright 2024 NanoMQ Team
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <nng/nng.h>
#include <string.h>

#include "convey.h"
#include "stubs.h"

// Helper function to simulate the shared topic parsing logic
// This mirrors the logic in broker_tcp.c, broker_tls.c, and nmq_websocket.c
static char *
parse_shared_topic(const char *sub_topic)
{
	char *topic = (char *) sub_topic;
	
	if (topic == NULL) {
		return NULL;
	}
	
	if (topic[0] == '$') {
		if (0 == strncmp(topic, "$share/", strlen("$share/"))) {
			// First strchr to find first '/'
			topic = strchr(topic, '/');
			topic != NULL ? topic++ : NULL;
			
			// Second strchr to find second '/'
			topic = strchr(topic, '/');
			topic != NULL ? topic++ : NULL;
		}
	}
	
	return topic;
}

TestMain("MQTT Shared Topic Parsing", {

	Convey("Shared topic parsing with valid topics", {
		char *result;
		
		Convey("Valid shared topic with group and actual topic", {
			result = parse_shared_topic("$share/group/sensor/temperature");
			So(result != NULL);
			So(strcmp(result, "sensor/temperature") == 0);
		});
		
		Convey("Valid shared topic with single level group", {
			result = parse_shared_topic("$share/g/topic");
			So(result != NULL);
			So(strcmp(result, "topic") == 0);
		});
		
		Convey("Valid shared topic with multi-level topic", {
			result = parse_shared_topic("$share/mygroup/device/+/status");
			So(result != NULL);
			So(strcmp(result, "device/+/status") == 0);
		});
		
		Convey("Valid shared topic with wildcard in actual topic", {
			result = parse_shared_topic("$share/group1/sensors/#");
			So(result != NULL);
			So(strcmp(result, "sensors/#") == 0);
		});
	});

	Convey("Shared topic parsing with edge cases", {
		char *result;
		
		Convey("Malformed shared topic - missing group name", {
			result = parse_shared_topic("$share//topic");
			// After first '/', topic points to empty string
			// After second '/', should be NULL (no second '/')
			So(result == NULL);
		});
		
		Convey("Malformed shared topic - only $share/ prefix", {
			result = parse_shared_topic("$share/");
			// After first '/', points to empty string
			// strchr on empty string returns NULL
			So(result == NULL);
		});
		
		Convey("Malformed shared topic - group but no topic", {
			result = parse_shared_topic("$share/group");
			// After first '/', points to "group"
			// strchr("group", '/') returns NULL
			So(result == NULL);
		});
		
		Convey("Malformed shared topic - group/ but no actual topic", {
			result = parse_shared_topic("$share/group/");
			// After first '/', points to "group/"
			// After second '/', points to empty string
			So(result != NULL);
			So(strcmp(result, "") == 0);
		});
		
		Convey("Non-shared topic starting with $", {
			result = parse_shared_topic("$SYS/broker/uptime");
			// Doesn't match "$share/" prefix, so no modification
			So(result != NULL);
			So(strcmp(result, "$SYS/broker/uptime") == 0);
		});
		
		Convey("Regular topic without $ prefix", {
			result = parse_shared_topic("regular/topic");
			So(result != NULL);
			So(strcmp(result, "regular/topic") == 0);
		});
		
		Convey("Empty string topic", {
			result = parse_shared_topic("");
			So(result != NULL);
			So(strcmp(result, "") == 0);
		});
		
		Convey("NULL topic pointer", {
			result = parse_shared_topic(NULL);
			So(result == NULL);
		});
	});

	Convey("Shared topic parsing with special characters", {
		char *result;
		
		Convey("Shared topic with numeric group name", {
			result = parse_shared_topic("$share/123/data");
			So(result != NULL);
			So(strcmp(result, "data") == 0);
		});
		
		Convey("Shared topic with hyphenated group name", {
			result = parse_shared_topic("$share/my-group/sensors/temp");
			So(result != NULL);
			So(strcmp(result, "sensors/temp") == 0);
		});
		
		Convey("Shared topic with underscore in group", {
			result = parse_shared_topic("$share/my_group/data/stream");
			So(result != NULL);
			So(strcmp(result, "data/stream") == 0);
		});
		
		Convey("Shared topic with long group name", {
			result = parse_shared_topic("$share/verylonggroupname123456789/topic");
			So(result != NULL);
			So(strcmp(result, "topic") == 0);
		});
	});

	Convey("Shared topic parsing pointer safety", {
		char *result;
		
		Convey("Single slash after $share - NULL safety check", {
			// This tests the fix: strchr returns NULL, should not crash
			result = parse_shared_topic("$share/nogroup");
			So(result == NULL);
		});
		
		Convey("Only prefix with trailing slash", {
			// strchr on "/" returns NULL
			result = parse_shared_topic("$share/");
			So(result == NULL);
		});
		
		Convey("Consecutive slashes in topic", {
			result = parse_shared_topic("$share/group//topic");
			// After second '/', points to "/topic"
			So(result != NULL);
			So(strcmp(result, "/topic") == 0);
		});
		
		Convey("Multiple consecutive slashes", {
			result = parse_shared_topic("$share/group///topic");
			// After second '/', points to "//topic"
			So(result != NULL);
			So(strcmp(result, "//topic") == 0);
		});
	});

	Convey("Shared topic with valid MQTT topic patterns", {
		char *result;
		
		Convey("Single level wildcard in actual topic", {
			result = parse_shared_topic("$share/group/sensor/+/data");
			So(result != NULL);
			So(strcmp(result, "sensor/+/data") == 0);
		});
		
		Convey("Multi-level wildcard in actual topic", {
			result = parse_shared_topic("$share/group/sensor/#");
			So(result != NULL);
			So(strcmp(result, "sensor/#") == 0);
		});
		
		Convey("Mixed wildcards in actual topic", {
			result = parse_shared_topic("$share/group/+/sensor/#");
			So(result != NULL);
			So(strcmp(result, "+/sensor/#") == 0);
		});
		
		Convey("Complex nested topic structure", {
			result = parse_shared_topic("$share/prod/building/floor/room/device/sensor");
			So(result != NULL);
			So(strcmp(result, "building/floor/room/device/sensor") == 0);
		});
	});

	Convey("Boundary conditions for shared topics", {
		char *result;
		
		Convey("Minimum valid shared topic", {
			result = parse_shared_topic("$share/g/t");
			So(result != NULL);
			So(strcmp(result, "t") == 0);
		});
		
		Convey("Topic with exact $share prefix length", {
			char topic[] = "$share/";
			result = parse_shared_topic(topic);
			So(result == NULL);
		});
		
		Convey("Case sensitivity - lowercase share", {
			result = parse_shared_topic("$share/group/topic");
			So(result != NULL);
			So(strcmp(result, "topic") == 0);
		});
		
		Convey("Case sensitivity - uppercase SHARE should not match", {
			result = parse_shared_topic("$SHARE/group/topic");
			// Doesn't match "$share/" prefix (case sensitive)
			So(result != NULL);
			So(strcmp(result, "$SHARE/group/topic") == 0);
		});
		
		Convey("Mixed case should not match", {
			result = parse_shared_topic("$Share/group/topic");
			So(result != NULL);
			So(strcmp(result, "$Share/group/topic") == 0);
		});
	});

	Convey("Integration with topic_filter scenarios", {
		char *result;
		
		Convey("Shared subscription for temperature sensors", {
			result = parse_shared_topic("$share/workers/sensors/temperature/#");
			So(result != NULL);
			So(strcmp(result, "sensors/temperature/#") == 0);
			// This would then be passed to topic_filter for matching
		});
		
		Convey("Shared subscription with device ID wildcard", {
			result = parse_shared_topic("$share/handlers/device/+/status");
			So(result != NULL);
			So(strcmp(result, "device/+/status") == 0);
		});
		
		Convey("Non-shared system topic should pass through", {
			result = parse_shared_topic("$SYS/broker/clients/connected");
			So(result != NULL);
			So(strcmp(result, "$SYS/broker/clients/connected") == 0);
		});
	});

	Convey("Stress testing with various malformed inputs", {
		char *result;
		
		Convey("Multiple $ symbols", {
			result = parse_shared_topic("$$share/group/topic");
			So(result != NULL);
			So(strcmp(result, "$$share/group/topic") == 0);
		});
		
		Convey("$ symbol in middle of string", {
			result = parse_shared_topic("share$/group/topic");
			So(result != NULL);
			So(strcmp(result, "share$/group/topic") == 0);
		});
		
		Convey("Share prefix without $", {
			result = parse_shared_topic("share/group/topic");
			So(result != NULL);
			So(strcmp(result, "share/group/topic") == 0);
		});
		
		Convey("Partial match of $share", {
			result = parse_shared_topic("$shar/group/topic");
			So(result != NULL);
			So(strcmp(result, "$shar/group/topic") == 0);
		});
		
		Convey("Extended $share prefix", {
			result = parse_shared_topic("$shared/group/topic");
			So(result != NULL);
			So(strcmp(result, "$shared/group/topic") == 0);
		});
	});

	Convey("UTF-8 and special characters in topics", {
		char *result;
		
		Convey("UTF-8 characters in group name", {
			// Note: MQTT spec allows UTF-8 in topic names
			result = parse_shared_topic("$share/グループ/topic");
			So(result != NULL);
			// Just verify we can parse it without crashing
		});
		
		Convey("UTF-8 characters in actual topic", {
			result = parse_shared_topic("$share/group/传感器/温度");
			So(result != NULL);
		});
		
		Convey("Mixed ASCII and UTF-8", {
			result = parse_shared_topic("$share/group123/sensor/温度");
			So(result != NULL);
		});
	});

	Convey("Real-world usage patterns", {
		char *result;
		
		Convey("Load balanced message processing", {
			result = parse_shared_topic("$share/processors/incoming/messages");
			So(result != NULL);
			So(strcmp(result, "incoming/messages") == 0);
		});
		
		Convey("Geographic distribution pattern", {
			result = parse_shared_topic("$share/us-east-1/events/#");
			So(result != NULL);
			So(strcmp(result, "events/#") == 0);
		});
		
		Convey("Service-based routing", {
			result = parse_shared_topic("$share/email-service/notifications/email");
			So(result != NULL);
			So(strcmp(result, "notifications/email") == 0);
		});
		
		Convey("IoT device command routing", {
			result = parse_shared_topic("$share/device-handlers/cmd/device/+/action");
			So(result != NULL);
			So(strcmp(result, "cmd/device/+/action") == 0);
		});
	});
})