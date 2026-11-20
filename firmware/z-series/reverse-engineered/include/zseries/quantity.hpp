// Original Z119 Quantity.h behavior, recovered from 0x401bf40c / 0x400dee30.
#pragma once
#include "zseries/enums.hpp"

namespace zseries { namespace oem {
struct Quantity {
    double value;
    Unit unit;
};

// Equality is tested before group membership. Equal unrecognized enum values
// are consequently compatible in the original firmware as well.
bool quantity_units_compatible(Unit from, Unit to) noexcept;

// False reports the original returning Quantity.h:0x7d assertion. The OEM
// continues with a zero result on that path; callers choose their error policy.
bool quantity_in(const Quantity&, Unit to, double& result) noexcept;
}} // namespace zseries::oem
