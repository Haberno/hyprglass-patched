#!/usr/bin/env python3
# Clicking the already-focused workspace pill opens the waypaper-selector
# plugin PANEL (same droopy Noctalia panel the old bar button opened).
# Patches system noctalia-shell QML (root-owned — run with sudo).
# Re-run after every noctalia-shell package update (updates revert it).
# Requires the waypaper-selector plugin to be enabled in plugins.json.
import sys

PILL = "/etc/xdg/quickshell/noctalia-shell/Modules/Bar/Extras/WorkspacePill.qml"
WS   = "/etc/xdg/quickshell/noctalia-shell/Modules/Bar/Widgets/Workspace.qml"

ok = True

# --- WorkspacePill.qml: click handler + screen property + import ----------
src = open(PILL).read()
OLD = """    onClicked: {
      CompositorService.switchToWorkspace(workspace);
    }"""
NEW = """    onClicked: {
      if (workspace.isFocused)
        PluginService.togglePluginPanel("waypaper-selector", pillContainer.pillScreen, pillContainer);
      else
        CompositorService.switchToWorkspace(workspace);
    }"""
# v1 of this patch spawned the waypaper GTK window instead of the panel
OLD_V1 = """    onClicked: {
      if (workspace.isFocused)
        Quickshell.execDetached(["waypaper"]);
      else
        CompositorService.switchToWorkspace(workspace);
    }"""
if NEW in src:
    print("WorkspacePill: already patched")
else:
    if OLD_V1 in src:
        src = src.replace(OLD_V1, OLD, 1)
    if OLD not in src:
        print("ERROR: WorkspacePill click handler not found — upstream changed")
        ok = False
    else:
        src = src.replace(OLD, NEW, 1)
        if "import qs.Services.Noctalia\n" not in src:
            src = src.replace("import qs.Services.Compositor\n",
                              "import qs.Services.Compositor\nimport qs.Services.Noctalia\n", 1)
        if "property var pillScreen" not in src:
            src = src.replace("  required property var workspace\n",
                              "  required property var workspace\n  property var pillScreen: null\n", 1)
        open(PILL, "w").write(src)
        print("patched:", PILL)

# --- Workspace.qml: pass screen into both pill delegates -------------------
src = open(WS).read()
if "pillScreen: root.screen" in src:
    print("Workspace: already patched")
else:
    n = src.count("        workspace: model\n")
    if n != 2:
        print(f"ERROR: expected 2 pill delegates in Workspace.qml, found {n}")
        ok = False
    else:
        src = src.replace("        workspace: model\n",
                          "        workspace: model\n        pillScreen: root.screen\n")
        open(WS, "w").write(src)
        print("patched:", WS)

sys.exit(0 if ok else 1)
