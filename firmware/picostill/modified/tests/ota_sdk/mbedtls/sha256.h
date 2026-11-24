#pragma once
#include <stddef.h>
struct mbedtls_sha256_context { unsigned char digest[32]; size_t consumed; };
void mbedtls_sha256_init(mbedtls_sha256_context*);
int mbedtls_sha256_starts_ret(mbedtls_sha256_context*,int);
int mbedtls_sha256_update_ret(mbedtls_sha256_context*,const unsigned char*,size_t);
int mbedtls_sha256_finish_ret(mbedtls_sha256_context*,unsigned char*);
void mbedtls_sha256_free(mbedtls_sha256_context*);
