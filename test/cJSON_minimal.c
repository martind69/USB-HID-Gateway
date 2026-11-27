/* Minimal JSON parser, host tests only. */
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
typedef struct { const char *p, *e; } cur_t;
static void ws(cur_t *c) { while (c->p < c->e && strchr(" \t\r\n", *c->p) && *c->p) ++c->p; }
static cJSON *val(cur_t *c);
static char *str(cur_t *c) {
    if (c->p >= c->e || *c->p != '"') return NULL;
    const char *s = ++c->p;
    while (c->p < c->e && *c->p != '"') { if (*c->p == '\\') return NULL; ++c->p; }
    if (c->p >= c->e) return NULL;
    size_t n = (size_t)(c->p - s); char *r = malloc(n + 1); memcpy(r, s, n); r[n] = 0; ++c->p; return r;
}
static cJSON *box(cur_t *c, int t, char close) {
    cJSON *n = calloc(1, sizeof *n); n->type = t; ++c->p; ws(c);
    if (c->p < c->e && *c->p == close) { ++c->p; return n; }
    cJSON **tail = &n->child;
    for (;;) {
        ws(c); char *k = NULL;
        if (t == cJSON_Object) { k = str(c); ws(c); if (!k || c->p >= c->e || *c->p != ':') { free(k); cJSON_Delete(n); return NULL; } ++c->p; }
        cJSON *v = val(c); if (!v) { free(k); cJSON_Delete(n); return NULL; }
        v->string = k; *tail = v; tail = &v->next; ws(c);
        if (c->p < c->e && *c->p == ',') { ++c->p; continue; }
        if (c->p < c->e && *c->p == close) { ++c->p; return n; }
        cJSON_Delete(n); return NULL;
    }
}
static cJSON *val(cur_t *c) {
    ws(c); if (c->p >= c->e) return NULL;
    if (*c->p == '{') return box(c, cJSON_Object, '}');
    if (*c->p == '[') return box(c, cJSON_Array, ']');
    cJSON *n = calloc(1, sizeof *n);
    if (*c->p == '"') { n->type = cJSON_String; n->valuestring = str(c); if (!n->valuestring) { free(n); return NULL; } return n; }
    if ((size_t)(c->e - c->p) >= 4 && !strncmp(c->p, "true", 4)) { c->p += 4; n->type = cJSON_True; return n; }
    if ((size_t)(c->e - c->p) >= 5 && !strncmp(c->p, "false", 5)) { c->p += 5; n->type = cJSON_False; return n; }
    char b[64]; size_t k = 0;
    while (c->p < c->e && k < 63 && strchr("+-0123456789.eE", *c->p) && *c->p) b[k++] = *c->p++;
    b[k] = 0; char *end; n->valuedouble = strtod(b, &end);
    if (!k || *end) { free(n); return NULL; } n->type = cJSON_Number; return n;
}
cJSON *cJSON_ParseWithLength(const char *t, size_t l) { cur_t c = { t, t + l }; cJSON *r = val(&c); ws(&c); if (r && c.p != c.e) { cJSON_Delete(r); return NULL; } return r; }
void cJSON_Delete(cJSON *i) { while (i) { cJSON *n = i->next; cJSON_Delete(i->child); free(i->valuestring); free(i->string); free(i); i = n; } }
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *o, const char *k) { for (cJSON *i = o ? o->child : NULL; i; i = i->next) if (i->string && !strcmp(i->string, k)) return i; return NULL; }
int cJSON_GetArraySize(const cJSON *a) { int n = 0; for (cJSON *i = a ? a->child : NULL; i; i = i->next) ++n; return n; }
