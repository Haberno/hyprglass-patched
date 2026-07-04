#!/usr/bin/env python3
"""Carve the HyprGlass shader mask into every backgrounded Noctalia widget.

Widgets currently painting OPAQUE backgrounds block the compositor glass
shader (it renders behind the layer; opaque pixels hide it). This rewrites
each background fill through Color.smartAlpha(<color>, <minAlpha>) so the
element becomes translucent AND its composited alpha over the ~0.4 panel
body crosses hyprglass's mask_threshold 0.5 -> the real refraction shader
renders individually under every element.

Idempotent (marker per file). Writes .pre-glassmask.bak backups.
Run with sudo. Re-run after any noctalia-shell package update.
"""
import shutil, sys

W = "/etc/xdg/quickshell/noctalia-shell/Widgets/"
MARK = "/* glass-mask-carve 2026-07-04 */"

# file -> list of (old, new) exact replacements
PATCHES = {
    "NButton.qml": [
        (
            """      if (!root.enabled)
        return root.outlined ? "transparent" : Qt.lighter(Color.mSurfaceVariant, 1.2);
      if (root.hovered)
        return root.hoverColor;
      return root.outlined ? "transparent" : root.backgroundColor;""",
            """      if (!root.enabled)
        return root.outlined ? "transparent" : Color.smartAlpha(Qt.lighter(Color.mSurfaceVariant, 1.2), 0.45);
      if (root.hovered)
        return Color.smartAlpha(root.hoverColor, 0.55);
      return root.outlined ? "transparent" : Color.smartAlpha(root.backgroundColor, 0.5);""",
        ),
    ],
    "NIconButton.qml": [
        (
            "  property color colorBgHover: Color.mHover",
            "  property color colorBgHover: Color.smartAlpha(Color.mHover, 0.55)",
        ),
    ],
    "NIconButtonHot.qml": [
        (
            "  property color colorBgHover: Color.mHover",
            "  property color colorBgHover: Color.smartAlpha(Color.mHover, 0.55)",
        ),
        (
            "  property color colorBgHot: Color.mPrimary",
            "  property color colorBgHot: Color.smartAlpha(Color.mPrimary, 0.55)",
        ),
    ],
    "NToggle.qml": [
        (
            "    color: root.checked ? Color.mPrimary : Color.mSurface",
            "    color: Color.smartAlpha(root.checked ? Color.mPrimary : Color.mSurface, 0.5)",
        ),
    ],
    "NComboBox.qml": [
        (
            "      color: Color.mSurface\n",
            "      color: Color.smartAlpha(Color.mSurface, 0.5)\n",
        ),
        (
            "        color: Color.mSurfaceVariant",
            "        color: Color.smartAlpha(Color.mSurfaceVariant, 0.6)",
        ),
    ],
    "NSearchableComboBox.qml": [
        (
            "      color: Color.mSurface\n",
            "      color: Color.smartAlpha(Color.mSurface, 0.5)\n",
        ),
        (
            "        color: Color.mSurfaceVariant",
            "        color: Color.smartAlpha(Color.mSurfaceVariant, 0.6)",
        ),
    ],
    "NSpinBox.qml": [
        (
            "    color: Color.mSurfaceVariant",
            "    color: Color.smartAlpha(Color.mSurfaceVariant, 0.5)",
        ),
    ],
    "NTabButton.qml": [
        (
            "  color: root.isHovered ? Color.mHover : (root.checked ? Color.mPrimary : Color.smartAlpha(Color.mSurface))",
            "  color: Color.smartAlpha(root.isHovered ? Color.mHover : (root.checked ? Color.mPrimary : Color.mSurface), 0.5)",
        ),
    ],
    "NCheckbox.qml": [
        (
            "    color: root.checked ? root.activeColor : Color.mSurface",
            "    color: Color.smartAlpha(root.checked ? root.activeColor : Color.mSurface, 0.5)",
        ),
    ],
    "NTextInput.qml": [
        (
            "      color: Color.mSurface\n",
            "      color: Color.smartAlpha(Color.mSurface, 0.5)\n",
        ),
    ],
}

for fname, pairs in PATCHES.items():
    path = W + fname
    src = open(path).read()
    if MARK in src:
        print(f"skip {fname}: already patched")
        continue
    orig = src
    ok = True
    for old, new in pairs:
        if old not in src:
            print(f"ERROR {fname}: pattern not found:\n{old[:90]}...")
            ok = False
            break
        src = src.replace(old, new, 1)
    if not ok:
        sys.exit(1)
    shutil.copy2(path, path + ".pre-glassmask.bak")
    open(path, "w").write(MARK + "\n" + src)
    print(f"patched {fname} ({len(pairs)} fills)")
print("done — restart noctalia to apply")
