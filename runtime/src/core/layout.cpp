#include "layout.h"

#include <algorithm>
#include <cmath>

namespace ltm::layout {

float round_to(float value, float step) { return std::round(value / step) * step; }

float ceil_to(float value, float step) {
    // Tolerate float noise just above a multiple (e.g. 147.00001).
    return std::ceil(value / step - 1e-4f) * step;
}

float scaled_font(float size, double factor) {
    return static_cast<float>(std::round(size * factor * 100.0) / 100.0);
}

float dialogue_text_height(const DialoguePanel &o, double scale) {
    return ceil_to(static_cast<float>(o.text_height * scale * scale), 10.0f);
}

float dialogue_panel_height(const DialoguePanel &o, double scale) {
    float s = static_cast<float>(scale);
    float text = dialogue_text_height(o, scale);
    // The original panel plus the extra text height, a little breathing room,
    // and room for the taller info row.
    float grown = o.panel_height + text - o.text_height + 20.0f + 80.0f * (s - 1.0f);
    // The info row starts |info_top_offset| / panel_height below the top and
    // moves with the panel's vertical stretch. The dialogue text is centred.
    // Its top must stay kInfoToDialogueGap below the (taller) info row:
    //   H/2 + offset + text/2 + gap <= H * (1 - r) - info*s
    //   H >= (info*s + offset + text/2 + gap) / (1/2 - r)
    float r = std::fabs(o.info_top_offset) / o.panel_height;
    float k = 0.5f - r;
    float minimum = grown;
    if (k > 0.01f) {
        minimum = (o.info_height * s + o.text_offset_y + text / 2.0f + kInfoToDialogueGap) / k;
    }
    return std::max(grown, ceil_to(minimum, 10.0f));
}

float dialogue_panel_top(const DialoguePanel &o, double scale) {
    return o.panel_bottom + dialogue_panel_height(o, scale);
}

RectData stretch_horizontally(const RectData &child, float parent_width) {
    RectData r = child;
    float x = child.anchoredPosition.x;
    float w = child.sizeDelta.x;
    float p = child.pivot.x;
    r.anchorMin.x += (x - p * w) / parent_width;
    r.anchorMax.x += (x + (1.0f - p) * w) / parent_width;
    r.anchoredPosition.x = 0;
    r.sizeDelta.x = 0;
    return r;
}

float image_bottom(const RectData &image) {
    // anchor y 0..1 and pivot 0.5: centre = H/2 + pos, height = H + size.
    return image.anchoredPosition.y - image.sizeDelta.y / 2.0f;
}

RectData clear_image_above(const RectData &image, float panel_top) {
    RectData r = image;
    float adjustment = std::max(0.0f, panel_top + kImageGap - image_bottom(image));
    // Raise the bottom edge only: shrinking by `adjustment` and moving the
    // centre up by half of it keeps the top edge where it was.
    r.sizeDelta.y -= adjustment;
    r.anchoredPosition.y += adjustment / 2.0f;
    return r;
}

TmpSize scale_tmp(const TmpSize &o, double factor, bool keep_minimum) {
    TmpSize t = o;
    float base = o.auto_size ? o.max : o.font_size;
    float target = scaled_font(base, factor);
    t.font_size = target;
    t.max = target;
    if (!keep_minimum) t.min = std::min(target, scaled_font(o.min, factor));
    return t;
}

TmpSize scale_heading(const TmpSize &o, double factor) {
    TmpSize t;
    float base = o.auto_size ? o.max : o.font_size;
    t.font_size = scaled_font(base, factor);
    t.max = t.font_size;
    t.min = base;
    t.auto_size = true;
    return t;
}

float heading_height(float original_height, double factor, float panel_top_anchor) {
    float wanted = original_height * static_cast<float>(factor);
    // Keep a small margin to the screen edge.
    float available = (1.0f - panel_top_anchor) * kReferenceHeight - 14.0f;
    return std::max(original_height, std::min(wanted, available));
}

}  // namespace ltm::layout
