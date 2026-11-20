#pragma once
#include <cstdint>
bool setCpuFrequencyMhz(std::uint32_t);
bool psramInit();
extern "C" bool btInUse();
void init();
void initVariant();
