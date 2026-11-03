"""enable-webgl : les retouches de WebKit pour WebGL sur ARMPI (docs/MESA.md), sur la copie de build-webkit.sh.
    python3 -I tools/mesa/webkit/enable-webgl.py COPIE_DE_WEBKIT
"""
import sys

w = sys.argv[1] + "/Source/"


def sub(path, a, b):
    with open(w + path, encoding="utf-8") as f:
        s = f.read()
    if b in s:
        return
    assert a in s, (path, a)
    with open(w + path, "w", encoding="utf-8") as f:
        f.write(s.replace(a, b, 1))


# WebGL : l'option, ANGLE dans la construction
sub("cmake/OptionsOnyx.cmake", "WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEBGL PRIVATE OFF)",
    "WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEBGL PRIVATE ON)")
sub("cmake/OptionsOnyx.cmake", "set(ENABLE_WEBGL OFF)\n", "set(ENABLE_WEBGL ON)\n")

# le contexte d'ARMPI
sub("WebCore/PlatformOnyx.cmake", "list(APPEND WebCore_SOURCES\n",
    "list(APPEND WebCore_SOURCES\n    platform/graphics/onyx/GraphicsContextGLOnyx.cpp\n"
    "    platform/graphics/skia/GraphicsContextGLSkia.cpp\n")

# pas de PlatformDisplay ni d'images sur le GPU ici : Skia est sur le processeur
sub("WebCore/platform/graphics/skia/GraphicsContextGLSkia.cpp", '#include "GLContext.h"\n', '#if !OS(ONYX)\n#include "GLContext.h"\n#endif\n')
sub("WebCore/platform/graphics/skia/GraphicsContextGLSkia.cpp", '#include "PlatformDisplay.h"\n', '#if !OS(ONYX)\n#include "PlatformDisplay.h"\n#endif\n')
sub("WebCore/platform/graphics/skia/GraphicsContextGLSkia.cpp",
    "        if (platformImage->isTextureBacked() && !PlatformDisplay::sharedDisplay().skiaGLContext()->makeContextCurrent())\n"
    "            return false;\n",
    "#if !OS(ONYX)\n"
    "        if (platformImage->isTextureBacked() && !PlatformDisplay::sharedDisplay().skiaGLContext()->makeContextCurrent())\n"
    "            return false;\n"
    "#endif\n")

# la toile dans le compositeur : une couche de contenu dont la texture est une cible du GPU, sans pixels du processeur
sub("WebCore/platform/graphics/onyx/GraphicsLayerOnyx.h",
    "    // The pixels are about to go: what was last drawn stays on the screen.\n",
    "    // A canvas: the compositor makes a texture of this size and gives it to the client, which draws into it\n"
    "    // with the GPU; no pixel goes through the processor.\n"
    "    class ExternalClient {\n"
    "    public:\n"
    "        virtual ~ExternalClient() = default;\n"
    "        virtual void externalTextureReady(void* pixels, unsigned physical, unsigned bytes, int stride) = 0;\n"
    "        virtual void externalTextureLost() = 0;\n"
    "    };\n"
    "    void setExternal(ExternalClient* client, const IntSize& size) { m_external = client; m_size = size; ++m_generation; externalPictureChanged(); }\n"
    "    ExternalClient* external() const { return m_external; }\n"
    "    WEBCORE_EXPORT void externalPictureChanged();\n"
    "\n"
    "    // The pixels are about to go: what was last drawn stays on the screen.\n")
sub("WebCore/platform/graphics/onyx/GraphicsLayerOnyx.h",
    "    GraphicsLayerOnyx* m_layer { nullptr };\n    const uint32_t* m_pixels { nullptr };\n",
    "    GraphicsLayerOnyx* m_layer { nullptr };\n    ExternalClient* m_external { nullptr };\n    const uint32_t* m_pixels { nullptr };\n")
