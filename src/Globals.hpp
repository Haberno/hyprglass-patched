#pragma once

#include "GlassLayerSurface.hpp"
#include "GlassRegion.hpp"
#include "PluginConfig.hpp"
#include "ShaderManager.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class CGlassDecoration;


struct SGlobalState {
    std::vector<WP<CGlassDecoration>> decorations;
    CShaderManager                    shaderManager;
    SPluginConfig                     config;

    // User-defined presets (populated from config keyword, swapped in on configReloaded)
    std::unordered_map<std::string, SCustomPreset> customPresets;

    // Per-namespace explicit glass regions (monitor-global logical coords).
    // When non-empty for a layer's namespace, each region gets its OWN glass
    // quad (window-grade bezel/rim/corner optics) instead of one layer-box
    // quad + alpha mask. Updated at runtime via `hyprctl glassregions`.
    std::unordered_map<std::string, std::vector<SGlassRegion>> layerNamespaceRegions;

    // Shared blur temp framebuffer (reused across all decorations since they render sequentially)
    SP<Render::IFramebuffer> blurTempFramebuffer;

    // Layer surface glass state (one per tracked layer, keyed by raw pointer).
    // shared_ptr so CGlassLayerPassElement can hold a copy that survives map erasure mid-frame.
    std::unordered_map<Desktop::View::CLayerSurface*, std::shared_ptr<CGlassLayerSurface>> layerSurfaces;

    // Parsed namespace whitelist (empty = match all when layers enabled)
    std::unordered_set<std::string> layerNamespaceFilter;
    // Parsed namespace blacklist (always excluded, takes priority over whitelist)
    std::unordered_set<std::string> layerNamespaceExclude;
    // Per-namespace preset overrides (namespace → preset name)
    std::unordered_map<std::string, std::string> layerNamespacePresets;
    // Per-namespace mask alpha threshold (namespace → threshold, default 0.001)
    std::unordered_map<std::string, float> layerNamespaceMaskThresholds;
    // Namespaces that re-sample the backdrop every frame instead of using the
    // cached sample (needed when animated content, e.g. a live wallpaper,
    // renders below the glass layer without bumping the scene generation)
    std::unordered_set<std::string> layerNamespaceLive;
    // Per-namespace content contrast strength (0..1): recolor the layer's
    // rendered content per-pixel toward the inverse/contrast of its local
    // backdrop. Runtime-set via `hyprctl glassregions <ns> contrast <v>`.
    std::unordered_map<std::string, float> layerNamespaceContentContrast;
    // Namespaces that additionally get one glass quad spanning the whole layer,
    // drawn beneath their per-element quads. A client publishing sub-shapes over
    // ext-background-effect-v1 cannot ask for this itself: that protocol carries
    // a pixman region, so a layer-sized rect would swallow the element rects
    // sitting inside it. Runtime-set via `hyprctl glassregions <ns> layerpane 1`.
    std::unordered_set<std::string> layerNamespaceLayerPane;

    // Per-monitor generation counter, incremented when the scene behind layers
    // changes on that monitor. Layer surfaces compare to their cached value to
    // skip redundant blur work. Per-monitor avoids cross-monitor feedback loops
    // where re-sampling on an idle monitor captures its own stale glass output.
    // Keyed by MONITORID rather than CMonitor* so the code builds on both
    // Hyprland <= 0.55.x (global CMonitor) and git (Monitor::CMonitor), and a
    // stale entry can never alias a reallocated monitor object.
    std::unordered_map<MONITORID, uint64_t> sceneGeneration;

    uint64_t getSceneGeneration(const PHLMONITOR& mon) const {
        if (!mon)
            return 0;
        auto it = sceneGeneration.find(mon->m_id);
        return it != sceneGeneration.end() ? it->second : 0;
    }
    void bumpSceneGeneration(const PHLMONITOR& mon) {
        if (mon)
            sceneGeneration[mon->m_id]++;
    }

    // renderLayer hook
    CFunctionHook* renderLayerHook = nullptr;
};

using Render::GL::g_pHyprOpenGL;

inline HANDLE                        PHANDLE = nullptr;
inline std::unique_ptr<SGlobalState> g_pGlobalState;

inline constexpr std::string_view PLUGIN_NAME        = "hyprglass";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "Apple-style Liquid Glass effect";
inline constexpr std::string_view PLUGIN_AUTHOR      = "Hyprnux";
inline constexpr std::string_view PLUGIN_VERSION     = "1.0.0";
