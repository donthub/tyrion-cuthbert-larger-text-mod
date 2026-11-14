// Pure layout calculations. All values are in the game's 1920x1080 reference
// canvas units and are derived from the game's original values (passed in),
// never from values the mod already changed.
#pragma once
#include "ui.h"

namespace ltm::layout {

// The canvas reference height the game's UI is authored for.
constexpr float kReferenceHeight = 1080.0f;
// Gap between a story image and the dialogue panel in the original layout.
constexpr float kImageGap = 10.0f;
// 20 px at 1440p = 15 reference units: the toolbar moves up by this much.
constexpr float kToolbarRaise = 20.0f * 1080.0f / 1440.0f;
// Screen fraction left free on each side of the widened dialogue panel.
constexpr float kDialogueSideMargin = 0.025f;
// Space kept between the character-info row and the first dialogue line.
constexpr float kInfoToDialogueGap = 12.0f;

float round_to(float value, float step);  // nearest multiple
float ceil_to(float value, float step);   // next multiple up

// Original geometry of a dialogue-style panel (Container + children).
struct DialoguePanel {
    float panel_height = 240;        // Container sizeDelta.y
    float panel_bottom = 50;         // Container anchoredPosition.y (bottom pivot, bottom anchor)
    float text_height = 70;          // Dialogue sizeDelta.y
    float text_offset_y = -10;       // Dialogue anchoredPosition.y (anchored at the vertical middle)
    float info_top_offset = -40;     // Character Info anchoredPosition.y (from the panel top)
    float info_height = 25.5f;       // Character Info sizeDelta.y
};

// Text area height: more wrapping needs roughly scale^2 as much room.
float dialogue_text_height(const DialoguePanel &o, double scale);
// Panel height that fits the taller text and still clears the info row.
float dialogue_panel_height(const DialoguePanel &o, double scale);
// Top edge of the panel above the canvas bottom, after scaling.
float dialogue_panel_top(const DialoguePanel &o, double scale);

// Converts a child's horizontal placement into anchors relative to a parent of
// the given original width, so the child keeps its position and proportion
// when the parent is resized (FR-UI-4). Only x components change.
RectData stretch_horizontally(const RectData &child, float parent_width);

// Story image: vertically stretched rect (anchor y 0..1, pivot y 0.5) whose
// bottom edge must clear `panel_top` by `kImageGap`; keeps the top margin.
RectData clear_image_above(const RectData &image, float panel_top);
float image_bottom(const RectData &image);  // distance of the bottom edge above the canvas bottom

// Enlarged TMP font sizes.
struct TmpSize {
    float font_size = 0, min = 0, max = 0;
    bool auto_size = false;
};
// Grows the text by `factor`. With keep_minimum the auto-size lower bound
// stays as it was (long entries may still shrink to fit).
TmpSize scale_tmp(const TmpSize &original, double factor, bool keep_minimum);
// Notification heading: auto-sizes between its original size and the enlarged
// size, so it never shrinks below the original (FR-UI-31).
TmpSize scale_heading(const TmpSize &original, double factor);
// Heading height: grows with the scale but stays within the space above the
// panel so the heading never extends above the screen.
float heading_height(float original_height, double factor, float panel_top_anchor);

// Rounds a font size to 2 decimals like the original mod.
float scaled_font(float size, double factor);

}  // namespace ltm::layout
