// Typed connections between recovered Runtime, Core and Head event services.
#pragma once
#include "zseries/core_flow.hpp"
#include "zseries/head_events.hpp"

namespace zseries { namespace oem {

// RuntimeServices::event uses two semantic arguments: previous/current for a
// transition, notification/unused for Notification, and neither for Update.
CoreRuntimeEvent runtime_event_payload(RuntimeEventType, std::uint32_t first,
                                       std::uint32_t second) noexcept;

// The application RuntimeServices::event binding supplies its actual Core.
// A pending event retains ownership of the one-slot mailbox; the next is
// dropped, matching original EventQ behavior. No out-of-band payload is used.
bool post_runtime_event(CoreFlow&, RuntimeEventType, std::uint32_t first,
                        std::uint32_t second) noexcept;

HeadRuntimeEvent head_runtime_event(const CoreRuntimeEvent&) noexcept;

}} // namespace zseries::oem