sub("WebCore/platform/graphics/onyx/GraphicsLayerOnyx.h",
    "    void setContentsToPlatformLayer(PlatformLayer*, ContentsLayerPurpose) final;\n",
    "    void setContentsToPlatformLayer(PlatformLayer*, ContentsLayerPurpose) final;\n"
    "    void setContentsDisplayDelegate(RefPtr<GraphicsLayerContentsDisplayDelegate>&&, ContentsLayerPurpose) final;\n")
sub("WebCore/platform/graphics/onyx/GraphicsLayerOnyx.cpp",
    "void GraphicsLayerOnyx::contentsPictureChanged()\n",
    "void GraphicsLayerOnyxContents::externalPictureChanged()\n{\n    ++m_generation;\n    if (m_layer)\n        m_layer->contentsPictureChanged();\n}\n\n"
    "void GraphicsLayerOnyx::setContentsDisplayDelegate(RefPtr<GraphicsLayerContentsDisplayDelegate>&& delegate, ContentsLayerPurpose purpose)\n"
    "{\n    setContentsToPlatformLayer(delegate ? delegate->platformLayer() : nullptr, purpose);\n}\n\n"
    "void GraphicsLayerOnyx::contentsPictureChanged()\n")
L = "WebKit/WebProcess/WebPage/CoordinatedGraphics/LayerTreeHostOnyx.cpp"
sub(L, "void LayerTreeHost::layerDestroyed(GraphicsLayerOnyx& layer)\n{\n    m_layers.remove(&layer);\n",
    "void LayerTreeHost::layerDestroyed(GraphicsLayerOnyx& layer)\n{\n    m_layers.remove(&layer);\n"
    "    if (auto* source = layer.contents(); source && source->external())\n        source->external()->externalTextureLost();\n")
sub(L, "    auto* pixels = source->pixels();\n",
    "    // A canvas: the GPU draws into the texture itself; here only its making, and the damage.\n"
    "    if (auto* client = source->external()) {\n"
    "        auto& size = source->size();\n"
    "        if (size.isEmpty())\n            return;\n"
    "        if (contents.texture && contents.size != size) {\n"
    "            client->externalTextureLost();\n"
    "            destroyContentsTexture(contents);\n"
    "        }\n"
    "        if (!contents.texture) {\n"
    "            if (!hasRoomForTileTexture(size))\n                return;\n"
    "            Vector<uint32_t> blank;\n"
    "            blank.fill(0u, size.width() * size.height());\n"
    "            contents.texture = gpc_tex_create(m_compositor, size.width(), size.height(), blank.span().data(), size.width());\n"
    "            if (!contents.texture)\n                return;\n"
    "            ++m_textureCount;\n"
    "            m_texturePixels += static_cast<uint64_t>(size.width()) * size.height();\n"
    "            contents.size = size;\n"
    "            long info[6];\n"
    "            if (!armpi_gpu_texture_info(armpi_gpu_last_texture(), info) && info[2] == 3 && info[3] == size.width() && info[4] == size.height())\n"
    "                client->externalTextureReady(reinterpret_cast<void*>(info[5]), static_cast<unsigned>(info[0]), static_cast<unsigned>(info[1]), ((size.width() + 31) & ~31) * 4);\n"
    "        }\n"
    "        if (contents.generation == source->generation())\n            return;\n"
    "        contents.generation = source->generation();\n"
    "        ++m_paintGeneration;\n"
    "        auto targetRect = enclosingIntRect(state.toTarget.mapRect(layer.contentsRect()));\n"
    "        targetRect.intersect(state.clip);\n"
    "        m_frameDamage.unite(targetRect);\n"
    "        return;\n"
    "    }\n\n"
    "    auto* pixels = source->pixels();\n")
sub(L, "namespace WebKit {\n",
    "extern \"C\" int armpi_gpu_last_texture();\nextern \"C\" int armpi_gpu_texture_info(int, long*);\n\nnamespace WebKit {\n")
