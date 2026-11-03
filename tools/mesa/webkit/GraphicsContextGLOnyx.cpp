/*
 * GraphicsContextGLOnyx.cpp -- WebGL sur ARMPI (docs/MESA.md) : ANGLE, puis armegl, Mesa, le V3D. Repris de
 * GraphicsContextGLEGL.cpp. Sans compositeur (--nogpu), la toile est relue et peinte dans la page. Avec lui,
 * elle a une couche : a chaque image presentee, le GPU la recopie dans une texture de GpuKit que le compositeur
 * montre, sans que le processeur touche un pixel.
 * Sans GPU, pas de display : getContext ("webgl") rend null, comme un navigateur sans WebGL.
 */
#include "config.h"

#if ENABLE(WEBGL) && OS(ONYX)
#include "GraphicsContextGL.h"

#include "ANGLEHeaders.h"
#include "ANGLEUtilities.h"
#include "GraphicsContextGLANGLE.h"
#include "GraphicsLayerContentsDisplayDelegate.h"
#include "GraphicsLayerOnyx.h"
#include "PixelBuffer.h"
#include "VideoFrame.h"
#include <wtf/TZoneMallocInlines.h>

extern "C" {
unsigned armgl_import(void* va, unsigned phys, unsigned bytes);
}

namespace WebCore {

class OnyxCanvasDisplayDelegate final : public GraphicsLayerContentsDisplayDelegate {
public:
    explicit OnyxCanvasDisplayDelegate(PlatformLayer* layer)
        : m_layer(layer)
    {
    }
    PlatformLayer* platformLayer() const final { return m_layer; }

private:
    PlatformLayer* m_layer;
};

class GraphicsContextGLOnyx final : public GraphicsContextGLANGLE, private GraphicsLayerOnyxContents::ExternalClient {
    WTF_MAKE_TZONE_ALLOCATED(GraphicsContextGLOnyx);
public:
    static RefPtr<GraphicsContextGLOnyx> create(GraphicsContextGLAttributes&& attributes)
    {
        RefPtr context = adoptRef(new GraphicsContextGLOnyx(WTF::move(attributes)));
        if (!context->initialize())
            return nullptr;
        return context;
    }

    ~GraphicsContextGLOnyx()
    {
        if (makeContextCurrent())
            dropExternalTexture();
    }

    RefPtr<GraphicsLayerContentsDisplayDelegate> layerContentsDisplayDelegate() final
    {
        if (!m_delegate)
            m_delegate = adoptRef(new OnyxCanvasDisplayDelegate(m_contents.platformLayer()));
        return m_delegate;
    }
#if ENABLE(VIDEO)
    bool copyTextureFromVideoFrame(VideoFrame&, PlatformGLObject, GCGLenum, GCGLint, GCGLenum, GCGLenum, GCGLenum, bool, bool) final { return false; }
#endif
#if ENABLE(MEDIA_STREAM) || ENABLE(WEB_CODECS)
    RefPtr<VideoFrame> surfaceBufferToVideoFrame(SurfaceBuffer) final { return nullptr; }
#endif

    RefPtr<PixelBuffer> readCompositedResults() final
    {
        if (!m_isCompositorTextureInitialized || !makeContextCurrent() || getInternalFramebufferSize().isEmpty())
            return nullptr;
        ScopedScratchReadFramebufferBinding fboBinding(m_isForWebGL2, m_state.boundReadFBO, m_compositorTexture);
        return readPixelsForPaintResults();
    }

    bool reshapeDrawingBuffer() final
    {
        auto attrs = contextAttributes();
        const auto size = getInternalFramebufferSize();
        GLuint colorFormat = attrs.alpha ? GL_RGBA : GL_RGB;
        ScopedRestoreTextureBinding restoreBinding(GL_TEXTURE_BINDING_2D, GL_TEXTURE_2D);
        ScopedBufferBinding scopedPixelUnpackBufferReset(GL_PIXEL_UNPACK_BUFFER, 0, m_isForWebGL2);
        GL_BindTexture(GL_TEXTURE_2D, m_compositorTexture);
        GL_TexImage2D(GL_TEXTURE_2D, 0, colorFormat, size.width(), size.height(), 0, colorFormat, GL_UNSIGNED_BYTE, 0);
        GL_BindTexture(GL_TEXTURE_2D, m_texture);
        GL_TexImage2D(GL_TEXTURE_2D, 0, colorFormat, size.width(), size.height(), 0, colorFormat, GL_UNSIGNED_BYTE, 0);
        m_isCompositorTextureInitialized = false;
        dropExternalTexture();
        m_contents.setExternal(this, size);
        return true;
    }

