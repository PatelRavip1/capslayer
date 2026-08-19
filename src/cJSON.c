/*
  cJSON - Lightweight JSON parser in ANSI C
  Dual-licensed under MIT or public domain.
*/

#if defined(_MSC_VER)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

#include "cJSON.h"

typedef struct internal_hooks
{
    void *(*allocate)(size_t size);
    void (*deallocate)(void *pointer);
} internal_hooks;

static internal_hooks global_hooks = { malloc, free };

void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (!hooks) {
        global_hooks.allocate = malloc;
        global_hooks.deallocate = free;
        return;
    }
    global_hooks.allocate = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
    global_hooks.deallocate = (hooks->free_fn) ? hooks->free_fn : free;
}

static void *cJSON_malloc(size_t size)
{
    return global_hooks.allocate(size);
}

static void cJSON_free(void *object)
{
    global_hooks.deallocate(object);
}

static cJSON *cJSON_New_Item(void)
{
    cJSON* node = (cJSON*)cJSON_malloc(sizeof(cJSON));
    if (node) {
        memset(node, '\0', sizeof(cJSON));
    }
    return node;
}

void cJSON_Delete(cJSON *item)
{
    cJSON *next = NULL;
    while (item != NULL) {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && (item->child != NULL)) {
            cJSON_Delete(item->child);
        }
        if (!(item->type & cJSON_IsReference) && (item->valuestring != NULL)) {
            cJSON_free(item->valuestring);
        }
        if (!(item->type & cJSON_StringIsConst) && (item->string != NULL)) {
            cJSON_free(item->string);
        }
        cJSON_free(item);
        item = next;
    }
}

static const unsigned char *skip_whitespace(const unsigned char *in)
{
    while (in && *in && (*in <= 32)) {
        in++;
    }
    return in;
}

static const char *global_error_ptr = NULL;

const char *cJSON_GetErrorPtr(void)
{
    return global_error_ptr;
}

static const unsigned char *parse_number(cJSON * const item, const unsigned char * const input)
{
    double number = 0;
    unsigned char *after_end = NULL;

    if (input == NULL) {
        return NULL;
    }

    number = strtod((const char *)input, (char **)&after_end);
    if (input == after_end) {
        return NULL;
    }

    item->valuedouble = number;
    if (number >= INT_MAX) {
        item->valueint = INT_MAX;
    } else if (number <= (double)INT_MIN) {
        item->valueint = INT_MIN;
    } else {
        item->valueint = (int)number;
    }
    item->type = cJSON_Number;

    return after_end;
}

static unsigned char parse_hex4(const unsigned char * const input)
{
    unsigned int h = 0;
    size_t i = 0;
    for (i = 0; i < 4; i++) {
        unsigned char c = input[i];
        if ((c >= '0') && (c <= '9')) {
            h += (unsigned int)(c - '0');
        } else if ((c >= 'A') && (c <= 'F')) {
            h += (unsigned int)(10 + c - 'A');
        } else if ((c >= 'a') && (c <= 'f')) {
            h += (unsigned int)(10 + c - 'a');
        } else {
            return 0;
        }
        if (i < 3) {
            h = h << 4;
        }
    }
    return (unsigned char)h;
}

static const unsigned char *parse_string(cJSON * const item, const unsigned char * const input)
{
    const unsigned char *ptr = input + 1;
    unsigned char *output = NULL;
    unsigned char *out_ptr = NULL;
    size_t len = 0;

    if (*input != '\"') {
        return NULL;
    }

    while ((*ptr != '\"') && *ptr) {
        if (*ptr == '\\') {
            ptr++;
            if (*ptr == '\0') return NULL;
        }
        ptr++;
        len++;
    }
    if (*ptr != '\"') {
        return NULL;
    }

    output = (unsigned char *)cJSON_malloc(len + 1);
    if (!output) {
        return NULL;
    }

    ptr = input + 1;
    out_ptr = output;
    while (*ptr != '\"') {
        if (*ptr != '\\') {
            *out_ptr++ = *ptr++;
        } else {
            ptr++;
            switch (*ptr) {
                case 'b': *out_ptr++ = '\b'; break;
                case 'f': *out_ptr++ = '\f'; break;
                case 'n': *out_ptr++ = '\n'; break;
                case 'r': *out_ptr++ = '\r'; break;
                case 't': *out_ptr++ = '\t'; break;
                case '\"': *out_ptr++ = '\"'; break;
                case '\\': *out_ptr++ = '\\'; break;
                case '/': *out_ptr++ = '/'; break;
                case 'u': {
                    unsigned char h = parse_hex4(ptr + 1);
                    ptr += 4;
                    *out_ptr++ = (h > 0) ? h : '?';
                    break;
                }
                default: *out_ptr++ = *ptr; break;
            }
            ptr++;
        }
    }
    *out_ptr = '\0';

    item->valuestring = (char *)output;
    item->type = cJSON_String;

    return ptr + 1;
}

