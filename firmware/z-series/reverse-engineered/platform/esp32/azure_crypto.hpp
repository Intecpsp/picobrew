#pragma once
#include <cstddef>
#include <cstdint>
namespace zseries { namespace oem { namespace esp32 {
// AzureServices::hmac binding to the pinned SDK's SHA-256 HMAC implementation.
int azure_hmac_sha256(void*,const std::uint8_t* key,std::size_t key_size,
    const std::uint8_t* input,std::size_t input_size,std::uint8_t output[32]);
}}}
