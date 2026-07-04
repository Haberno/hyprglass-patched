#include "GlassLayerSurface.hpp"
#include "BuiltInPresets.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <hyprland/src/desktop/Workspace.hpp>
#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>

static CBox transformedLayerBox(CBox pixelBox, PHLMONITOR monitor) {
    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    pixelBox.transform(transform, monitor->m_transformedSize.x, monitor->m_transformedSize.y).noNegativeSize().round();
    return pixelBox;
}

CGlassLayerSurface::CGlassLayerSurface(PHLLS layerSurface)
    : m_layerSurface(layerSurface) {
}

CGlassLayerSurface::~CGlassLayerSurface() {
    // Damage the area where glass was last drawn so the compositor
    // re-renders it without the glass effect (prevents ghost artifacts).
    if (g_pHyprRenderer && m_lastSize.x > 0 && m_lastSize.y > 0 &&
        std::isfinite(m_lastPosition.x) && std::isfinite(m_lastPosition.y) &&
        std::isfinite(m_lastSize.x) && std::isfinite(m_lastSize.y)) {
        auto box = CBox{m_lastPosition, m_lastSize};
        box.expand(GlassRenderer::SAMPLE_PADDING_PX).noNegativeSize();
        if (box.w > 0.0 && box.h > 0.0)
            g_pHyprRenderer->damageBox(box);
    }
}

bool CGlassLayerSurface::resolveThemeIsDark() const {
    try {
        const auto& config = g_pGlobalState->config;
        const auto theme = readStringConfig(config.defaultTheme);
        if (!theme.empty())
            return theme != "light";
    } catch (...) {}

    return true;
}

std::string CGlassLayerSurface::resolvePresetName() const {
    try {
        // Per-namespace preset override (highest priority)
        const auto layerSurface = m_layerSurface.lock();
        if (layerSurface) {
            const auto& nsPresets = g_pGlobalState->layerNamespacePresets;
            auto it = nsPresets.find(layerSurface->m_namespace);
            if (it != nsPresets.end())
                return it->second;
        }

        const auto& config = g_pGlobalState->config;

        // Layer-wide preset override
        const auto layerPreset = readStringConfig(config.layersPreset);
        if (!layerPreset.empty())
            return std::string(layerPreset);

        // Fall back to global default preset
        const auto defaultPreset = readStringConfig(config.defaultPreset);
        if (!defaultPreset.empty())
            return std::string(defaultPreset);
    } catch (...) {}

    return "default";
}

PHLLS CGlassLayerSurface::getLayerSurface() const {
    return m_layerSurface.lock();
}

void CGlassLayerSurface::damageIfMoved() {
    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    const auto currentPosition = layerSurface->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const auto currentSize     = layerSurface->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    if (currentSize.x <= 0.0 || currentSize.y <= 0.0 ||
        !std::isfinite(currentPosition.x) || !std::isfinite(currentPosition.y) ||
        !std::isfinite(currentSize.x) || !std::isfinite(currentSize.y))
        return;

    const bool isAnimating = layerSurface->positionAnimation()->isBeingAnimated() ||
                             layerSurface->sizeAnimation()->isBeingAnimated() ||
                             layerSurface->alpha()[Desktop::View::LS_ALPHA_FADE]->isBeingAnimated() ||
                             !layerSurface->m_mapped;

    const bool moved = currentPosition != m_lastPosition || currentSize != m_lastSize;

    if (moved || isAnimating) {
        m_lastPosition  = currentPosition;
        m_lastSize      = currentSize;

        auto box = CBox{currentPosition, currentSize};
        const auto monitor = layerSurface->m_monitor.lock();
        const float scale = monitor ? monitor->m_scale : 1.0f;
        box.expand(GlassRenderer::SAMPLE_PADDING_PX / scale).noNegativeSize();
        if (box.w > 0.0 && box.h > 0.0)
            g_pHyprRenderer->damageBox(box);

        if (monitor)
            g_pGlobalState->bumpSceneGeneration(monitor);
    }
}

