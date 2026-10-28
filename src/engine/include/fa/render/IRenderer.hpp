#pragma once

#include <memory>
#include <span>
#include <string_view>

#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/platform/Platform.hpp>

#include "Camera2D.hpp"
#include "RenderConstants.hpp"
#include "RenderQueue.hpp"
#include "RenderTypes.hpp"
#include "Viewport.hpp"

namespace fa::render {

/// \brief Creation parameters for the renderer.
struct RendererDesc {
    /// Colour of the virtual surface, i.e. what shows through where nothing is
    /// drawn. The letterbox bars around it are always black.
    core::Color clearColor{0.06f, 0.07f, 0.10f, 1.0f};
    /// Quads buffered before an early flush; see kDefaultMaxQuads.
    int maxQuadsPerFrame = kDefaultMaxQuads;
    /// Asks for vsync on top of whatever the platform context already uses.
    bool vsync = true;
    /// Requests a debug OpenGL context; wiring it to the log is M27's job.
    bool debugContext = false;
};

/// \brief The engine's only OpenGL user.
///
/// Frame contract, in call order:
///   initialize() once, then per frame beginFrame() -> submit()* -> endFrame().
/// submit() only appends to the render queue; every GL call happens in
/// endFrame(), which also swaps buffers. Textures may be created and destroyed
/// at any time between frames, but not in the middle of one.
///
/// Deliberately absent from v1: text and glyph drawing (the font atlas is M09's
/// call), render targets, and non-sprite passes. See docs/modules/06-rendering.md.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// \brief Creates the GL context through \p platform and loads everything the
    /// sprite pipeline needs.
    ///
    /// The platform window must already exist. On failure nothing is left
    /// half-initialized and shutdown() is still safe to call.
    virtual core::Status initialize(platform::IPlatform& platform, const RendererDesc& desc = {}) = 0;

    /// \brief Releases GL objects and the context. Safe to call when not initialized.
    virtual void shutdown() = 0;

    /// \brief Whether initialize() succeeded and the pipeline is usable.
    virtual bool isReady() const = 0;

    /// \brief Uploads a texture and returns its handle.
    ///
    /// \param outHandle Set only on success.
    virtual core::Status createTexture(const TextureDesc& desc, TextureHandle& outHandle) = 0;

    /// \brief Destroys a texture. Destroying an invalid handle does nothing.
    virtual void destroyTexture(TextureHandle handle) = 0;

    /// \brief Size in pixels of \p handle, or (0, 0) for an unknown handle.
    virtual core::Vec2 textureSize(TextureHandle handle) const = 0;

    /// \brief Number of live textures; used by the smoke run and the editor.
    virtual int liveTextureCount() const = 0;

    /// \brief Starts a frame: clears the letterbox and the virtual surface.
    virtual void beginFrame(const Camera2D& camera) = 0;

    /// \brief Queues one sprite for the current frame.
    virtual void submit(const DrawItem& item) = 0;

    /// \brief Queues every item in \p items; convenience for extraction passes.
    virtual void submitAll(std::span<const DrawItem> items) = 0;

    /// \brief Sorts, batches, draws and presents the frame.
    ///
    /// Safe to call after beginFrame() even when nothing was submitted, which is
    /// how a frame that only clears the screen is expressed.
    virtual void endFrame() = 0;

    /// \brief Queues a one-pixel-thick rectangle outline in world space.
    ///
    /// Debug overlay only (M27): it shares the sprite batch, so it costs no extra
    /// draw call when it happens to sit between items of the same texture.
    virtual void drawDebugRect(core::Rect rect, core::Color color, float thickness = 1.0f) = 0;

    /// \brief Queues a straight line in world space; debug overlay only.
    virtual void drawDebugLine(core::Vec2 from, core::Vec2 to, core::Color color, float thickness = 1.0f) = 0;

    /// \brief Cost of the last completed frame.
    virtual const RenderStats& stats() const = 0;

    /// \brief The letterbox mapping used by the last beginFrame().
    virtual Viewport viewport() const = 0;

    /// \brief `GL_VERSION` of the live context, empty before initialize().
    virtual std::string_view glVersion() const = 0;

    /// \brief `GL_RENDERER` of the live context, empty before initialize().
    virtual std::string_view glRenderer() const = 0;
};

/// \brief Creates the OpenGL sprite renderer.
///
/// Nothing is initialized yet: the caller decides when a window exists and then
/// calls initialize(). Returns a valid object even when the platform lacks GL
/// support, so initialize() is the single place that can fail.
std::unique_ptr<IRenderer> createRenderer();

}  // namespace fa::render
