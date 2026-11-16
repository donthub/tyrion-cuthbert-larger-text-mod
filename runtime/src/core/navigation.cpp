#include "navigation.h"

#include <limits>

namespace ltm::nav {

namespace {

Vec2 center(const Bounds &b) { return {(b.x_min + b.x_max) / 2.0f, (b.y_min + b.y_max) / 2.0f}; }

bool inside(Vec2 p, const Bounds &b) { return p.x >= b.x_min && p.x <= b.x_max && p.y >= b.y_min && p.y <= b.y_max; }

Vec2 vector(Dir d) {
    switch (d) {
        case Dir::Left: return {-1, 0};
        case Dir::Right: return {1, 0};
        case Dir::Up: return {0, 1};
        case Dir::Down: return {0, -1};
    }
    return {0, 0};
}

}  // namespace

bool corrects(const Item &self) {
    return self.mode == Mode::Horizontal || self.mode == Mode::Vertical || self.mode == Mode::Automatic;
}

bool valid(const Item &self, const Item &target, const Bounds &screen) {
    if (target.id == self.id || !target.interactable || target.mode == Mode::None) return false;
    if (target.scope != self.scope) return false;
    Vec2 c = center(target.rect);
    if (!inside(c, screen)) return false;
    if (target.has_viewport && !inside(c, target.viewport))
        return target.scroll != 0 && target.scroll == self.scroll;
    return true;
}

int pick(const Item &self, Dir dir, const std::vector<Item> &items, const Bounds &screen) {
    // As Selectable.FindSelectable: start on the edge of self's rect facing the
    // direction, score by dot / distance² ahead, -dot * distance² behind (wrap).
    Vec2 d = vector(dir);
    Vec2 c = center(self.rect);
    Vec2 start{c.x + d.x * (self.rect.x_max - self.rect.x_min) / 2.0f, c.y + d.y * (self.rect.y_max - self.rect.y_min) / 2.0f};
    bool wrap = self.wrap && (self.mode == Mode::Horizontal || self.mode == Mode::Vertical);
    float best = -std::numeric_limits<float>::infinity(), best_far = best;
    int pick_near = -1, pick_far = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item &it = items[i];
        if (!valid(self, it, screen)) continue;
        Vec2 p = center(it.rect);
        Vec2 v{p.x - start.x, p.y - start.y};
        float dot = d.x * v.x + d.y * v.y;
        float sq = v.x * v.x + v.y * v.y;
        if (wrap && dot < 0) {
            float score = -dot * sq;
            if (score > best_far) {
                best_far = score;
                pick_far = static_cast<int>(i);
            }
            continue;
        }
        if (dot <= 0) continue;
        float score = dot / sq;
        if (score > best) {
            best = score;
            pick_near = static_cast<int>(i);
        }
    }
    return pick_near >= 0 ? pick_near : wrap ? pick_far : -1;
}

int step_wrapping(int index, int count, bool forward) {
    if (count < 2 || index < 0 || index >= count) return -1;
    return forward ? (index + 1) % count : (index + count - 1) % count;
}

int grid_step(const std::vector<Bounds> &cells, int self, Dir dir) {
    if (self < 0 || self >= static_cast<int>(cells.size())) return -1;
    const Bounds &me = cells[static_cast<size_t>(self)];
    Vec2 c = center(me);
    bool horizontal = dir == Dir::Left || dir == Dir::Right;
    float tolerance = horizontal ? (me.y_max - me.y_min) / 2.0f : (me.x_max - me.x_min) / 2.0f;
    // Position along the line, increasing in the direction of the press.
    auto along = [&](Vec2 p) {
        switch (dir) {
            case Dir::Left: return -p.x;
            case Dir::Right: return p.x;
            case Dir::Up: return p.y;
            case Dir::Down: return -p.y;
        }
        return 0.0f;
    };
    float here = along(c);
    int next = -1, first = -1;
    float next_at = 0, first_at = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (static_cast<int>(i) == self) continue;
        Vec2 p = center(cells[i]);
        float off = horizontal ? p.y - c.y : p.x - c.x;
        if (off > tolerance || off < -tolerance) continue;  // another row/column
        float at = along(p);
        if (at > here && (next < 0 || at < next_at)) {
            next = static_cast<int>(i);
            next_at = at;
        }
        if (first < 0 || at < first_at) {  // the far end, for wrapping
            first = static_cast<int>(i);
            first_at = at;
        }
    }
    return next >= 0 ? next : first;
}

}  // namespace ltm::nav