void CGlassLayerSurface::sampleAndRedirect(PHLMONITOR monitor, float alpha) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    auto source = g_pHyprRenderer->m_renderData.currentFB;
    if (!source)
        return;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox transformBox = transformedLayerBox(*layerBox, monitor);

    // Decide whether we need to re-sample and re-blur the background.
    // When only the layer surface content changed (e.g. waybar clock tick)
    // but no window moved behind us, we reuse the cached blurred background.
    // This skips the most expensive GPU work (blit + 6 blur passes).
    const uint64_t currentGeneration = g_pGlobalState->getSceneGeneration(monitor);
    const auto activeWs = monitor->m_activeWorkspace;
    const bool isAnimating = layerSurface->positionAnimation()->isBeingAnimated() ||
                             layerSurface->sizeAnimation()->isBeingAnimated() ||
                             layerSurface->alpha()[Desktop::View::LS_ALPHA_FADE]->isBeingAnimated() ||
                             (activeWs && activeWs->m_renderOffset->isBeingAnimated());
    // Live namespaces (e.g. glass over an animated wallpaper) re-sample every
    // frame: nothing bumps the scene generation when only a background layer's
    // content changes, so the cache would freeze the backdrop.
    const bool liveSample = g_pGlobalState->layerNamespaceLive.contains(layerSurface->m_namespace);
    const bool backgroundChanged = !m_hasCachedSample ||
                                   currentGeneration != m_lastSceneGeneration ||
                                   isAnimating || liveSample;

    if (!layerSurface->m_mapped) {
        // During fade-out, re-sampling captures stale pixels. Reuse cached sample.
        if (!m_hasCachedSample)
            return;
    } else if (backgroundChanged) {
        const bool isDark          = resolveThemeIsDark();
        const std::string preset   = resolvePresetName();
        const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

        float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

        GlassRenderer::sampleBackground(m_sampleFramebuffer, source, transformBox, m_samplePaddingRatio, downscale);

        float blurRadius     = blurStrength * 12.0f / downscale;
        int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        if (blurRadius > 0.05f)
            GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, source);

        m_hasCachedSample      = true;
        m_lastSceneGeneration  = currentGeneration;
    }
    // else: background unchanged, reuse cached blur — skip 7 GPU operations

    // Redirect surface rendering to a temp FBO cleared to transparent.
    // The original renderLayer (called between pre/post elements) will render
    // the surface into this FBO. compositeAndRestore uses its alpha as a mask.
    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    // In FP16/HDR mode, the source FB uses RGBA16F which has full alpha precision.
    // Use the source format to avoid clipping HDR color values.
    // In SDR mode, force ARGB8888 because monitor FBOs (XRGB2101010 etc.) have
    // limited/no alpha, which would quantize mask values and break the discard.
    DRMFormat tempFormat = (monitor->useFP16()) ? source->m_drmFormat : DRM_FORMAT_ARGB8888;

    if (!m_surfaceTempFramebuffer)
        m_surfaceTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-layer-temp");

    if (m_surfaceTempFramebuffer->m_size.x != monitorWidth || m_surfaceTempFramebuffer->m_size.y != monitorHeight ||
        m_surfaceTempFramebuffer->m_drmFormat != tempFormat)
        m_surfaceTempFramebuffer->alloc(monitorWidth, monitorHeight, tempFormat);

    m_savedCurrentFB = source;

    g_pHyprRenderer->m_renderData.currentFB = m_surfaceTempFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_surfaceTempFramebuffer.get())->getFBID());

    CBox clearBox = transformBox;
    clearBox.expand(GlassRenderer::SAMPLE_PADDING_PX);
    clearBox = clearBox.intersection(CBox{0.0, 0.0, static_cast<double>(monitorWidth), static_cast<double>(monitorHeight)}).noNegativeSize().round();

    if (std::isfinite(clearBox.x) && std::isfinite(clearBox.y) && std::isfinite(clearBox.w) && std::isfinite(clearBox.h) &&
        clearBox.w > 0.0 && clearBox.h > 0.0) {
        g_pHyprOpenGL->scissor(clearBox, false);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        g_pHyprOpenGL->scissor(nullptr);
    }
}

