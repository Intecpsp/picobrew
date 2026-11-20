// Private adapter contract for the recovered diagnostic leaf.
#pragma once
#include "zseries/azure.hpp"
namespace zseries { namespace oem {
// Original NetworkSessionState::parse used by its embedded diagnostic. The
// outer AzureService receive method additionally validates FirmwareVersion;
// that transport gate is intentionally not part of this data-object test.
bool basic_console_decode_session_json(char*,std::size_t,AzureSession&,RecipeJsonError&);
}} // namespace zseries::oem
