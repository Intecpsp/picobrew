// Original Z119 Quantity.h behavior, recovered from 0x401bf40c / 0x400dee30.
#pragma once
#include "zseries/enums.hpp"

namespace zseries { namespace oem {
struct ConsoleServices;
struct Quantity {
    double value;
    Unit unit;
};

// Equality is tested before group membership. Equal unrecognized enum values
// are consequently compatible in the original firmware as well.
bool quantity_units_compatible(Unit from, Unit to) noexcept;

// Empty diagnostics retain the guarded standalone API (false, zero result).
// Bound diagnostics execute the returning assertion and incompatibility log,
// then return true with the OEM zero value. Callback failures return false.
bool quantity_in(const Quantity&, Unit to, double& result,const ConsoleServices* diagnostics=nullptr) noexcept;
}} // namespace zseries::oem