sub("WebCore/platform/graphics/onyx/GraphicsLayerOnyx.cpp", '#include "GraphicsLayerOnyx.h"\n',
    '#include "GraphicsLayerOnyx.h"\n#include "GraphicsLayerContentsDisplayDelegate.h"\n')
sub(L, "        triggers |= ChromeClient::AnimationTrigger | ChromeClient::AnimatedOpacityTrigger;\n",
    "        triggers |= ChromeClient::AnimationTrigger | ChromeClient::AnimatedOpacityTrigger;\n"
    "        // A WebGL canvas is a layer too: the GPU copies it into a texture of the compositor.\n"
    "        triggers |= ChromeClient::CanvasTrigger;\n")

# ce que la mise a jour de la page a demande (la toile vient de dessiner) est dans cette image : pas d'image vide apres
sub(L, "        m_pageUpdateTime += MonotonicTime::now() - m_frameTime;\n    }\n",
    "        m_pageUpdateTime += MonotonicTime::now() - m_frameTime;\n"
    "        // What the update asked for is in this frame: no empty frame after it, which would only hold back the next one.\n"
    "        if (!m_renderingUpdateRequested)\n"
    "            m_renderTimer.stop();\n"
    "    }\n")

# sans moniteur d'affichage, le minuteur de la page compte depuis le debut de la mise a jour, pas depuis sa fin
R = "WebCore/page/RenderingUpdateScheduler"
sub(R + ".h", "    unsigned m_rescheduledRenderingUpdateCount { 0 };\n",
    "    unsigned m_rescheduledRenderingUpdateCount { 0 };\n#if OS(ONYX)\n    MonotonicTime m_lastRefresh;\n#endif\n")
sub(R + ".h", "#include <wtf/Seconds.h>\n", "#include <wtf/MonotonicTime.h>\n#include <wtf/Seconds.h>\n")
sub(R + ".cpp", "        startTimer(protect(m_page)->preferredRenderingUpdateInterval());\n",
    "#if OS(ONYX)\n"
    "        // Counted from the start of the last update: the page's own work is not added to the interval.\n"
    "        startTimer(std::max(0_s, m_lastRefresh + protect(m_page)->preferredRenderingUpdateInterval() - MonotonicTime::now()));\n"
    "#else\n"
    "        startTimer(protect(m_page)->preferredRenderingUpdateInterval());\n"
    "#endif\n")
sub(R + ".cpp", "    tracePoint(TriggerRenderingUpdate);\n\n    clearScheduled();\n",
    "    tracePoint(TriggerRenderingUpdate);\n\n#if OS(ONYX)\n    m_lastRefresh = MonotonicTime::now();\n#endif\n    clearScheduled();\n")

# l'image du compositeur n'est plus recopiee par le process UI : Elegant la lit dans la surface, ou il y en a deux,
# l'une au-dessus de l'autre ; le compositeur ecrit celle qui n'est pas montree (l'accuse dit que l'autre l'est)
L_H = L[:-3] + "h"
P = "WebKit/UIProcess/onyx/DrawingAreaProxyCoordinatedGraphicsOnyx.cpp"
PC = "WebKit/UIProcess/CoordinatedGraphics/DrawingAreaProxyCoordinatedGraphics"
sub(L, "    unsigned* pixels = m_surfacePixels;\n    int stride = m_surfaceSize.width();\n",
    "    // The image the window does not show.\n"
    "    unsigned* image = m_surfacePixels + static_cast<size_t>(m_image) * m_surfaceSize.height() * m_surfaceSize.width();\n"
    "    unsigned* pixels = image;\n    int stride = m_surfaceSize.width();\n")
sub(L, "memcpy(m_surfacePixels + static_cast<size_t>(y) * m_surfaceSize.width() + damage.x(),",
    "memcpy(image + static_cast<size_t>(y) * m_surfaceSize.width() + damage.x(),")
