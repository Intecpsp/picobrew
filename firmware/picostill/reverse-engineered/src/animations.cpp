// Original PicoStill 0.0.35 LED table, verified by tools/recover_animations.py.
#include "picostill/hardware.hpp"

namespace picostill { namespace oem {
const AnimationProgram* animation_program(Animation animation) noexcept {
    static const AnimationProgram programs[] = {
        {"", 1},
        {"ryg", 1},
        {"g", 1},
        {"y", 1},
        {"r", 1},
        {"g.", 0},
        {"y.", 0},
        {"r.", 0},
        {"g-", 0},
        {"y-", 0},
        {"r-", 0},
        {"ryg-", 0},
        {"r.ry.yg.g", 0},
        {"ryg.", 0},
        {"gy", 1},
        {"y-", 0},
        {"g", 1},
        {"g.", 0},
        {"y", 1},
        {"r", 1},
        {"r-r-y.y.-", 0},
        {"r-r-y.y.y.y.-", 0},
        {"r-r-y.y.y.y.y.y.-", 0},
        {"r-r-y.y.y.y.y.y.y.y.-", 0},
        {"ry-", 0},
        {"r.", 0},
        {"r-", 0},
        {"ry-ry-y.y.y.y.y.y.y.y.-", 0},
        {"ry-ry-y-----y-", 0},
        {"ry-ry-y-y-y-y-y-y-y-y.-", 0},
        {"ry-ry-r-----r-", 0},
        {"ry-ry-r.r.r.r.r.r.r.r.-", 0},
        {"ry-ry-r.r.r.r.r.r.r.r.-", 0},
        {"ry-ry-y.y.y.y.y.y.y.y-r-----r-", 0},
        {"ry-ry-y-----y-r-----r-", 0},
        {"ry-ry-y-y-y-y-y-y-y-y-r-----r-", 0},
        {"ry-ry-y.y.y.y.y.y.y.y.r.r.r.r.r.r.r.r.-", 0},
        {"ry-ry-y-----y-r.r.r.r.r.r.r.r.-", 0},
        {"ry-ry-y-y-y-y-y-y-y-y-r.r.r.r.r.r.r.r.-", 0},
    };
    const auto index=static_cast<std::uint32_t>(animation);
    return index<AnimationCount ? &programs[index] : nullptr;
}
}} // namespace picostill::oem
