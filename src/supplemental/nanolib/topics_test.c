#include "nng/supplemental/nanolib/topics.h"
#include "nuts.h"


void
test_generate_repub_topics(void)
{
    char* remote_topic = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/abv/#";
    char* local_topic = "+/abv/#";
    topics *topic = malloc(sizeof(topics));
    topic->local_topic = strdup(local_topic);
    topic->remote_topic     = strdup(remote_topic);
    topic->remote_topic_len = strlen(remote_topic);
    topic->local_topic_len  = strlen(local_topic);
    char *test_topic = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd/asdasda");
    preprocess_topics(topic);
    char * new_topic = generate_repub_topic(topic, test_topic);
    NUTS_MATCH(new_topic, "abc/abv/adasdasd/asdasda");

    nng_strfree(new_topic);
    nng_strfree(test_topic);
    nng_strfree(topic->local_topic);
    nng_strfree(topic->remote_topic);
    free(topic);
    
    char* remote_topic1 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/abv/+/#";
    char* local_topic1 = "+";
    topics *topic1 = malloc(sizeof(topics));
    topic1->local_topic = strdup(local_topic1);
    topic1->remote_topic = strdup(remote_topic1);
    topic1->remote_topic_len = strlen(remote_topic1);
    topic1->local_topic_len  = strlen(local_topic1);
    char *test_topic1 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd/asdasda");
    preprocess_topics(topic1);
    char * new_topic1 = generate_repub_topic(topic1, test_topic1);
    NUTS_MATCH(new_topic1, "abc");

    nng_strfree(new_topic1);
    nng_strfree(test_topic1);
    nng_strfree(topic1->local_topic);
    nng_strfree(topic1->remote_topic);
    free(topic1);


    char* remote_topic2 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/abv/#";
    char* local_topic2 = "zb/+";
    topics *topic2 = malloc(sizeof(topics));
    topic2->local_topic = strdup(local_topic2);
    topic2->remote_topic = strdup(remote_topic2);
    topic2->remote_topic_len = strlen(remote_topic2);
    topic2->local_topic_len  = strlen(local_topic2);

    char *test_topic2 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd/asdasda");
    preprocess_topics(topic2);
    char * new_topic2 = generate_repub_topic(topic2, test_topic2);
    NUTS_MATCH(new_topic2, "zb/abc");

    nng_strfree(new_topic2);
    nng_strfree(test_topic2);
    nng_strfree(topic2->local_topic);
    nng_strfree(topic2->remote_topic);
    free(topic2);

    char* remote_topic3 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/abv/#";
    char* local_topic3 = "#";
    topics *topic3 = malloc(sizeof(topics));
    topic3->local_topic = strdup(local_topic3);
    topic3->remote_topic = strdup(remote_topic3);
    topic3->remote_topic_len = strlen(remote_topic3);
    topic3->local_topic_len  = strlen(local_topic3);

    char *test_topic3 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd/asdasda");
    preprocess_topics(topic3);
    char * new_topic3 = generate_repub_topic(topic3, test_topic3);
    NUTS_MATCH(new_topic3, "adasdasd/asdasda");

    nng_strfree(new_topic3);
    nng_strfree(test_topic3);
    nng_strfree(topic3->local_topic);
    nng_strfree(topic3->remote_topic);
    free(topic3);

    char* remote_topic4 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/abv/#";
    char* local_topic4 = "#";
    topics *topic4 = malloc(sizeof(topics));
    topic4->local_topic = strdup(local_topic4);
    topic4->remote_topic = strdup(remote_topic4);
    topic4->remote_topic_len = strlen(remote_topic4);
    topic4->local_topic_len  = strlen(local_topic4);

    char *test_topic4 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd");
    preprocess_topics(topic4);
    char * new_topic4 = generate_repub_topic(topic4, test_topic4);
    NUTS_MATCH(new_topic4, "adasdasd");

    nng_strfree(new_topic4);
    nng_strfree(test_topic4);
    nng_strfree(topic4->local_topic);
    nng_strfree(topic4->remote_topic);
    free(topic4);

    char* remote_topic5 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/abv/#";
    char* local_topic5 = "abc";
    topics *topic5 = malloc(sizeof(topics));
    topic5->local_topic = strdup(local_topic5);
    topic5->remote_topic = strdup(remote_topic5);
    topic5->remote_topic_len = strlen(remote_topic5);
    topic5->local_topic_len  = strlen(local_topic5);

    char *test_topic5 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd");

    preprocess_topics(topic5);
    char * new_topic5 = generate_repub_topic(topic5, test_topic5);
    NUTS_MATCH(new_topic5, "abc");

    nng_strfree(new_topic5);
    nng_strfree(test_topic5);
    nng_strfree(topic5->local_topic);
    nng_strfree(topic5->remote_topic);
    free(topic5);


    char* remote_topic6 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/abv/#";
    char* local_topic6 = "+/#";
    topics *topic6 = malloc(sizeof(topics));
    topic6->local_topic = strdup(local_topic6);
    topic6->remote_topic = strdup(remote_topic6);
    topic6->remote_topic_len = strlen(remote_topic6);
    topic6->local_topic_len  = strlen(local_topic6);

    char *test_topic6 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd");

    preprocess_topics(topic6);
    char * new_topic6 = generate_repub_topic(topic6, test_topic6);
    NUTS_NULL(new_topic6);

    nng_strfree(test_topic6);
    nng_strfree(topic6->local_topic);
    nng_strfree(topic6->remote_topic);
    free(topic6);


    char* remote_topic7 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/abv/#/+";
    char* local_topic7 = "abv/#/+";
    topics *topic7 = malloc(sizeof(topics));
    topic7->local_topic = strdup(local_topic7);
    topic7->remote_topic = strdup(remote_topic7);
    topic7->remote_topic_len = strlen(remote_topic7);
    topic7->local_topic_len  = strlen(local_topic7);

    char *test_topic7 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd");

    preprocess_topics(topic7);
    char * new_topic7 = generate_repub_topic(topic7, test_topic7);
    NUTS_NULL(new_topic7);

    nng_strfree(test_topic7);
    nng_strfree(topic7->local_topic);
    nng_strfree(topic7->remote_topic);
    free(topic7);


    char* remote_topic8 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/+/abv/#/+";
    char* local_topic8 = "+/+";
    topics *topic8 = malloc(sizeof(topics));
    topic8->local_topic = strdup(local_topic8);
    topic8->remote_topic = strdup(remote_topic8);
    topic8->remote_topic_len = strlen(remote_topic8);
    topic8->local_topic_len  = strlen(local_topic8);

    char *test_topic8 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd");

    preprocess_topics(topic8);
    char * new_topic8 = generate_repub_topic(topic8, test_topic8);
    NUTS_NULL(new_topic8);

    nng_strfree(test_topic8);
    nng_strfree(topic8->local_topic);
    nng_strfree(topic8->remote_topic);
    free(topic8);

    char* remote_topic9 = "785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/+/+/abv/#";
    char* local_topic9 = "+/+";
    topics *topic9 = malloc(sizeof(topics));
    topic9->local_topic = strdup(local_topic9);
    topic9->remote_topic = strdup(remote_topic9);
    topic9->remote_topic_len = strlen(remote_topic9);
    topic9->local_topic_len  = strlen(local_topic9);

    char *test_topic9 = strdup("785fa9a4-1382-4c58-a11e-c89d94569f6b/zb/abc/abv/adasdasd");

    preprocess_topics(topic9);
    char * new_topic9 = generate_repub_topic(topic9, test_topic9);
    NUTS_MATCH(new_topic9, "abc/abv");

    nng_strfree(new_topic9);
    nng_strfree(test_topic9);
    nng_strfree(topic9->local_topic);
    nng_strfree(topic9->remote_topic);
    free(topic9);


}

NUTS_TESTS = {
   {"Generate repub topics", test_generate_repub_topics},
   {NULL, NULL} 
};