#pragma once

// One explicit per-element glass quad (monitor-global logical coords),
// set at runtime via `hyprctl glassregions`.
struct SGlassRegion {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f, radius = 0.f;

    bool operator==(const SGlassRegion& o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h && radius == o.radius;
    }
};