sub(L, "    if (m_copyTarget && IntSize { width, height } != m_surfaceSize) {\n",
    "    // (Two images in the surface, one above the other.)\n    height /= 2;\n    if (m_copyTarget && IntSize { width, height } != m_surfaceSize) {\n")
sub(L, "    m_surfaceSize = { width, height };\n    m_targetPathLogged = false;\n",
    "    m_surfaceSize = { width, height };\n    m_image = 0;\n    m_lastDamage = { };\n    m_targetPathLogged = false;\n")
sub(L, "    if (!m_frameDamage.isEmpty()) {\n        if (compositeFrame(m_frameDamage)) {\n",
    "    if (!m_frameDamage.isEmpty()) {\n"
    "        // This image lacks the last frame's changes too; what the window must redo is this frame's.\n"
    "        auto shownDamage = m_frameDamage;\n"
    "        m_frameDamage.unite(m_lastDamage);\n"
    "        m_frameDamage.intersect(viewRect);\n"
    "        if (compositeFrame(m_frameDamage)) {\n")
sub(L, "            m_frameCallback(viewRect.size(), m_frameDamage);\n",
    "            m_frameCallback(viewRect.size(), shownDamage, m_image);\n"
    "            m_lastDamage = shownDamage;\n"
    "            m_image ^= 1;\n")
sub(L_H, "    using FrameCallback = Function<void(const WebCore::IntSize&, const WebCore::IntRect&)>;\n",
    "    using FrameCallback = Function<void(const WebCore::IntSize&, const WebCore::IntRect&, unsigned image)>;\n")
sub(L_H, "    WebCore::IntRect m_frameDamage;\n",
    "    WebCore::IntRect m_frameDamage;\n    WebCore::IntRect m_lastDamage;\n    unsigned m_image { 0 };\n")
sub("WebKit/WebProcess/WebPage/CoordinatedGraphics/DrawingAreaCoordinatedGraphics.cpp",
    "[this](const IntSize& size, const IntRect& damage) {\n        send(Messages::DrawingAreaProxy::OnyxCompositedFrame(size, damage));\n",
    "[this](const IntSize& size, const IntRect& damage, unsigned image) {\n        send(Messages::DrawingAreaProxy::OnyxCompositedFrame(size, damage, image));\n")
sub("WebKit/UIProcess/DrawingAreaProxy.messages.in",
    "    OnyxCompositedFrame(WebCore::IntSize size, WebCore::IntRect damage)\n",
    "    OnyxCompositedFrame(WebCore::IntSize size, WebCore::IntRect damage, unsigned image)\n")
sub("WebKit/UIProcess/DrawingAreaProxy.h",
    "    virtual void onyxCompositedFrame(const WebCore::IntSize&, const WebCore::IntRect& /* damage */) { }\n",
    "    virtual void onyxCompositedFrame(const WebCore::IntSize&, const WebCore::IntRect& /* damage */, unsigned /* image */) { }\n")
sub(PC + ".h", "    void onyxCompositedFrame(const WebCore::IntSize&, const WebCore::IntRect& damage) override;\n",
    "    void onyxCompositedFrame(const WebCore::IntSize&, const WebCore::IntRect& damage, unsigned image) override;\n")
sub(PC + ".h", "    WebCore::IntSize m_onyxFrameSize;\n",
    "    WebCore::IntSize m_onyxFrameSize;\n    unsigned m_onyxImage { 0 };\n    bool m_onyxSurfaceIsDirect { false };\n")
