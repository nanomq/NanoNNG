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

static cJSON *path_expression_core(cJSON *parent, cJSON *jso)
{
    char *str = jso->string;
    char *p = jso->string;
    char *p_a = jso->string + strlen(jso->string);
    cJSON *jso_new = NULL;
    jso = cJSON_GetObjectItem(jso, jso->string);
    char t[128] = { 0 };
    while (NULL != (p = strrchr(str, '.'))) {

        // a.b.c: {object}
        // c ==> create json object jso(c, jso)
        *p = '_';
        strncpy(t, p + 1, p_a - p);
        cJSON_AddItemToObject(jso_new, t, jso);
        memset(t, 0, 128);
        // jso_new = json(c, jso)
        jso = jso_new;
        jso_new = NULL;
        p_a = --p;
    }

    strncpy(t, str, p_a - str + 1);
    cJSON_AddItemToObject(parent, t, jso);
    memset(t, 0, 128);

    return parent;
}

// {"bridge.sqlite":{"enable":false,"disk_cache_size":102400,"mounted_file_path":"/tmp/","flush_mem_threshold":100,"resend_interval":5000}}
// {"bridge":{"sqlite":{"enable":false,"disk_cache_size":102400,"mounted_file_path":"/tmp/","flush_mem_threshold":100,"resend_interval":5000}}}

// level-order traversal
// find key bridge.sqlite 
// create object sqlite with object value
// insert object bridge with sqlite
// delete bridge.sqlite

static cJSON *path_expression(cJSON *jso)
{
    cJSON *parent = jso;
    cJSON *child = jso->child;

    while (child) {
        if (NULL != child->string && NULL != strchr(child->string, '.')) {
            split(parent, child);
        }
        if (child->child) {
            path_expression(child);
        }
        child = child->next;
    }

    return jso;
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
            return path_expression(jso);
        }
    }

    return NULL;

}