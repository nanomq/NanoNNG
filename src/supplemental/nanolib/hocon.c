#include "nng/supplemental/nanolib/hocon.h"
#include "stdbool.h"
#include "stdio.h"

static char *skip_whitespace(char *str)
{
    if (NULL == str) {
        return NULL;
    }

    // Skip white_space and tab with bounds check 
    while ('\0' != *str && (' ' == *str || '\t' == *str)) {
        str++;
    }

    return str;

}

cJSON *hocon_str_to_json(char *str)
{
    if (NULL == str) {
        return NULL;
    }

    // If it's not an illegal json object return
    cJSON *jso = cJSON_Parse(str);
    if (cJSON_False == cJSON_IsInvalid(jso)) {
        return jso;
    }

    char *p = str;
    int index = 0;
    char new[strlen(str) * 2];
    memset(new, 0, sizeof(new));

    new[index++] = '{';
    new[index++] = '"';

    // Replace all '=' to ':' 
    // If there are no '=' before object, add ':'
    // If first non-blank character is not '{', push-front '{' and push push-back '}'
    // Replace key to \"key\"
    while ('\0' != *p && NULL != (p = skip_whitespace(p))) {
        while (' ' != *p && '\0' != *p) {

            if ('}' == new[index-1]) {
                new[index++] = ',';
                new[index++] = '"';
            }

            if ('{' == *p && ':' != new[index-1]) {
                new[index++] = '"';
                new[index++] = ':';
            }

            if ('=' == *p) {
                new[index++] = '"';
                new[index++] = ':';
            } else if (',' == *p || '{' == *p) {
                new[index++] = *p;
                // TODO FIXME unsafe
                if ('}' != *(p+1)) {
                    new[index++] = '"';
                }
            } else {
                new[index++] = *p;
            }

            // remove ,"}
            if ('}' == new[index-1] && '"' == new[index-2] && ',' == new[index-3]) {
                new[index-3] = new[index-1];
                new[index-2] = '\0';
                new[index-1] = '\0';
                index -= 2;
            }

            p++;

        }
    }

    new[index++] = '}';


    if ((jso = cJSON_Parse(new))) {
        if (cJSON_False != cJSON_IsInvalid(jso)) {
            return jso;
        }
    }

    return NULL;

}