sub(P, "namespace WebKit {\nusing namespace WebCore;\n\nstruct OnyxSurface {\n",
    "// Who shows the surface without a copy (the embedder's window compositor reads its buffer).\n"
    "static int (*s_onyxFrameClient)(int buffer, int pitch, long offset, int x, int y, int width, int height);\n\n"
    "extern \"C\" int onyx_surface_buffer(int surfaceID);\n"
    "extern \"C\" void onyx_set_frame_client(int (*client)(int, int, long, int, int, int, int)) { s_onyxFrameClient = client; }\n\n"
    "// Compositing left: the view's own painting is what is shown again.\n"
    "extern \"C\" void onyx_frame_client_reset()\n{\n    if (s_onyxFrameClient)\n        s_onyxFrameClient(-1, 0, 0, 0, 0, 0, 0);\n}\n\n"
    "namespace WebKit {\nusing namespace WebCore;\n\nstruct OnyxSurface {\n")
sub(P, "            int surfaceID = onyx_surface_create(std::max(screenWidth, viewRect.width()), std::max(screenHeight, viewRect.height()));\n",
    "            // (Two images, one above the other: the web process writes the one not shown.)\n"
    "            int surfaceID = onyx_surface_create(std::max(screenWidth, viewRect.width()), 2 * std::max(screenHeight, viewRect.height()));\n")
sub(P, "            m_onyxSurfaceSize = { width, height };\n            logLine(\"the UI process made surface\"",
    "            m_onyxSurfaceSize = { width, height / 2 };\n            logLine(\"the UI process made surface\"")
sub(P, "void DrawingAreaProxyCoordinatedGraphics::onyxCompositedFrame(const IntSize& frameSize, const IntRect& damage)\n",
    "void DrawingAreaProxyCoordinatedGraphics::onyxCompositedFrame(const IntSize& frameSize, const IntRect& damage, unsigned image)\n")
sub(P, "    m_onyxFrameSize = frameSize.shrunkTo(m_onyxSurfaceSize);\n\n",
    "    m_onyxFrameSize = frameSize.shrunkTo(m_onyxSurfaceSize);\n"
    "    m_onyxImage = image;\n\n"
    "    if (s_onyxFrameClient) {\n"
    "        int pitch = m_onyxSurfaceSize.width() * 4;\n"
    "        auto shown = sizeChanged ? IntRect { { }, m_onyxFrameSize } : damage;\n"
    "        if (s_onyxFrameClient(onyx_surface_buffer(m_onyxSurfaceID), pitch, static_cast<long>(image) * pitch * m_onyxSurfaceSize.height(), shown.x(), shown.y(), shown.width(), shown.height())) {\n"
    "            if (!std::exchange(m_onyxSurfaceIsDirect, true))\n"
    "                logLine(\"the window shows surface\", m_onyxSurfaceID, m_onyxSurfaceSize);\n"
    "            send(Messages::DrawingArea::DisplayDidRefresh(MonotonicTime::now()));\n"
    "            return;\n"
    "        }\n"
    "    }\n\n")
sub(P, "            const unsigned* source = m_onyxSurfacePixels + static_cast<size_t>(y) * m_onyxSurfaceSize.width() + paintRect.x();\n",
    "            const unsigned* source = m_onyxSurfacePixels + static_cast<size_t>(m_onyxImage * m_onyxSurfaceSize.height() + y) * m_onyxSurfaceSize.width() + paintRect.x();\n")
sub(PC + ".cpp",
    "    m_layerTreeContext = { };\n    if (RefPtr page = this->page())\n        page->exitAcceleratedCompositingMode();\n",
    "    m_layerTreeContext = { };\n#if USE(GRAPHICS_LAYER_ONYX)\n    onyx_frame_client_reset();\n#endif\n    if (RefPtr page = this->page())\n        page->exitAcceleratedCompositingMode();\n")
sub(PC + ".cpp",
    "void DrawingAreaProxyCoordinatedGraphics::exitAcceleratedCompositingMode(uint64_t, UpdateInfo&& updateInfo)\n",
    "#if USE(GRAPHICS_LAYER_ONYX)\nextern \"C\" void onyx_frame_client_reset();\n#endif\n\n"
    "void DrawingAreaProxyCoordinatedGraphics::exitAcceleratedCompositingMode(uint64_t, UpdateInfo&& updateInfo)\n")
