#ifdef PICOFERM_AB
#include "picoferm/version.hpp"
extern "C" __attribute__((used,section(".ferm_ota_identity")))
const char picoferm_ota_identity[32] = "PicoFerm-AB1:" PICOFERM_VERSION;
#endif
