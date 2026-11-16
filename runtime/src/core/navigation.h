// Directional (gamepad/keyboard) navigation between selectables (FR-UI-51).
//
// Unity's automatic navigation searches every selectable in the scene, so a
// press can move the selection to something behind the open menu, off screen
// or scrolled out of view; with wrap-around it takes the selectable farthest
// away in the opposite direction, anywhere. The engine asks this module whether
// Unity's pick is acceptable and, if not, which one to take instead. Plain data
// only, so it is testable without the game.
#pragma once
#include <vector>

#include "ui.h"

namespace ltm::nav {

// UnityEngine.UI.Navigation.Mode
enum class Mode { None = 0, Horizontal = 1, Vertical = 2, Automatic = 3, Explicit = 4 };

// Direction of the press, in the canvas plane (y up).
enum class Dir { Left, Right, Up, Down };

struct Item {
    int id = 0;             // identity (instance id)
    int scope = 0;          // the menu it belongs to (nearest GraphicRaycaster); 0 = none
    Bounds rect;            // in the root canvas's space
    bool has_viewport = false;
    Bounds viewport;        // the clipping rect (RectMask2D/Mask) it sits in, same space
    int scroll = 0;         // the scroll view it belongs to; 0 = none
    Mode mode = Mode::Automatic;
    bool wrap = false;
    bool interactable = true;
};

// Whether navigation from `self` is Unity's automatic kind (the only kind the
// mod corrects; explicit links are the game's own choice).
bool corrects(const Item &self);

// Whether `target` is an acceptable destination when moving from `self`: in the
// same menu, interactable, on screen and not clipped away (unless both are in
// the same scroll view, so lists still scroll one entry at a time).
bool valid(const Item &self, const Item &target, const Bounds &screen);

// Unity's own choice (Selectable.FindSelectable), restricted to valid targets:
// the nearest one in the direction, else with wrap-around the farthest one in
// the opposite direction. Index into `items`, or -1 to stay where it is.
int pick(const Item &self, Dir dir, const std::vector<Item> &items, const Bounds &screen);

// The neighbour of entry `index` in a list of `count` entries, wrapping at the
// ends (the Save/Load slots); -1 when there is no other entry.
int step_wrapping(int index, int count, bool forward);

// Grid navigation with wrap-around (the evidence items): from entry `self`,
// the next entry in its row (left/right) or column (up/down), wrapping from one
// end of that row or column to the other. Rows and columns are found from the
// entries' rects: same row when the vertical centres differ by less than half
// self's height, same column likewise by width. -1 when there is no other entry.
int grid_step(const std::vector<Bounds> &cells, int self, Dir dir);

}  // namespace ltm::nav
