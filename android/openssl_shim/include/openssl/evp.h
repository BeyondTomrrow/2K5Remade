#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct evp_md_st { int size; } EVP_MD;
const EVP_MD *EVP_sha1(void);
#ifdef __cplusplus
}
#endif