    void prepareForDisplay() final
    {
        if (!makeContextCurrent())
            return;
        prepareTexture();
        std::swap(m_texture, m_compositorTexture);
        m_isCompositorTextureInitialized = true;
        ScopedRestoreReadFramebufferBinding fboBinding(m_isForWebGL2, m_state.boundReadFBO, m_fbo);
        GL_FramebufferTexture2D(fboBinding.framebufferTarget(), GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
        copyToExternalTexture();
        GL_Flush();
        m_contents.externalPictureChanged();
    }

private:
    explicit GraphicsContextGLOnyx(GraphicsContextGLAttributes&& attributes)
        : GraphicsContextGLANGLE(WTF::move(attributes))
    {
    }

    bool platformInitializeContext() final;
    bool platformInitialize() final;

    // La texture du compositeur, cible de Mesa par une EGLImage (un dma-buf pour ANGLE : son fd est un nom d'armgl).
    void externalTextureReady(void* pixels, unsigned physical, unsigned bytes, int stride) final
    {
        if (!makeContextCurrent())
            return;
        dropExternalTexture();
        const auto size = getInternalFramebufferSize();
        const EGLint attributes[] = {
            EGL_WIDTH, size.width(), EGL_HEIGHT, size.height(),
            EGL_LINUX_DRM_FOURCC_EXT, 0x34324241, // DRM_FORMAT_ABGR8888 : R G B A en memoire
            EGL_DMA_BUF_PLANE0_FD_EXT, static_cast<EGLint>(armgl_import(pixels, physical, bytes)),
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
            EGL_NONE
        };
        m_externalImage = EGL_CreateImageKHR(m_displayObj, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
        if (!m_externalImage)
            return;
        ScopedRestoreTextureBinding restoreBinding(GL_TEXTURE_BINDING_2D, GL_TEXTURE_2D);
        GL_GenTextures(1, &m_externalTexture);
        GL_BindTexture(GL_TEXTURE_2D, m_externalTexture);
        GL_EGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_externalImage);
        GL_GenFramebuffers(1, &m_externalFBO);
        GL_BindFramebuffer(GL_DRAW_FRAMEBUFFER_ANGLE, m_externalFBO);
        GL_FramebufferTexture2D(GL_DRAW_FRAMEBUFFER_ANGLE, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_externalTexture, 0);
        GL_BindFramebuffer(GL_DRAW_FRAMEBUFFER_ANGLE, m_state.boundDrawFBO);
        copyToExternalTexture();
        GL_Flush();
    }

    void externalTextureLost() final
    {
        if (makeContextCurrent())
            dropExternalTexture();
    }

    void dropExternalTexture()
    {
        if (m_externalFBO)
            GL_DeleteFramebuffers(1, &m_externalFBO);
        if (m_externalTexture)
            GL_DeleteTextures(1, &m_externalTexture);
        if (m_externalImage)
            EGL_DestroyImageKHR(m_displayObj, m_externalImage);
        m_externalFBO = 0;
        m_externalTexture = 0;
        m_externalImage = nullptr;
    }

    // L'image presentee, retournee : la ligne 0 de GL est en bas, celle du compositeur en haut.
    void copyToExternalTexture()
    {
        if (!m_externalFBO || !m_isCompositorTextureInitialized)
            return;
        const auto size = getInternalFramebufferSize();
        ScopedGLCapability scissor(GL_SCISSOR_TEST, false);
        ScopedScratchReadFramebufferBinding fboBinding(m_isForWebGL2, m_state.boundReadFBO, m_compositorTexture);
        GL_BindFramebuffer(GL_DRAW_FRAMEBUFFER_ANGLE, m_externalFBO);
        if (m_isForWebGL2)
            GL_BlitFramebuffer(0, 0, size.width(), size.height(), 0, size.height(), size.width(), 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        else
            GL_BlitFramebufferNV(0, 0, size.width(), size.height(), 0, size.height(), size.width(), 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        GL_BindFramebuffer(GL_DRAW_FRAMEBUFFER_ANGLE, m_state.boundDrawFBO);
    }

    GraphicsLayerOnyxContents m_contents;
    RefPtr<GraphicsLayerContentsDisplayDelegate> m_delegate;
    void* m_externalImage { nullptr };
    GCGLuint m_externalTexture { 0 };
    GCGLuint m_externalFBO { 0 };
    GCGLuint m_compositorTexture { 0 };
    bool m_isCompositorTextureInitialized { false };
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(GraphicsContextGLOnyx);

GraphicsContextGLANGLE::~GraphicsContextGLANGLE()
{
    if (!makeContextCurrent())
        return;
    GL_Disable(DEBUG_OUTPUT);
    if (m_texture)
        GL_DeleteTextures(1, &m_texture);
    GL_DeleteFramebuffers(1, &m_fbo);
    if (m_contextObj) {
        for (auto* image : m_eglImages.values())
            EGL_DestroyImageKHR(m_displayObj, image);
        for (auto* sync : m_eglSyncs.values())
            EGL_DestroySync(m_displayObj, sync);
        EGL_MakeCurrent(m_displayObj, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        EGL_DestroyContext(m_displayObj, m_contextObj);
    }
}

bool GraphicsContextGLANGLE::makeContextCurrent()
{
    if (EGL_GetCurrentContext() == m_contextObj)
        return true;
    return !!EGL_MakeCurrent(m_displayObj, EGL_NO_SURFACE, EGL_NO_SURFACE, m_contextObj);
}

void GraphicsContextGLANGLE::checkGPUStatus()
{
    if (m_failNextStatusCheck) {
        m_failNextStatusCheck = false;
        forceContextLost();
    }
}

void GraphicsContextGLANGLE::platformReleaseThreadResources()
{
}

static EGLDisplay sharedDisplay()
{
    static EGLDisplay display = EGL_NO_DISPLAY;
    static bool tried = false;
    if (tried)
        return display;
    tried = true;
    // ni X11 ni GBM : il faut dire a ANGLE que son backend est un EGL natif
    const EGLint attributes[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_EGL_ANGLE,
        EGL_NONE
    };
    EGLDisplay candidate = EGL_GetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attributes);
    EGLint major = 0, minor = 0;
    if (candidate != EGL_NO_DISPLAY && EGL_Initialize(candidate, &major, &minor))
        display = candidate;
    return display;
}

bool GraphicsContextGLOnyx::platformInitializeContext()
{
    m_isForWebGL2 = contextAttributes().isWebGL2;
    m_displayObj = sharedDisplay();
    if (m_displayObj == EGL_NO_DISPLAY)
        return false;

    EGLint count = 0;
    EGL_GetConfigs(m_displayObj, &m_configObj, 1, &count);
    if (count != 1)
        return false;
    EGL_BindAPI(EGL_OPENGL_ES_API);

    Vector<EGLint> attributes;
    attributes.append(EGL_CONTEXT_CLIENT_VERSION);
    attributes.append(m_isForWebGL2 ? 3 : 2);
    if (!m_isForWebGL2) {
        // ANGLE passerait sinon en ES3 de lui-meme
        attributes.append(EGL_CONTEXT_OPENGL_BACKWARDS_COMPATIBLE_ANGLE);
        attributes.append(EGL_FALSE);
    }
    attributes.append(EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE);
    attributes.append(EGL_TRUE);
    attributes.append(EGL_ROBUST_RESOURCE_INITIALIZATION_ANGLE);
    attributes.append(EGL_TRUE);
    attributes.append(EGL_CONTEXT_CLIENT_ARRAYS_ENABLED_ANGLE);
    attributes.append(EGL_FALSE);
    attributes.append(EGL_CONTEXT_BIND_GENERATES_RESOURCE_CHROMIUM);
    attributes.append(EGL_FALSE);
    attributes.append(EGL_NONE);

    m_contextObj = EGL_CreateContext(m_displayObj, m_configObj, EGL_NO_CONTEXT, attributes.span().data());
    if (m_contextObj == EGL_NO_CONTEXT)
        return false;
    return makeContextCurrent();
}

bool GraphicsContextGLOnyx::platformInitialize()
{
    GL_RequestExtensionANGLE("GL_OES_EGL_image");
    GL_RequestExtensionANGLE("GL_NV_framebuffer_blit");
    GL_GenTextures(1, &m_compositorTexture);
    GL_BindTexture(GL_TEXTURE_2D, m_compositorTexture);
    GL_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GL_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GL_BindTexture(GL_TEXTURE_2D, 0);
    return true;
}

RefPtr<GraphicsContextGL> createWebProcessGraphicsContextGL(const GraphicsContextGLAttributes& attributes)
{
    return GraphicsContextGLOnyx::create(GraphicsContextGLAttributes { attributes });
}

} // namespace WebCore

#endif