static const unsigned char *parse_value(cJSON * const item, const unsigned char * const input);

static const unsigned char *parse_array(cJSON * const item, const unsigned char * const input)
{
    const unsigned char *buffer = input;
    cJSON *head = NULL;
    cJSON *current = NULL;

    if (*buffer != '[') {
        return NULL;
    }
    buffer++;
    buffer = skip_whitespace(buffer);
    if (*buffer == ']') {
        item->type = cJSON_Array;
        return buffer + 1;
    }

    while (*buffer) {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        if (!head) {
            head = new_item;
            current = head;
        } else {
            current->next = new_item;
            new_item->prev = current;
            current = new_item;
        }

        buffer = parse_value(new_item, buffer);
        if (!buffer) {
            cJSON_Delete(head);
            return NULL;
        }
        buffer = skip_whitespace(buffer);
        if (*buffer == ',') {
            buffer++;
            buffer = skip_whitespace(buffer);
        } else if (*buffer == ']') {
            item->type = cJSON_Array;
            item->child = head;
            return buffer + 1;
        } else {
            cJSON_Delete(head);
            return NULL;
        }
    }

    cJSON_Delete(head);
    return NULL;
}

static const unsigned char *parse_object(cJSON * const item, const unsigned char * const input)
{
    const unsigned char *buffer = input;
    cJSON *head = NULL;
    cJSON *current = NULL;

    if (*buffer != '{') {
        return NULL;
    }
    buffer++;
    buffer = skip_whitespace(buffer);
    if (*buffer == '}') {
        item->type = cJSON_Object;
        return buffer + 1;
    }

    while (*buffer) {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        if (!head) {
            head = new_item;
            current = head;
        } else {
            current->next = new_item;
            new_item->prev = current;
            current = new_item;
        }

        if (*buffer != '\"') {
            cJSON_Delete(head);
            return NULL;
        }

        buffer = parse_string(new_item, buffer);
        if (!buffer) {
            cJSON_Delete(head);
            return NULL;
        }
        new_item->string = new_item->valuestring;
        new_item->valuestring = NULL;

        buffer = skip_whitespace(buffer);
        if (*buffer != ':') {
            cJSON_Delete(head);
            return NULL;
        }
        buffer++;
        buffer = skip_whitespace(buffer);

        buffer = parse_value(new_item, buffer);
        if (!buffer) {
            cJSON_Delete(head);
            return NULL;
        }
        buffer = skip_whitespace(buffer);
        if (*buffer == ',') {
            buffer++;
            buffer = skip_whitespace(buffer);
        } else if (*buffer == '}') {
            item->type = cJSON_Object;
            item->child = head;
            return buffer + 1;
        } else {
            cJSON_Delete(head);
            return NULL;
        }
    }

    cJSON_Delete(head);
    return NULL;
}

static const unsigned char *parse_value(cJSON * const item, const unsigned char * const input)
{
    const unsigned char *buffer = skip_whitespace(input);
    if (!buffer || !*buffer) return NULL;

    if (strncmp((const char *)buffer, "null", 4) == 0) {
        item->type = cJSON_NULL;
        return buffer + 4;
    }
    if (strncmp((const char *)buffer, "false", 5) == 0) {
        item->type = cJSON_False;
        return buffer + 5;
    }
    if (strncmp((const char *)buffer, "true", 4) == 0) {
        item->type = cJSON_True;
        return buffer + 4;
    }
    if (*buffer == '\"') {
        return parse_string(item, buffer);
    }
    if ((*buffer == '-') || ((*buffer >= '0') && (*buffer <= '9'))) {
        return parse_number(item, buffer);
    }
    if (*buffer == '[') {
        return parse_array(item, buffer);
    }
    if (*buffer == '{') {
        return parse_object(item, buffer);
    }

    return NULL;
}

cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated)
{
    const unsigned char *buffer = NULL;
    cJSON *item = cJSON_New_Item();
    if (!item) return NULL;

    global_error_ptr = NULL;
    buffer = parse_value(item, (const unsigned char *)value);
    if (!buffer) {
        cJSON_Delete(item);
        global_error_ptr = (const char *)value;
        return NULL;
    }

    if (require_null_terminated) {
        buffer = skip_whitespace(buffer);
        if (*buffer != '\0') {
            cJSON_Delete(item);
            global_error_ptr = (const char *)buffer;
            return NULL;
        }
    }

    if (return_parse_end) {
        *return_parse_end = (const char *)buffer;
    }

    return item;
}

cJSON *cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, 0, 0);
}

int cJSON_GetArraySize(const cJSON *array)
{
    cJSON *c = array ? array->child : NULL;
    int i = 0;
    while (c) {
        i++;
        c = c->next;
    }
    return i;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
    cJSON *c = array ? array->child : NULL;
    while (c && (index > 0)) {
        index--;
        c = c->next;
    }
    return c;
}

cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string)
{
    cJSON *current_element = NULL;
    if ((object == NULL) || (string == NULL)) {
        return NULL;
    }
    current_element = object->child;
    while (current_element != NULL) {
        if ((current_element->string != NULL) && (_stricmp(string, current_element->string) == 0)) {
            return current_element;
        }
        current_element = current_element->next;
    }
    return NULL;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string)
{
    cJSON *current_element = NULL;
    if ((object == NULL) || (string == NULL)) {
        return NULL;
    }
    current_element = object->child;
    while (current_element != NULL) {
        if ((current_element->string != NULL) && (strcmp(string, current_element->string) == 0)) {
            return current_element;
        }
        current_element = current_element->next;
    }
    return NULL;
}

cJSON_bool cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

cJSON_bool cJSON_IsInvalid(const cJSON * const item)
{
    return item ? (item->type == cJSON_Invalid) : 0;
}

cJSON_bool cJSON_IsFalse(const cJSON * const item)
{
    return item ? ((item->type & 0xFF) == cJSON_False) : 0;
}

cJSON_bool cJSON_IsTrue(const cJSON * const item)
{
    return item ? ((item->type & 0xFF) == cJSON_True) : 0;
}

cJSON_bool cJSON_IsBool(const cJSON * const item)
{
    return item ? ((item->type & (cJSON_True | cJSON_False)) != 0) : 0;
}

cJSON_bool cJSON_IsNull(const cJSON * const item)
{
    return item ? ((item->type & 0xFF) == cJSON_NULL) : 0;
}

cJSON_bool cJSON_IsNumber(const cJSON * const item)
{
    return item ? ((item->type & 0xFF) == cJSON_Number) : 0;
}

cJSON_bool cJSON_IsString(const cJSON * const item)
{
    return item ? ((item->type & 0xFF) == cJSON_String) : 0;
}

cJSON_bool cJSON_IsArray(const cJSON * const item)
{
    return item ? ((item->type & 0xFF) == cJSON_Array) : 0;
}

cJSON_bool cJSON_IsObject(const cJSON * const item)
{
    return item ? ((item->type & 0xFF) == cJSON_Object) : 0;
}

cJSON_bool cJSON_IsRaw(const cJSON * const item)
{
    return item ? ((item->type & 0xFF) == cJSON_Raw) : 0;
}

cJSON *cJSON_CreateNull(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_NULL;
    return item;
}

cJSON *cJSON_CreateTrue(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_True;
    return item;
}

cJSON *cJSON_CreateFalse(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_False;
    return item;
}

cJSON *cJSON_CreateBool(cJSON_bool boolean)
{
    return boolean ? cJSON_CreateTrue() : cJSON_CreateFalse();
}

cJSON *cJSON_CreateNumber(double num)
{
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_Number;
        item->valuedouble = num;
        item->valueint = (int)num;
    }
    return item;
}

cJSON *cJSON_CreateString(const char *string)
{
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_String;
        item->valuestring = _strdup(string);
    }
    return item;
}

cJSON *cJSON_CreateArray(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_Array;
    return item;
}

cJSON *cJSON_CreateObject(void)
{
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_Object;
    return item;
}
