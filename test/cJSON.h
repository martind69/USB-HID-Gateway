#pragma once
#include <stddef.h>
#define cJSON_False 1
#define cJSON_True 2
#define cJSON_NULL 4
#define cJSON_Number 8
#define cJSON_String 16
#define cJSON_Array 32
#define cJSON_Object 64
typedef struct cJSON { struct cJSON *next; struct cJSON *child; int type; char *valuestring; double valuedouble; char *string; } cJSON;
cJSON *cJSON_ParseWithLength(const char *text, size_t length);
void cJSON_Delete(cJSON *item);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *key);
int cJSON_GetArraySize(const cJSON *array);
#define cJSON_IsObject(i) ((i) != NULL && (i)->type == cJSON_Object)
#define cJSON_IsArray(i) ((i) != NULL && (i)->type == cJSON_Array)
#define cJSON_IsString(i) ((i) != NULL && (i)->type == cJSON_String)
#define cJSON_IsNumber(i) ((i) != NULL && (i)->type == cJSON_Number)
#define cJSON_IsTrue(i) ((i) != NULL && (i)->type == cJSON_True)
#define cJSON_ArrayForEach(e, a) for ((e) = (a) != NULL ? (a)->child : NULL; (e) != NULL; (e) = (e)->next)
