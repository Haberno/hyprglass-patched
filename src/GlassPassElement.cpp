#include "GlassPassElement.hpp"
#include "GlassDecoration.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

CGlassPassElement::CGlassPassElement(const SGlassPassData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassPassElement::draw() {
    if (m_data.decoration.valid())
        m_data.decoration->renderPass(g_pHyprRenderer->m_renderData.pMonitor.lock(), m_data.alpha);

    return {};
}

std::optional<CBox> CGlassPassElement::boundingBox() {
    if (!m_data.decoration.valid())
        return std::nullopt;

    auto window = m_data.decoration->getOwner();
    if (!window)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    auto box = WindowGeometry::computeWindowBox(window, monitor);
    if (!box)
        return std::nullopt;

    // Include the sampling margin in the live-blur footprint. The decoration
    // separately adds its complete source to the current frame's damage.
    const float padding = GlassRenderer::SAMPLE_PADDING_PX / monitor->m_scale;
    box->expand(padding);
    return box;
}

bool CGlassPassElement::needsLiveBlur() {
    // Declare the background dependency to the pass. This alone only expands
    // partial damage by the native blur radius, not our full sampling area.
    // Layers don't need this — they have their own blur cache with
    // scene generation tracking.
    return m_data.decoration.valid() && m_data.decoration->getOwner();
}

bool CGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassPassElement::disableSimplification() {
    return m_data.decoration.valid() && m_data.decoration->getOwner();
}
