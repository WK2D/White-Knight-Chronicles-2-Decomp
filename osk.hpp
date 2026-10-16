#pragma once
#include <cstdint>
#include <string>

// Platform UI receives host UTF-16, never guest pointers. Completion is deferred
// until the guest pumps sysutil. A generation prevents stale windows completing
// a later keyboard request.
bool wkc2_osk_ui_open(uint64_t generation, const std::u16string& prompt,
                      const std::u16string& initial, uint32_t limit, bool visible);
void wkc2_osk_ui_close(uint64_t generation);
void wkc2_osk_ui_clear(uint64_t generation);
void wkc2_osk_ui_show(uint64_t generation, bool visible);
bool wkc2_osk_complete(uint64_t generation, uint32_t result,
                       const std::u16string& text);
