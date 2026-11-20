#include "zseries/quantity.hpp"
#include "zseries/console.hpp"
#include <cstdio>

namespace zseries { namespace oem {
bool quantity_units_compatible(Unit from, Unit to) noexcept {
    const auto a = static_cast<std::uint32_t>(from);
    const auto b = static_cast<std::uint32_t>(to);
    return a == b || (a < 2 && b < 2) || (a - 2u < 2 && b - 2u < 2) ||
           (a - 4u < 2 && b - 4u < 2);
}

bool quantity_in(const Quantity& quantity, Unit to, double& result,const ConsoleServices* diagnostics) noexcept {
    if (!quantity_units_compatible(quantity.unit, to)) {
        result = 0;
        if(!diagnostics||!diagnostics->assertion||!diagnostics->assertion(diagnostics->context,"Quantity.h",0x7d,"areCompatible(_unit, unit)"))return false;
        char message[128];::snprintf(message,sizeof(message),"getValueAs(%s) incompatible with %s",to_string(to),to_string(quantity.unit));
        return diagnostics->log&&diagnostics->log(diagnostics->context,ConsoleLevel::Error,"QTY","Quantity.h",0x85,message);
    }
    if (quantity.unit == to) { result = quantity.value; return true; }
    switch (quantity.unit) {
        case Unit::Liter: result = quantity.value / 3.78541; break;
        case Unit::Gallon: result = quantity.value * 3.78541; break;
        case Unit::Gram: result = quantity.value / 28.349523125; break;
        case Unit::Ounce: result = quantity.value * 28.349523125; break;
        case Unit::Celsius: result = quantity.value * 1.8 + 32; break;
        case Unit::Fahrenheit: result = (quantity.value - 32) / 1.8; break;
        default: result = 0; return false; // unreachable after the compatibility check
    }
    return true;
}
}} // namespace zseries::oem
