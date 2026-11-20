#include "engine_internal.hpp"
#include <cfloat>

namespace picobrew { namespace standalone { namespace detail {
namespace {
const char* const baseline[] = {"elapsed_ms", "wort_f", "thermoblock_f", "target_f", "step_seconds",
    "phase", "controller_status", "compartment", "current_step", "flags", "device_state",
    "temperature_f", "pressure_kpa", "battery_v"};
constexpr std::size_t FieldBytes = 74;
void add_value(Point& point, unsigned index, double value, bool valid = true) {
    if (valid && std::isfinite(value) && value > -1e12 && value < 1e12) {
        point.values[index] = value; point.valid |= std::uint64_t(1) << index;
    }
}
bool integer_field(unsigned index) { return index == 0 || (index >= 4 && index <= 10); }
unsigned row_stride(unsigned fields, bool compact) { return fields * 4 - (compact ? 18 : 0); }
unsigned field_offset(unsigned index, bool compact) {
    if (!compact || index < 5) return index * 4;
    return index < 11 ? 20 + index - 5 : 26 + (index - 11) * 4;
}
}
bool layout_create(const Snapshot& snapshot, Layout& layout) {
    if (snapshot.sensor_count > SensorCapacity || snapshot.actuator_count > ActuatorCapacity) return false;
    unsigned count = 14 + snapshot.sensor_count;
    for (std::size_t i = 0; i < snapshot.actuator_count; ++i)
        count += 1 + (snapshot.actuators[i].supports_level ? 1 : 0) + (snapshot.actuators[i].supports_rpm ? 1 : 0);
    if (!layout.allocate(count)) return false;
    layout.count = 14;
    for (unsigned i = 0; i < 14; ++i) copy(layout.fields[i].name, sizeof(layout.fields[i].name), baseline[i]);
    for (std::size_t i = 0; i < snapshot.sensor_count; ++i) {
        const Sensor& sensor = snapshot.sensors[i];
        if (!valid_id(sensor.id) || !valid_text(sensor.label, 32)) return false;
        Field& field = layout.fields[layout.count++];
        ::snprintf(field.name, sizeof(field.name), "sensor_%s", sensor.id);
        copy(field.label, sizeof(field.label), sensor.label); field.unit = sensor.unit; field.kind = sensor.kind;
    }
    const char* suffixes[] = {"enabled", "level", "rpm"};
    for (std::size_t i = 0; i < snapshot.actuator_count; ++i) {
        const Actuator& actuator = snapshot.actuators[i];
        if (!valid_id(actuator.id) || !valid_text(actuator.label, 32)) return false;
        for (unsigned n = 0; n < 3; ++n) {
            if ((n == 1 && !actuator.supports_level) || (n == 2 && !actuator.supports_rpm)) continue;
            Field& field = layout.fields[layout.count++];
            ::snprintf(field.name, sizeof(field.name), "actuator_%s_%s", actuator.id, suffixes[n]);
            copy(field.label, sizeof(field.label), actuator.label);
            field.unit = n == 1 ? Unit::Percent : n == 2 ? Unit::Rpm : Unit::Raw;
            field.kind = n == 2 ? SensorKind::Speed : SensorKind::Other;
        }
    }
    for (unsigned i = 0; i < layout.count; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (!std::strcmp(layout.fields[i].name, layout.fields[j].name)) return false;
    return true;
}
bool layout_save(const Records& records, const char* name, const Layout& layout,
                 std::uint32_t previous, std::uint32_t& generation) {
    if (layout.count < 14 || layout.count > FieldCapacity) return false;
    Bytes bytes(1 + layout.count * FieldBytes); if (!bytes.data) return false;
    bytes.data[0] = layout.count;
    for (unsigned i = 0; i < layout.count; ++i) {
        std::uint8_t* p = bytes.data + 1 + i * FieldBytes;
        const Field& field = layout.fields[i];
        std::memcpy(p, field.name, 40); std::memcpy(p + 40, field.label, 32);
        p[72] = static_cast<unsigned>(field.unit); p[73] = static_cast<unsigned>(field.kind);
    }
    return records.save(name, bytes.data, bytes.size, previous, generation) == RecordResult::Ok;
}
bool layout_load(const Records& records, const char* name, Layout& layout, std::uint32_t generation, unsigned fields) {
    if (fields < 14 || fields > FieldCapacity) return false;
    // Field is precisely the existing stable byte encoding: two bounded byte
    // strings followed by two byte-sized enums. Decode in its final buffer.
    static_assert(sizeof(Field) == FieldBytes && offsetof(Field, unit) == 72 && offsetof(Field, kind) == 73,
                  "history field layout differs from its byte encoding");
    std::uint8_t count = 0; const std::size_t total = 1 + fields * FieldBytes;
    if (records.load_slice(name, 0, &count, 1, total, generation) != RecordResult::Ok ||
        count != fields || !layout.allocate(fields) ||
        records.load_slice(name, 1, reinterpret_cast<std::uint8_t*>(layout.fields), total - 1, total, generation) != RecordResult::Ok)
        return false;
    for (unsigned i = 0; i < layout.count; ++i) {
        const Field& field = layout.fields[i];
        if (!std::memchr(field.name, 0, 40) || !std::memchr(field.label, 0, 32) ||
            static_cast<unsigned>(field.unit) > 5 || static_cast<unsigned>(field.kind) > 5) return false;
        if (i < 14 && std::strcmp(field.name, baseline[i])) return false;
    }
    return true;
}
void layout_fields(Writer& writer, const Layout& layout) {
    writer.text("[");
    for (unsigned i = 0; i < layout.count; ++i) { if (i) writer.text(","); writer.quote(layout.fields[i].name); }
    writer.text("]");
}
void layout_metadata(Writer& writer, const Layout& layout) {
    writer.text("{");
    for (unsigned i = 14; i < layout.count; ++i) {
        const Field& field = layout.fields[i]; if (i > 14) writer.text(",");
        writer.quote(field.name); writer.text(":{\"label\":"); writer.quote(field.label);
        writer.text(",\"unit\":"); writer.quote(unit_name(field.unit));
        writer.text(",\"kind\":"); writer.quote(kind_name(field.kind)); writer.text("}");
    }
    writer.text("}");
}
void point_create(const Snapshot& s, const Layout& layout, std::uint32_t id, std::uint32_t elapsed, Point& p) {
    p.id = id; p.count = layout.count; p.valid = 0;
    add_value(p, 0, elapsed); add_value(p, 1, s.wort_f.value, s.wort_f.valid);
    add_value(p, 2, s.thermoblock_f.value, s.thermoblock_f.valid); add_value(p, 3, s.target_f.value, s.target_f.valid);
    add_value(p, 4, s.step_seconds); add_value(p, 5, s.phase); add_value(p, 6, s.controller_status);
    add_value(p, 7, s.compartment); add_value(p, 8, s.current_step < 0 ? 255 : s.current_step);
    add_value(p, 9, (s.ready ? 1 : 0) | (s.busy ? 2 : 0) | (s.stopped ? 4 : 0) | (s.filter_known && s.filter_inserted ? 8 : 0));
    add_value(p, 10, s.device_state); add_value(p, 11, s.temperature_f.value, s.temperature_f.valid);
    add_value(p, 12, s.pressure_kpa.value, s.pressure_kpa.valid); add_value(p, 13, s.battery_v.value, s.battery_v.valid);
    // Match by stored name, not current array order. A hot-swapped/invalid
    // sensor cannot silently move another channel into the old field's column.
    for (unsigned i = 14; i < layout.count; ++i) {
        const char* name = layout.fields[i].name;
        for (std::size_t n = 0; n < s.sensor_count; ++n) {
            char expected[40]; ::snprintf(expected, sizeof(expected), "sensor_%s", s.sensors[n].id);
            if (!std::strcmp(name, expected)) add_value(p, i, s.sensors[n].reading.value, s.sensors[n].reading.valid);
        }
        for (std::size_t n = 0; n < s.actuator_count; ++n) {
            const Actuator& a = s.actuators[n]; char expected[40];
            ::snprintf(expected, sizeof(expected), "actuator_%s_enabled", a.id);
            if (!std::strcmp(name, expected)) add_value(p, i, a.enabled ? 1 : 0, a.valid);
            ::snprintf(expected, sizeof(expected), "actuator_%s_level", a.id);
            if (!std::strcmp(name, expected)) add_value(p, i, a.level.value, a.level.valid);
            ::snprintf(expected, sizeof(expected), "actuator_%s_rpm", a.id);
            if (!std::strcmp(name, expected)) add_value(p, i, a.rpm.value, a.rpm.valid);
        }
    }
}
bool point_save(const Records& records, unsigned slot, const Point& point) {
    if (slot >= PointCapacity || point.count < 14 || point.count > FieldCapacity) return false;
    char name[12]; ::snprintf(name, sizeof(name), "p%03u", slot / ChunkPoints);
    const unsigned index = slot % ChunkPoints, stride = row_stride(point.count, true);
    // The full legacy stride remains readable, but only this session's actual
    // fields need space. Accessories do not allocate absent brewer channels.
    Bytes bytes(12 + ChunkPoints * point.count * 4); if (!bytes.data) return false;
    std::size_t length = 0; std::uint32_t previous = 0, generation = 0;
    const RecordResult loaded = records.load(name, index ? bytes.data : nullptr, index ? bytes.size : 0, length, previous);
    if (loaded != RecordResult::Ok && loaded != RecordResult::Missing) return false;
    if (index && (loaded != RecordResult::Ok || length < 12 || get32(bytes.data) != point.id ||
                  bytes.data[4] != point.count || bytes.data[5] < index || bytes.data[5] > ChunkPoints || bytes.data[6] > 1 ||
                  length != 12 + bytes.data[5] * row_stride(point.count, bytes.data[6] == 1))) return false;
    // The initial development format stored every column as four bytes. Read
    // it unchanged, and compact its committed prefix only on an append. The
    // six bounded integer columns keep their exact byte values; elapsed time
    // and signed step_seconds remain full-width integers.
    if (index && bytes.data[6] == 0) {
        const unsigned old_stride = row_stride(point.count, false);
        for (unsigned row = 0; row < index; ++row) for (unsigned field = 0; field < point.count; ++field) {
            const std::uint8_t* from = bytes.data + 12 + row * old_stride + field * 4;
            std::uint8_t* to = bytes.data + 12 + row * stride + field_offset(field, true);
            const std::uint32_t value = get32(from);
            if (field >= 5 && field <= 10) { if (value > 255) return false; *to = static_cast<std::uint8_t>(value); }
            else put32(to, value);
        }
    }
    if (!index) { std::memset(bytes.data, 0, bytes.size); put32(bytes.data, point.id); bytes.data[4] = point.count; }
    bytes.data[5] = index + 1; bytes.data[6] = 1;
    std::uint8_t* p = bytes.data + 12 + index * stride;
    for (unsigned i = 0; i < point.count; ++i) {
        std::uint32_t bits = 0x7fc00000u; // one canonical NaN encodes a missing reading
        if (integer_field(i)) bits = i == 4 ? static_cast<std::uint32_t>(static_cast<std::int32_t>(point.values[i])) : static_cast<std::uint32_t>(point.values[i]);
        else if ((point.valid >> i) & 1) { float value = static_cast<float>(point.values[i]); std::memcpy(&bits, &value, 4); }
        if (i >= 5 && i <= 10) {
            if (point.values[i] < 0 || point.values[i] > 255 || point.values[i] != std::floor(point.values[i])) return false;
            p[field_offset(i, true)] = static_cast<std::uint8_t>(bits);
        } else put32(p + field_offset(i, true), bits);
    }
    return records.save(name, bytes.data, 12 + (index + 1) * stride, previous, generation) == RecordResult::Ok;
}
bool point_load(const Records& records, unsigned slot, std::uint32_t id, unsigned fields, Point& point) {
    if (slot >= PointCapacity || fields < 14 || fields > FieldCapacity) return false;
    char name[12]; ::snprintf(name, sizeof(name), "p%03u", slot / ChunkPoints);
    Bytes bytes(12 + ChunkPoints * fields * 4); if (!bytes.data) return false;
    std::size_t length = 0; std::uint32_t generation = 0;
    const unsigned index = slot % ChunkPoints;
    if (records.load(name, bytes.data, bytes.size, length, generation) != RecordResult::Ok ||
        length < 12 || get32(bytes.data) != id || bytes.data[4] != fields ||
        bytes.data[5] <= index || bytes.data[5] > ChunkPoints || bytes.data[6] > 1 ||
        length != 12 + bytes.data[5] * row_stride(fields, bytes.data[6] == 1)) return false;
    point.id = id; point.count = fields; point.valid = 0;
    const bool compact = bytes.data[6] == 1;
    const std::uint8_t* p = bytes.data + 12 + index * row_stride(fields, compact);
    for (unsigned i = 0; i < fields; ++i) {
        const std::uint32_t bits = compact && i >= 5 && i <= 10 ? p[field_offset(i, true)] : get32(p + field_offset(i, compact));
        if (integer_field(i)) add_value(point, i, i == 4 ? static_cast<double>(static_cast<std::int32_t>(bits)) : static_cast<double>(bits));
        else { float value = 0; std::memcpy(&value, &bits, 4); add_value(point, i, value, std::isfinite(value)); }
    }
    return true;
}
void point_json(Writer& writer, const Point& point) {
    writer.text("[");
    for (unsigned i = 0; i < point.count; ++i) {
        if (i) writer.text(",");
        if (!((point.valid >> i) & 1)) writer.text("null");
        else if (integer_field(i)) writer.format("%lld", static_cast<long long>(point.values[i]));
        else writer.number(point.values[i]);
    }
    writer.text("]");
}
}}}
