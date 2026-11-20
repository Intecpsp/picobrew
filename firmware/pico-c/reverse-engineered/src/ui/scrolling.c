/* C-specific scrolling state, absent from the archived S/Pro UI.
 * OEM main entries: start20004bd0, update20009974, clear200050a8.
 */
#include "ui_internal.h"

void pico_display_start_scrolling(const char *text, void (*redraw)(void))
{
    PicoScrollingText *s = &pico_ui_active->scrolling;
    /* Explicit portable repair: the OEM asserts nonfatally then uses strcpy.
     * Keep invalid callers from overrunning the recovered252-byte field.
     */
    if (!text) {
        s->enabled = 0;
        return;
    }
    s->redraw = redraw;
    size_t length = strlen(text);
    if (length >= sizeof s->text)
        length = sizeof s->text - 1;
    memcpy(s->text, text, length);
    s->text[length] = 0;
    s->length = (int32_t)length;
    s->row = s->pixel_offset = s->character_offset = s->frame = 0;
    s->deadline_ms = 0;
    s->enabled = 1;
}

void pico_display_stop_scrolling(void)
{
    pico_ui_active->scrolling.enabled = 0;
}

void pico_display_clear_scrolling(void)
{
    pico_display_stop_scrolling();
    pico_display_clear();
}

void pico_display_update_scrolling(void)
{
    PicoScrollingText *s = &pico_ui_active->scrolling;
    uint32_t now = ui_millis();
    /* Preserve the observed unsigned absolute-deadline comparison, including
     * its rollover behavior; do not replace it with interval arithmetic.
     */
    if (!s->enabled || now <= s->deadline_ms)
        return;
    const char *start = s->text;
    if (s->character_offset >= 0 && (size_t)s->character_offset < sizeof s->text)
        start += s->character_offset;
    for (unsigned i = 0; i < 20; i++) {
        s->visible[i] = *start;
        if (*start)
            start++;
    }
    s->visible[20] = 0;
    PicoDisplay *d = &pico_ui_active->display;
    d->wrap = 0;
    if (s->redraw)
        pico_display_clear();
    else
        pico_display_clear_row((uint8_t)s->row);
    /* OEM writes this expression into cursor_x and sets cursor_y=0. */
    pico_graphics_cursor(d, (int16_t)((uint32_t)s->row * 16u - (uint32_t)s->pixel_offset), 0);
    pico_graphics_text(d, s->visible);
    if (s->redraw)
        s->redraw();
    pico_display_flush();
    s->frame = (int32_t)((uint32_t)s->frame + 1u);
    int32_t hold_ticks = !s->tick_ms ? 0 :
        (s->hold_ms == INT32_MIN && s->tick_ms == -1 ? INT32_MIN : s->hold_ms / s->tick_ms);
    if (hold_ticks < s->frame)
        s->pixel_offset = (int32_t)((uint32_t)s->pixel_offset + 1u);
    if (s->pixel_offset > 6) {
        s->pixel_offset = 0;
        s->character_offset = (int32_t)((uint32_t)s->character_offset + 1u);
    }
    if (s->character_offset >= s->length) {
        s->character_offset = 0;
        s->frame = 0;
    }
    s->deadline_ms = now + (uint32_t)s->tick_ms;
}
