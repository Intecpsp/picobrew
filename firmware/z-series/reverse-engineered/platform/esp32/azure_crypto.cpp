#include "azure_crypto.hpp"
#include <mbedtls/md.h>
namespace zseries { namespace oem { namespace esp32 {
int azure_hmac_sha256(void*,const std::uint8_t* key,std::size_t key_size,
        const std::uint8_t* input,std::size_t input_size,std::uint8_t output[32]) {
    if(!key||(!input&&input_size)||!output)return MBEDTLS_ERR_MD_BAD_INPUT_DATA;
    const auto* sha256=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(!sha256)return MBEDTLS_ERR_MD_FEATURE_UNAVAILABLE;
    return mbedtls_md_hmac(sha256,key,key_size,input,input_size,output);
}
}}}
