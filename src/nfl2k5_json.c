/* A small strict JSON reader (src/nfl2k5_json.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nfl2k5_json.h"

typedef struct { NjDoc *d; const char *b, *p, *e; int depth; } P;

static int fail(P *s, const char *why)
{
    if (!s->d->error[0]) snprintf(s->d->error, sizeof s->d->error, "%s at byte %d", why, (int)(s->p - s->b));
    return -1;
}

static void ws(P *s) { while (s->p < s->e && (*s->p == ' ' || *s->p == '\t' || *s->p == '\r' || *s->p == '\n')) s->p++; }

static int node(P *s, int type)
{
    NjNode *n;
    if (s->d->count >= (int)(sizeof s->d->nodes / sizeof s->d->nodes[0])) return fail(s, "too many values");
    n = &s->d->nodes[s->d->count];
    memset(n, 0, sizeof *n);
    n->type = type;
    n->child = n->next = -1;
    return s->d->count++;
}

/* a string at s->p (after the quote check); returns the stored copy or NULL */
static const char *string(P *s)
{
    char *out = s->d->strings + s->d->used, *o = out;
    char *end = s->d->strings + sizeof s->d->strings - 1;
    if (s->p >= s->e || *s->p != '"') { fail(s, "string expected"); return NULL; }
    s->p++;
    while (s->p < s->e && *s->p != '"') {
        char c = *s->p++;
        if ((unsigned char)c < 0x20) { fail(s, "control character in a string"); return NULL; }
        if (c == '\\') {
            if (s->p >= s->e) break;
            c = *s->p++;
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                unsigned v = 0;
                int k;
                for (k = 0; k < 4; k++) {
                    char h = s->p < s->e ? *s->p++ : 0;
                    v <<= 4;
                    if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                    else { fail(s, "bad \\u escape"); return NULL; }
                }
                c = v < 0x80 && v ? (char)v : '?';
                break;
            }
            default: fail(s, "bad escape"); return NULL;
            }
        }
        if (o >= end) { fail(s, "strings too long"); return NULL; }
        *o++ = c;
    }
    if (s->p >= s->e) { fail(s, "unterminated string"); return NULL; }
    s->p++;
    *o++ = 0;
    s->d->used += (size_t)(o - out);
    return out;
}

static int value(P *s);

static int container(P *s, int obj)
{
    int me = node(s, obj ? NJ_OBJECT : NJ_ARRAY), last = -1;
    if (me < 0) return -1;
    if (++s->depth > 32) return fail(s, "nested too deep");
    s->p++;
    ws(s);
    if (s->p < s->e && *s->p == (obj ? '}' : ']')) { s->p++; s->depth--; return me; }
    for (;;) {
        const char *key = NULL;
        int v;
        ws(s);
        if (obj) {
            if (!(key = string(s))) return -1;
            ws(s);
            if (s->p >= s->e || *s->p != ':') return fail(s, "':' expected");
            s->p++;
        }
        if ((v = value(s)) < 0) return -1;
        s->d->nodes[v].key = key;
        if (last < 0) s->d->nodes[me].child = v; else s->d->nodes[last].next = v;
        last = v;
        ws(s);
        if (s->p < s->e && *s->p == ',') { s->p++; continue; }
        if (s->p < s->e && *s->p == (obj ? '}' : ']')) { s->p++; break; }
        return fail(s, obj ? "',' or '}' expected" : "',' or ']' expected");
    }
    s->depth--;
    return me;
}

static int value(P *s)
{
    int n;
    ws(s);
    if (s->p >= s->e) return fail(s, "value expected");
    switch (*s->p) {
    case '{': return container(s, 1);
    case '[': return container(s, 0);
    case '"': {
        const char *v;
        if ((n = node(s, NJ_STRING)) < 0 || !(v = string(s))) return -1;
        s->d->nodes[n].str = v;
        return n;
    }
    case 't': if (s->e - s->p >= 4 && !memcmp(s->p, "true", 4)) { s->p += 4; return node(s, NJ_TRUE); } break;
    case 'f': if (s->e - s->p >= 5 && !memcmp(s->p, "false", 5)) { s->p += 5; return node(s, NJ_FALSE); } break;
    case 'n': if (s->e - s->p >= 4 && !memcmp(s->p, "null", 4)) { s->p += 4; return node(s, NJ_NULL); } break;
    default:
        if (*s->p == '-' || (*s->p >= '0' && *s->p <= '9')) {
            char buf[64], *end;
            size_t k = 0;
            while (s->p + k < s->e && k < sizeof buf - 1 && strchr("+-0123456789.eE", s->p[k])) { buf[k] = s->p[k]; k++; }
            buf[k] = 0;
            if ((n = node(s, NJ_NUMBER)) < 0) return -1;
            s->d->nodes[n].num = strtod(buf, &end);
            if (end == buf) return fail(s, "bad number");
            s->p += k;
            return n;
        }
    }
    return fail(s, "unexpected character");
}

int nj_parse(NjDoc *doc, const char *text, size_t len)
{
    P s;
    memset(doc, 0, sizeof *doc);
    s.d = doc; s.b = text; s.p = text; s.e = text + len; s.depth = 0;
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) s.p += 3;
    if (value(&s) != 0) { if (!doc->error[0]) snprintf(doc->error, sizeof doc->error, "not JSON"); return -1; }
    ws(&s);
    if (s.p != s.e) { snprintf(doc->error, sizeof doc->error, "text after the value"); return -1; }
    return 0;
}

int nj_get(const NjDoc *doc, int obj, const char *key)
{
    int c;
    if (obj < 0 || obj >= doc->count || doc->nodes[obj].type != NJ_OBJECT) return -1;
    for (c = doc->nodes[obj].child; c >= 0; c = doc->nodes[c].next)
        if (doc->nodes[c].key && !strcmp(doc->nodes[c].key, key)) return c;
    return -1;
}

const char *nj_str(const NjDoc *doc, int obj, const char *key, const char *def)
{
    int n = nj_get(doc, obj, key);
    return n >= 0 && doc->nodes[n].type == NJ_STRING ? doc->nodes[n].str : def;
}

double nj_num(const NjDoc *doc, int obj, const char *key, double def)
{
    int n = nj_get(doc, obj, key);
    return n >= 0 && doc->nodes[n].type == NJ_NUMBER ? doc->nodes[n].num : def;
}

int nj_at(const NjDoc *doc, int arr, int i)
{
    int c;
    if (arr < 0 || arr >= doc->count || doc->nodes[arr].type != NJ_ARRAY) return -1;
    for (c = doc->nodes[arr].child; c >= 0 && i > 0; c = doc->nodes[c].next) i--;
    return c;
}