void CGlassLayerSurface::compositeAndRestore(PHLMONITOR monitor, float alpha) {
    // Restore the original currentFB before compositing
    if (m_savedCurrentFB) {
        g_pHyprRenderer->m_renderData.currentFB = m_savedCurrentFB;
        glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_savedCurrentFB.get())->getFBID());
        m_savedCurrentFB.reset();
    }

    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!shaderManager.isInitialized() || !m_hasCachedSample)
        return;

    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    auto target = g_pHyprRenderer->m_renderData.currentFB;
    if (!target)
        return;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox rawBox       = *layerBox;
    CBox transformBox = transformedLayerBox(rawBox, monitor);

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float cornerRadius  = 0.0f;
    float roundingPower = 2.0f;

    float contentContrast = 0.0f;
    if (auto ccIt = g_pGlobalState->layerNamespaceContentContrast.find(layerSurface->m_namespace);
        ccIt != g_pGlobalState->layerNamespaceContentContrast.end())
        contentContrast = std::clamp(ccIt->second, 0.0f, 1.0f);

    // Explicit regions: one true glass quad per element (window-grade optics:
    // per-quad bezel refraction, fresnel rim, corner SDF). The layer's own
    // rendered content then composites on top glass-free via a zeroed preset.
    auto regIt = g_pGlobalState->layerNamespaceRegions.find(layerSurface->m_namespace);
    if (regIt != g_pGlobalState->layerNamespaceRegions.end() && !regIt->second.empty()) {
        const auto& regions = regIt->second;
        float blurStrength = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int   downscale    = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;
        float blurRadius   = blurStrength * 12.0f / downscale;
        int   blurIters    = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        int   monW         = static_cast<int>(monitor->m_transformedSize.x);
        int   monH         = static_cast<int>(monitor->m_transformedSize.y);
        auto  targetFBID   = dynamic_cast<Render::GL::CGLFramebuffer*>(target.get())->getFBID();

        if (m_regionFramebuffers.size() < regions.size())
            m_regionFramebuffers.resize(regions.size());

        for (size_t i = 0; i < regions.size(); ++i) {
            const auto& r = regions[i];
            CBox rr{static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.w), static_cast<double>(r.h)};
            rr.translate(-monitor->m_position);
            rr.scale(monitor->m_scale).round().noNegativeSize();
            if (rr.w <= 0.0 || rr.h <= 0.0 || !std::isfinite(rr.x) || !std::isfinite(rr.y))
                continue;
            CBox rt = transformedLayerBox(rr, monitor);
            Vector2D pad;
            GlassRenderer::sampleBackground(m_regionFramebuffers[i], target, rt, pad, downscale);
            if (blurRadius > 0.05f)
                GlassRenderer::blurBackground(m_regionFramebuffers[i], blurRadius, blurIters, targetFBID, monW, monH);
            GlassRenderer::applyGlassEffect(m_regionFramebuffers[i], target, rr, rt, alpha,
                                             r.radius * static_cast<float>(monitor->m_scale), 2.0f, pad, ctx, nullptr);
        }

        // Composite the layer's rendered content over the region glass with a
        // preset that zeroes every glass term -> shader emits content only.
        static const SCustomPreset contentOnlyPreset = [] {
            SCustomPreset p;
            p.name = "__hg_content_only";
            for (SPresetValues* v : {&p.shared, &p.dark, &p.light}) {
                v->blurStrength = 0.0f;  v->blurIterations = 1;   v->refractionStrength = 0.0f;
                v->chromaticAberration = 0.0f; v->fresnelStrength = 0.0f; v->specularStrength = 0.0f;
                v->glassOpacity = 0.0f;  v->edgeThickness = 0.0f; v->tintColor = 0;
                v->lensDistortion = 0.0f; v->brightness = 1.0f;   v->contrast = 1.0f;
                v->saturation = 1.0f;    v->vibrancy = 0.0f;      v->vibrancyDarkness = 0.0f;
                v->adaptiveDim = 0.0f;   v->adaptiveBoost = 0.0f;
            }
            return p;
        }();
        static const std::unordered_map<std::string, SCustomPreset> contentOnlyMap{
            {contentOnlyPreset.name, contentOnlyPreset}};
        static const std::string contentOnlyName = contentOnlyPreset.name;
        const SResolveContext contentCtx{contentOnlyName, isDark, g_pGlobalState->config, contentOnlyMap};

        GlassRenderer::SMaskInfo contentMask{
            .textureId      = m_surfaceTempFramebuffer->getTexture()->m_texID,
            .target         = GL_TEXTURE_2D,
            .uvOffset       = {transformBox.x / monW, transformBox.y / monH},
            .uvScale        = {transformBox.w / monW, transformBox.h / monH},
            // Respect the configured namespace threshold: the legacy path
            // discarded sub-threshold pixels (faint shadows/scrim fringe past
            // the panel border) entirely — keep that behavior for content.
            .alphaThreshold = [&] {
                float t = 0.001f;
                auto it = g_pGlobalState->layerNamespaceMaskThresholds.find(layerSurface->m_namespace);
                if (it != g_pGlobalState->layerNamespaceMaskThresholds.end())
                    t = it->second;
                return t * std::clamp(alpha, 0.0f, 1.0f);
            }(),
            .contentContrast = contentContrast,
        };
        GlassRenderer::applyGlassEffect(m_sampleFramebuffer, target, rawBox, transformBox, alpha,
                                         0.0f, 2.0f, m_samplePaddingRatio, contentCtx, &contentMask);
        return;
    }

    // Use the temp FBO's rendered alpha as a mask: glass only where the surface
    // has visible content (alpha > 0). The temp FBO is in monitor coordinates,
    // so we map from the glass quad UV to monitor UV.
    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    float maskThreshold = 0.001f;
    auto threshIt = g_pGlobalState->layerNamespaceMaskThresholds.find(layerSurface->m_namespace);
    if (threshIt != g_pGlobalState->layerNamespaceMaskThresholds.end())
        maskThreshold = threshIt->second;

    // The temp FBO stores the layer after Hyprland applies fade alpha. Keep
    // mask_threshold relative to the layer's content alpha, otherwise fade-out
    // makes the mask fall below threshold early and the glass blinks off.
    maskThreshold *= std::clamp(alpha, 0.0f, 1.0f);

    GlassRenderer::SMaskInfo maskInfo{
        .textureId         = m_surfaceTempFramebuffer->getTexture()->m_texID,
        .target            = GL_TEXTURE_2D,
        .uvOffset          = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale           = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
        .alphaThreshold    = maskThreshold,
        .contentContrast   = contentContrast,
    };

    // The glass shader composites both the glass effect and the surface content
    // in a single pass: glass behind, surface on top, using the temp FBO alpha.
    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, target,
                                     rawBox, transformBox, alpha,
                                     cornerRadius, roundingPower, m_samplePaddingRatio, ctx,
                                     &maskInfo);
}
