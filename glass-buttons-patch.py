#!/usr/bin/env python3
"""Add a liquid-glass edge (hairline white border + inset top highlight)
to Noctalia button/card widgets. Idempotent; writes .pre-glass.bak backups.
Run with sudo. Re-run after any noctalia-shell package update."""
import shutil, sys

W = "/etc/xdg/quickshell/noctalia-shell/Widgets/"
MARK = "// liquid-glass edge"

# QML child injected into each widget's background Rectangle: transparent
# fill, 1px white hairline, plus an inner 1px-inset rect whose vertical
# gradient fakes a small white inset box-shadow along the top edge.
OVERLAY = """{ind}{mark} (added 2026-07-04, see restore notes)
{ind}Rectangle {{
{ind}  anchors.fill: parent
{ind}  radius: parent.radius
{ind}  color: "transparent"
{ind}  border.width: 1
{ind}  border.color: Qt.rgba(1, 1, 1, 0.30)
{ind}  Rectangle {{
{ind}    anchors.fill: parent
{ind}    anchors.margins: 1
{ind}    radius: Math.max(0, parent.radius - 1)
{ind}    gradient: Gradient {{
{ind}      GradientStop {{ position: 0.0; color: Qt.rgba(1, 1, 1, 0.16) }}
{ind}      GradientStop {{ position: 0.30; color: Qt.rgba(1, 1, 1, 0.0) }}
{ind}    }}
{ind}  }}
{ind}}}
"""

# file -> line to insert AFTER (first occurrence; unique enough per file)
TARGETS = {
    "NButton.qml":        "radius: root.buttonRadius",
    "NIconButton.qml":    "border.width: Style.borderS",
    "NIconButtonHot.qml": "border.width: Style.borderS",
    "NBox.qml":           "border.width: Style.borderS",
}

for fname, anchor in TARGETS.items():
    path = W + fname
    src = open(path).read()
    if MARK in src:
        print(f"skip {fname}: already patched")
        continue
    lines = src.split("\n")
    for i, line in enumerate(lines):
        if anchor in line:
            ind = line[: len(line) - len(line.lstrip())]
            block = OVERLAY.format(ind=ind, mark=MARK).rstrip("\n")
            lines.insert(i + 1, block)
            break
    else:
        print(f"ERROR {fname}: anchor not found: {anchor}"); sys.exit(1)
    shutil.copy2(path, path + ".pre-glass.bak")
    open(path, "w").write("\n".join(lines))
    print(f"patched {fname}")
print("done — restart noctalia to apply")
