/* A small strict JSON reader for manifests (pack.json): the whole text is
 * parsed into a fixed node pool, no allocation per node. Strings are kept
 * decoded (\uXXXX above 0x7F becomes '?'). docs/eras/MOD-PACK-DESIGN.md 5. */
#ifndef NFL2K5_JSON_H
#define NFL2K5_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NJ_NULL, NJ_FALSE, NJ_TRUE, NJ_NUMBER, NJ_STRING, NJ_ARRAY, NJ_OBJECT };

typedef struct NjNode {
    int type;
    const char *key;        /* member name inside an object, else NULL */
    const char *str;        /* NJ_STRING */
    double num;             /* NJ_NUMBER */
    int child, next;        /* first child / next sibling (node index), -1 = none */
} NjNode;

typedef struct NjDoc {
    NjNode nodes[512];
    int count;
    char strings[16384];
    size_t used;
    char error[96];
} NjDoc;

/* 0 on success; doc->error says what went wrong otherwise. The root is node 0. */
int nj_parse(NjDoc *doc, const char *text, size_t len);
/* Member `key` of object node `obj`, or -1. */
int nj_get(const NjDoc *doc, int obj, const char *key);
/* A string member (or `def` when missing / not a string). */
const char *nj_str(const NjDoc *doc, int obj, const char *key, const char *def);
double nj_num(const NjDoc *doc, int obj, const char *key, double def);
/* The i-th element of an array node, or -1. */
int nj_at(const NjDoc *doc, int arr, int i);

#ifdef __cplusplus
}
#endif

#endif
