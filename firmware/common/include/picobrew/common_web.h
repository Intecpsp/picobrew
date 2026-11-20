#ifndef PICOBREW_COMMON_WEB_H
#define PICOBREW_COMMON_WEB_H
#include "picobrew/http_server.h"
#ifdef __cplusplus
extern "C" {
#endif
// Compressed browser assets are immutable firmware constants. AssetIo performs
// bounded flash-safe reads through the existing streaming HTTP transport.
extern const PicoHttpAsset pico_common_web_assets[];
extern const size_t pico_common_web_assets_count;
extern const PicoHttpAssetIo pico_common_web_asset_io;
#ifdef __cplusplus
}
#endif
#endif
