#include "GlApi.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fa/core/Error.hpp>
#include <fa/core/Log.hpp>
#include <fa/render/IRenderer.hpp>
#include <fa/render/RenderError.hpp>

namespace fa::render {
namespace {

namespace gl = detail;

/// Guards against a caller asking for an absurd batch size and overflowing the
/// index range (uint32 indices cap a batch at 16384 quads anyway).
constexpr int kMaxQuadsLimit = 16384;

constexpr gl::GLsizei kFloatsPerVertex = 8;

struct Vertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

constexpr const char* kSpriteVertexShader = R"(#version 410 core
layout (location = 0) in vec2 aPosition;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

uniform mat4 uProjection;

out vec2 vTexCoord;
out vec4 vColor;

void main() {
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
)";

constexpr const char* kSpriteFragmentShader = R"(#version 410 core
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;

out vec4 oColor;

void main() {
    oColor = texture(uTexture, vTexCoord) * vColor;
}
)";

std::string hexError(gl::GLenum error) {
    constexpr const char* kDigits = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 12; shift >= 0; shift -= 4) {
        text.push_back(kDigits[(error >> shift) & 0xFu]);
    }
    return text;
}

}  // namespace

/// \brief Sprite-batching OpenGL 4.1 renderer.
///
/// Everything drawn is a textured quad in virtual pixel space. Positions are
/// snapped to whole virtual pixels on the CPU, so the GPU never resamples a
/// sprite; the projection only scales the finished 640x360 surface into the
/// letterboxed window region.
class GlRenderer final : public IRenderer {
public:
    GlRenderer() = default;
    ~GlRenderer() override { shutdown(); }

    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;

    core::Status initialize(platform::IPlatform& platform, const RendererDesc& desc) override;
    void shutdown() override;
    bool isReady() const override { return m_ready; }

    core::Status createTexture(const TextureDesc& desc, TextureHandle& outHandle) override;
    void destroyTexture(TextureHandle handle) override;
    core::Vec2 textureSize(TextureHandle handle) const override;
    int liveTextureCount() const override { return m_liveTextures; }

    void beginFrame(const Camera2D& camera) override;
    void submit(const DrawItem& item) override;
    void submitAll(std::span<const DrawItem> items) override;
    void endFrame() override;

    void drawDebugRect(core::Rect rect, core::Color color, float thickness) override;
    void drawDebugLine(core::Vec2 from, core::Vec2 to, core::Color color, float thickness) override;

    const RenderStats& stats() const override { return m_stats; }
    Viewport viewport() const override { return m_viewport; }
    std::string_view glVersion() const override { return m_version; }
    std::string_view glRenderer() const override { return m_rendererName; }

private:
    struct TextureSlot {
        gl::GLuint name = 0;
        int width = 0;
        int height = 0;
        bool alive = false;
    };

    core::Status compileShader(gl::GLenum type, const char* source, const char* debugName, gl::GLuint& outShader);
    core::Status createProgram();
    core::Status createGeometryBuffers();
    std::string shaderLog(gl::GLuint shader) const;
    std::string programLog(gl::GLuint program) const;
    std::string readString(gl::GLenum name) const;

    int slotIndexFor(TextureHandle handle) const;
    void startBatch(gl::GLuint texture);
    void flushBatch();
    void appendQuad(const DrawItem& item);
    void queueDebugItem(core::Rect rect, core::Color color);

    platform::IPlatform* m_platform = nullptr;
    platform::IGlContext* m_gl = nullptr;
    detail::GlFunctions m_functions;
    RendererDesc m_desc{};

    gl::GLuint m_program = 0;
    gl::GLint m_projectionLocation = -1;
    gl::GLint m_textureLocation = -1;
    gl::GLuint m_vertexArray = 0;
    gl::GLuint m_vertexBuffer = 0;
    gl::GLuint m_indexBuffer = 0;

    std::vector<Vertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
    int m_capacityQuads = kDefaultMaxQuads;

    gl::GLuint m_batchTexture = 0;
    int m_batchQuadCount = 0;
    bool m_batching = false;

    std::vector<TextureSlot> m_textures;
    std::vector<int> m_freeSlots;
    int m_liveTextures = 0;
    TextureHandle m_whiteTexture{};

    RenderQueue m_queue;
    Camera2D m_camera;
    float m_projection[16]{};
    Viewport m_viewport;
    RenderStats m_stats;
    std::string m_version;
    std::string m_rendererName;

    bool m_ready = false;
    bool m_inFrame = false;
};

core::Status GlRenderer::initialize(platform::IPlatform& platform, const RendererDesc& desc) {
    if (m_ready) {
        shutdown();
    }

    m_platform = &platform;
    m_gl = platform.glContext();
    if (m_gl == nullptr) {
        m_platform = nullptr;
        return core::errorStatus(core::error_code::kUnsupported, "the platform backend has no OpenGL support");
    }

    m_desc = desc;
    m_capacityQuads = desc.maxQuadsPerFrame < 1 ? 1 : desc.maxQuadsPerFrame;
    if (m_capacityQuads > kMaxQuadsLimit) {
        m_capacityQuads = kMaxQuadsLimit;
    }

    platform::GlContextDesc contextDesc;
    contextDesc.vsync = desc.vsync;
    contextDesc.debugContext = desc.debugContext;
    const core::Status contextStatus = m_gl->createContext(contextDesc);
    if (contextStatus.isError()) {
        m_gl = nullptr;
        m_platform = nullptr;
        return contextStatus;
    }

    const char* missing = nullptr;
    if (!detail::loadGlFunctions(m_functions, *m_gl, &missing)) {
        std::string message = "missing OpenGL entry point: ";
        message += missing != nullptr ? missing : "<unknown>";
        shutdown();
        return core::errorStatus(render_error::kGlFunctionMissing, std::move(message));
    }

    m_version = readString(gl::GL_VERSION);
    m_rendererName = readString(gl::GL_RENDERER);
    const std::string vendor = readString(gl::GL_VENDOR);

    core::Status status = createProgram();
    if (status.isError()) {
        shutdown();
        return status;
    }
    status = createGeometryBuffers();
    if (status.isError()) {
        shutdown();
        return status;
    }

    m_ready = true;

    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    TextureDesc whiteDesc;
    whiteDesc.width = 1;
    whiteDesc.height = 1;
    whiteDesc.pixels = whitePixel;
    whiteDesc.debugName = "fa-white";
    status = createTexture(whiteDesc, m_whiteTexture);
    if (status.isError()) {
        shutdown();
        return status;
    }

    core::logf(core::LogCategory::Render,
               core::LogLevel::Info,
               "OpenGL %s on %s (%s), vsync %d, %d quads/frame",
               m_version.c_str(),
               m_rendererName.c_str(),
               vendor.c_str(),
               m_gl->swapInterval(),
               m_capacityQuads);
    return core::okStatus();
}

void GlRenderer::shutdown() {
    if (m_gl != nullptr && m_functions.loaded) {
        if (m_program != 0) {
            m_functions.glDeleteProgram(m_program);
        }
        for (const TextureSlot& slot : m_textures) {
            if (slot.alive && slot.name != 0) {
                m_functions.glDeleteTextures(1, &slot.name);
            }
        }
        if (m_vertexBuffer != 0) {
            m_functions.glDeleteBuffers(1, &m_vertexBuffer);
        }
        if (m_indexBuffer != 0) {
            m_functions.glDeleteBuffers(1, &m_indexBuffer);
        }
        if (m_vertexArray != 0) {
            m_functions.glDeleteVertexArrays(1, &m_vertexArray);
        }
    }
    if (m_gl != nullptr) {
        m_gl->destroyContext();
    }

    m_functions = detail::GlFunctions{};
    m_textures.clear();
    m_freeSlots.clear();
    m_liveTextures = 0;
    m_whiteTexture = TextureHandle{};
    m_vertices.clear();
    m_indices.clear();
    m_program = 0;
    m_projectionLocation = -1;
    m_textureLocation = -1;
    m_vertexArray = 0;
    m_vertexBuffer = 0;
    m_indexBuffer = 0;
    m_batchTexture = 0;
    m_batchQuadCount = 0;
    m_batching = false;
    m_ready = false;
    m_inFrame = false;
    m_platform = nullptr;
    m_gl = nullptr;
    m_version.clear();
    m_rendererName.clear();
    m_queue.clear();
    m_stats.reset();
    m_viewport = Viewport{};
}

core::Status
GlRenderer::compileShader(gl::GLenum type, const char* source, const char* debugName, gl::GLuint& outShader) {
    outShader = 0;
    const gl::GLuint shader = m_functions.glCreateShader(type);
    if (shader == 0) {
        return core::errorStatus(render_error::kShaderCompileFailed,
                                 std::string("glCreateShader failed for ") + debugName);
    }

    m_functions.glShaderSource(shader, 1, &source, nullptr);
    m_functions.glCompileShader(shader);

    gl::GLint compiled = 0;
    m_functions.glGetShaderiv(shader, gl::GL_COMPILE_STATUS, &compiled);
    if (compiled == 0) {
        std::string message = std::string(debugName) + " failed to compile: " + shaderLog(shader);
        m_functions.glDeleteShader(shader);
        return core::errorStatus(render_error::kShaderCompileFailed, std::move(message));
    }

    outShader = shader;
    return core::okStatus();
}

core::Status GlRenderer::createProgram() {
    gl::GLuint vertexShader = 0;
    core::Status status = compileShader(gl::GL_VERTEX_SHADER, kSpriteVertexShader, "sprite.vert", vertexShader);
    if (status.isError()) {
        return status;
    }

    gl::GLuint fragmentShader = 0;
    status = compileShader(gl::GL_FRAGMENT_SHADER, kSpriteFragmentShader, "sprite.frag", fragmentShader);
    if (status.isError()) {
        m_functions.glDeleteShader(vertexShader);
        return status;
    }

    const gl::GLuint program = m_functions.glCreateProgram();
    if (program == 0) {
        m_functions.glDeleteShader(vertexShader);
        m_functions.glDeleteShader(fragmentShader);
        return core::errorStatus(render_error::kShaderLinkFailed, "glCreateProgram failed");
    }

    m_functions.glAttachShader(program, vertexShader);
    m_functions.glAttachShader(program, fragmentShader);
    m_functions.glLinkProgram(program);

    gl::GLint linked = 0;
    m_functions.glGetProgramiv(program, gl::GL_LINK_STATUS, &linked);
    const std::string linkLog = programLog(program);

    m_functions.glDeleteShader(vertexShader);
    m_functions.glDeleteShader(fragmentShader);

    if (linked == 0) {
        m_functions.glDeleteProgram(program);
        return core::errorStatus(render_error::kShaderLinkFailed, "sprite program failed to link: " + linkLog);
    }

    m_projectionLocation = m_functions.glGetUniformLocation(program, "uProjection");
    m_textureLocation = m_functions.glGetUniformLocation(program, "uTexture");
    if (m_projectionLocation < 0 || m_textureLocation < 0) {
        m_functions.glDeleteProgram(program);
        return core::errorStatus(render_error::kShaderLinkFailed, "sprite program is missing uProjection or uTexture");
    }

    m_program = program;
    return core::okStatus();
}

core::Status GlRenderer::createGeometryBuffers() {
    const std::size_t quadCount = static_cast<std::size_t>(m_capacityQuads);
    m_vertices.assign(quadCount * 4, Vertex{});

    m_indices.assign(quadCount * 6, 0);
    for (std::size_t quad = 0; quad < quadCount; ++quad) {
        const std::uint32_t base = static_cast<std::uint32_t>(quad) * 4;
        std::uint32_t* out = m_indices.data() + quad * 6;
        out[0] = base + 0;
        out[1] = base + 1;
        out[2] = base + 2;
        out[3] = base + 0;
        out[4] = base + 2;
        out[5] = base + 3;
    }

    m_functions.glGenVertexArrays(1, &m_vertexArray);
    m_functions.glGenBuffers(1, &m_vertexBuffer);
    m_functions.glGenBuffers(1, &m_indexBuffer);
    if (m_vertexArray == 0 || m_vertexBuffer == 0 || m_indexBuffer == 0) {
        return core::errorStatus(render_error::kNotInitialized, "could not allocate the sprite vertex buffers");
    }

    m_functions.glBindVertexArray(m_vertexArray);

    m_functions.glBindBuffer(gl::GL_ARRAY_BUFFER, m_vertexBuffer);
    m_functions.glBufferData(gl::GL_ARRAY_BUFFER,
                             static_cast<gl::GLsizeiptr>(m_vertices.size() * sizeof(Vertex)),
                             nullptr,
                             gl::GL_DYNAMIC_DRAW);

    m_functions.glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    m_functions.glBufferData(gl::GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<gl::GLsizeiptr>(m_indices.size() * sizeof(std::uint32_t)),
                             m_indices.data(),
                             gl::GL_DYNAMIC_DRAW);

    const gl::GLsizei stride = static_cast<gl::GLsizei>(sizeof(Vertex));
    m_functions.glEnableVertexAttribArray(0);
    m_functions.glVertexAttribPointer(
        0, 2, gl::GL_FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, x)));
    m_functions.glEnableVertexAttribArray(1);
    m_functions.glVertexAttribPointer(
        1, 2, gl::GL_FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, u)));
    m_functions.glEnableVertexAttribArray(2);
    m_functions.glVertexAttribPointer(
        2, 4, gl::GL_FLOAT, 0, stride, reinterpret_cast<const void*>(offsetof(Vertex, r)));

    m_functions.glBindVertexArray(0);
    m_functions.glBindBuffer(gl::GL_ARRAY_BUFFER, 0);
    m_functions.glBindBuffer(gl::GL_ELEMENT_ARRAY_BUFFER, 0);

    const gl::GLenum error = detail::takeGlError(m_functions);
    if (error != gl::GL_NO_ERROR) {
        return core::errorStatus(render_error::kNotInitialized,
                                 "OpenGL error while creating sprite buffers: " + hexError(error));
    }
    return core::okStatus();
}

std::string GlRenderer::shaderLog(gl::GLuint shader) const {
    gl::GLint length = 0;
    m_functions.glGetShaderiv(shader, gl::GL_INFO_LOG_LENGTH, &length);
    if (length <= 0) {
        return {};
    }
    std::string buffer(static_cast<std::size_t>(length), '\0');
    gl::GLsizei written = 0;
    m_functions.glGetShaderInfoLog(shader, length, &written, buffer.data());
    buffer.resize(written > 0 ? static_cast<std::size_t>(written) : 0);
    return buffer;
}

std::string GlRenderer::programLog(gl::GLuint program) const {
    gl::GLint length = 0;
    m_functions.glGetProgramiv(program, gl::GL_INFO_LOG_LENGTH, &length);
    if (length <= 0) {
        return {};
    }
    std::string buffer(static_cast<std::size_t>(length), '\0');
    gl::GLsizei written = 0;
    m_functions.glGetProgramInfoLog(program, length, &written, buffer.data());
    buffer.resize(written > 0 ? static_cast<std::size_t>(written) : 0);
    return buffer;
}

std::string GlRenderer::readString(gl::GLenum name) const {
    if (m_functions.glGetString == nullptr) {
        return {};
    }
    const gl::GLubyte* text = m_functions.glGetString(name);
    if (text == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(text));
}

core::Status GlRenderer::createTexture(const TextureDesc& desc, TextureHandle& outHandle) {
    if (!m_ready) {
        return core::errorStatus(render_error::kNotInitialized, "createTexture called before initialize");
    }

    const int pixelBytes = bytesPerPixel(desc.format);
    if (pixelBytes == 0) {
        return core::errorStatus(core::error_code::kInvalidArgument, "unknown texture format");
    }
    if (desc.width <= 0 || desc.height <= 0 || desc.width > kMaxTextureSize || desc.height > kMaxTextureSize) {
        return core::errorStatus(core::error_code::kInvalidArgument,
                                 "texture dimensions out of range: " + std::to_string(desc.width) + "x" +
                                     std::to_string(desc.height));
    }
    if (desc.pixels != nullptr && desc.rowPitchBytes > 0 && desc.rowPitchBytes < desc.width * pixelBytes) {
        return core::errorStatus(core::error_code::kInvalidArgument, "row pitch is smaller than one row");
    }
    if (m_freeSlots.empty() && static_cast<int>(m_textures.size()) >= kMaxTextures) {
        return core::errorStatus(render_error::kTextureCreationFailed, "texture handle limit reached");
    }

    gl::GLuint name = 0;
    m_functions.glGenTextures(1, &name);
    if (name == 0) {
        return core::errorStatus(render_error::kTextureCreationFailed, "glGenTextures returned no name");
    }

    m_functions.glActiveTexture(gl::GL_TEXTURE0);
    m_functions.glBindTexture(gl::GL_TEXTURE_2D, name);
    m_functions.glPixelStorei(gl::GL_UNPACK_ALIGNMENT, 1);

    const int rowPitch = desc.effectiveRowPitch();
    const bool needsRowLength = desc.pixels != nullptr && rowPitch != desc.width * pixelBytes;
    if (needsRowLength) {
        m_functions.glPixelStorei(gl::GL_UNPACK_ROW_LENGTH, rowPitch / pixelBytes);
    }

    const bool singleChannel = desc.format == TextureFormat::R8;
    const gl::GLenum internalFormat = singleChannel ? gl::GL_R8 : gl::GL_RGBA8;
    const gl::GLenum pixelFormat = singleChannel ? gl::GL_RED : gl::GL_RGBA;
    m_functions.glTexImage2D(gl::GL_TEXTURE_2D,
                             0,
                             static_cast<gl::GLint>(internalFormat),
                             desc.width,
                             desc.height,
                             0,
                             pixelFormat,
                             gl::GL_UNSIGNED_BYTE,
                             desc.pixels);

    if (needsRowLength) {
        m_functions.glPixelStorei(gl::GL_UNPACK_ROW_LENGTH, 0);
    }

    // Nearest + clamp is the whole reason pixel art survives the upscale.
    m_functions.glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, static_cast<gl::GLint>(gl::GL_NEAREST));
    m_functions.glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, static_cast<gl::GLint>(gl::GL_NEAREST));
    m_functions.glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::GL_CLAMP_TO_EDGE));
    m_functions.glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::GL_CLAMP_TO_EDGE));
    m_functions.glBindTexture(gl::GL_TEXTURE_2D, 0);

    const gl::GLenum error = detail::takeGlError(m_functions);
    if (error != gl::GL_NO_ERROR) {
        m_functions.glDeleteTextures(1, &name);
        return core::errorStatus(render_error::kTextureCreationFailed,
                                 "OpenGL error while uploading a texture: " + hexError(error));
    }

    TextureSlot slot;
    slot.name = name;
    slot.width = desc.width;
    slot.height = desc.height;
    slot.alive = true;

    int index = 0;
    if (!m_freeSlots.empty()) {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
        m_textures[static_cast<std::size_t>(index)] = slot;
    } else {
        index = static_cast<int>(m_textures.size());
        m_textures.push_back(slot);
    }
    ++m_liveTextures;

    outHandle = TextureHandle(static_cast<std::uint32_t>(index) + 1);
    return core::okStatus();
}

void GlRenderer::destroyTexture(TextureHandle handle) {
    const int index = slotIndexFor(handle);
    if (index < 0 || m_gl == nullptr || !m_functions.loaded) {
        return;
    }

    TextureSlot& slot = m_textures[static_cast<std::size_t>(index)];
    if (slot.name != 0) {
        m_functions.glDeleteTextures(1, &slot.name);
    }
    slot = TextureSlot{};
    m_freeSlots.push_back(index);
    --m_liveTextures;
}

core::Vec2 GlRenderer::textureSize(TextureHandle handle) const {
    const int index = slotIndexFor(handle);
    if (index < 0) {
        return core::Vec2{};
    }
    const TextureSlot& slot = m_textures[static_cast<std::size_t>(index)];
    return core::Vec2{static_cast<float>(slot.width), static_cast<float>(slot.height)};
}

int GlRenderer::slotIndexFor(TextureHandle handle) const {
    if (!handle.isValid()) {
        return -1;
    }
    const std::size_t index = static_cast<std::size_t>(handle.value() - 1);
    if (index >= m_textures.size() || !m_textures[index].alive) {
        return -1;
    }
    return static_cast<int>(index);
}

void GlRenderer::beginFrame(const Camera2D& camera) {
    if (!m_ready || m_platform == nullptr) {
        return;
    }
    if (!m_gl->makeCurrent()) {
        return;
    }

    m_camera = camera.snapped();
    m_queue.clear();
    m_stats.reset();
    m_inFrame = true;

    const platform::IWindow& window = m_platform->window();
    const int virtualWidth = static_cast<int>(camera.viewportSize().x);
    const int virtualHeight = static_cast<int>(camera.viewportSize().y);
    m_viewport = Viewport::fit(window.width(), window.height(), virtualWidth, virtualHeight);

    // Vertices are already in virtual pixels, so the projection is a plain
    // 0..size orthographic box; the camera transform happened on the CPU.
    Camera2D pixelSpace;
    pixelSpace.setViewportSize(core::Vec2{static_cast<float>(virtualWidth), static_cast<float>(virtualHeight)});
    pixelSpace.setPosition(
        core::Vec2{static_cast<float>(virtualWidth) * 0.5f, static_cast<float>(virtualHeight) * 0.5f});
    pixelSpace.projection(m_projection);

    // Letterbox bars are black, the virtual surface gets the clear colour.
    m_functions.glDisable(gl::GL_DEPTH_TEST);
    m_functions.glViewport(0, 0, window.width(), window.height());
    m_functions.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    m_functions.glClear(gl::GL_COLOR_BUFFER_BIT);

    m_functions.glViewport(
        m_viewport.offsetX, m_viewport.offsetY, m_viewport.drawableWidth(), m_viewport.drawableHeight());
    m_functions.glClearColor(m_desc.clearColor.r, m_desc.clearColor.g, m_desc.clearColor.b, m_desc.clearColor.a);
    m_functions.glClear(gl::GL_COLOR_BUFFER_BIT);

    m_functions.glEnable(gl::GL_BLEND);
    m_functions.glBlendFuncSeparate(
        gl::GL_SRC_ALPHA, gl::GL_ONE_MINUS_SRC_ALPHA, gl::GL_ONE, gl::GL_ONE_MINUS_SRC_ALPHA);

    m_functions.glUseProgram(m_program);
    m_functions.glUniformMatrix4fv(m_projectionLocation, 1, 0, m_projection);
    m_functions.glUniform1i(m_textureLocation, 0);
}

void GlRenderer::submit(const DrawItem& item) {
    if (!m_ready || !m_inFrame) {
        return;
    }
    m_queue.submit(item);
}

void GlRenderer::submitAll(std::span<const DrawItem> items) {
    if (!m_ready || !m_inFrame) {
        return;
    }
    for (const DrawItem& item : items) {
        m_queue.submit(item);
    }
}

void GlRenderer::startBatch(gl::GLuint texture) {
    m_batchTexture = texture;
    m_batchQuadCount = 0;
    m_batching = true;
}

void GlRenderer::flushBatch() {
    if (!m_batching || m_batchQuadCount == 0) {
        m_batching = false;
        m_batchQuadCount = 0;
        m_batchTexture = 0;
        return;
    }

    const std::size_t vertexCount = static_cast<std::size_t>(m_batchQuadCount) * 4;
    const std::size_t indexCount = static_cast<std::size_t>(m_batchQuadCount) * 6;

    m_functions.glBindBuffer(gl::GL_ARRAY_BUFFER, m_vertexBuffer);
    m_functions.glBufferSubData(
        gl::GL_ARRAY_BUFFER, 0, static_cast<gl::GLsizeiptr>(vertexCount * sizeof(Vertex)), m_vertices.data());

    m_functions.glActiveTexture(gl::GL_TEXTURE0);
    m_functions.glBindTexture(gl::GL_TEXTURE_2D, m_batchTexture);
    m_functions.glBindVertexArray(m_vertexArray);
    m_functions.glDrawElements(gl::GL_TRIANGLES, static_cast<gl::GLsizei>(indexCount), gl::GL_UNSIGNED_INT, nullptr);

    ++m_stats.drawCalls;
    m_stats.quads += static_cast<std::uint32_t>(m_batchQuadCount);
    m_batchQuadCount = 0;
    m_batchTexture = 0;
    m_batching = false;
}

void GlRenderer::appendQuad(const DrawItem& item) {
    const int index = slotIndexFor(item.texture);
    if (index < 0) {
        ++m_stats.droppedItems;
        return;
    }

    const TextureSlot& slot = m_textures[static_cast<std::size_t>(index)];
    if (!m_batching) {
        startBatch(slot.name);
    } else if (slot.name != m_batchTexture || m_batchQuadCount >= m_capacityQuads) {
        flushBatch();
        startBatch(slot.name);
    }

    core::Rect source = item.source;
    if (core::isEmpty(source)) {
        source = core::makeRect(0.0f, 0.0f, static_cast<float>(slot.width), static_cast<float>(slot.height));
    }
    const float textureWidth = static_cast<float>(slot.width);
    const float textureHeight = static_cast<float>(slot.height);
    float u0 = source.x / textureWidth;
    float u1 = (source.x + source.w) / textureWidth;
    float v0 = source.y / textureHeight;
    float v1 = (source.y + source.h) / textureHeight;
    if (item.flipX) {
        const float swapped = u0;
        u0 = u1;
        u1 = swapped;
    }
    if (item.flipY) {
        const float swapped = v0;
        v0 = v1;
        v1 = swapped;
    }

    const core::Vec2 position = item.position;
    const core::Vec2 size = item.size;
    core::Vec2 corners[4] = {
        m_camera.worldToPixel(position),
        m_camera.worldToPixel(core::Vec2{position.x + size.x, position.y}),
        m_camera.worldToPixel(core::Vec2{position.x + size.x, position.y + size.y}),
        m_camera.worldToPixel(core::Vec2{position.x, position.y + size.y}),
    };

    if (item.rotation != 0.0f) {
        const core::Vec2 centre{(corners[0].x + corners[2].x) * 0.5f, (corners[0].y + corners[2].y) * 0.5f};
        const float cosine = std::cos(item.rotation);
        const float sine = std::sin(item.rotation);
        for (core::Vec2& corner : corners) {
            const float dx = corner.x - centre.x;
            const float dy = corner.y - centre.y;
            corner = core::Vec2{centre.x + dx * cosine - dy * sine, centre.y + dx * sine + dy * cosine};
        }
    }

    Vertex* out = m_vertices.data() + static_cast<std::size_t>(m_batchQuadCount) * 4;
    const core::Color tint = item.tint;
    out[0] = Vertex{corners[0].x, corners[0].y, u0, v0, tint.r, tint.g, tint.b, tint.a};
    out[1] = Vertex{corners[1].x, corners[1].y, u1, v0, tint.r, tint.g, tint.b, tint.a};
    out[2] = Vertex{corners[2].x, corners[2].y, u1, v1, tint.r, tint.g, tint.b, tint.a};
    out[3] = Vertex{corners[3].x, corners[3].y, u0, v1, tint.r, tint.g, tint.b, tint.a};

    ++m_batchQuadCount;
}

void GlRenderer::endFrame() {
    if (!m_ready || !m_gl->hasContext()) {
        return;
    }
    if (!m_inFrame) {
        m_gl->swapBuffers();
        return;
    }

    m_queue.sort();
    for (const DrawItem& item : m_queue.items()) {
        appendQuad(item);
    }
    flushBatch();

    m_stats.textureSwitches = m_stats.drawCalls == 0 ? 0 : m_stats.drawCalls - 1;
    m_stats.vertices = m_stats.quads * 4;

    m_functions.glBindVertexArray(0);
    m_inFrame = false;
    m_gl->swapBuffers();
}

void GlRenderer::queueDebugItem(core::Rect rect, core::Color color) {
    if (core::isEmpty(rect)) {
        return;
    }
    DrawItem item;
    item.texture = m_whiteTexture;
    item.position = core::Vec2{rect.x, rect.y};
    item.size = core::Vec2{rect.w, rect.h};
    item.tint = color;
    item.layer = RenderLayer::UI;
    item.sortY = core::bottom(rect);
    submit(item);
}

void GlRenderer::drawDebugRect(core::Rect rect, core::Color color, float thickness) {
    if (!m_ready || !m_whiteTexture.isValid() || core::isEmpty(rect)) {
        return;
    }
    const float edge = thickness > 0.0f ? thickness : 1.0f;

    queueDebugItem(core::makeRect(rect.x, rect.y, rect.w, edge), color);
    queueDebugItem(core::makeRect(rect.x, core::bottom(rect) - edge, rect.w, edge), color);

    const float innerHeight = rect.h - edge * 2.0f;
    if (innerHeight > 0.0f) {
        queueDebugItem(core::makeRect(rect.x, rect.y + edge, edge, innerHeight), color);
        queueDebugItem(core::makeRect(core::right(rect) - edge, rect.y + edge, edge, innerHeight), color);
    }
}

void GlRenderer::drawDebugLine(core::Vec2 from, core::Vec2 to, core::Color color, float thickness) {
    if (!m_ready || !m_whiteTexture.isValid()) {
        return;
    }
    const core::Vec2 delta{to.x - from.x, to.y - from.y};
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length <= 0.0f) {
        return;
    }
    const float width = thickness > 0.0f ? thickness : 1.0f;

    // The quad rotates around its own centre, so the centre has to be the
    // midpoint of the line.
    DrawItem item;
    item.texture = m_whiteTexture;
    item.size = core::Vec2{length, width};
    item.position = core::Vec2{(from.x + to.x) * 0.5f - length * 0.5f, (from.y + to.y) * 0.5f - width * 0.5f};
    item.tint = color;
    item.rotation = std::atan2(delta.y, delta.x);
    item.layer = RenderLayer::UI;
    item.sortY = from.y > to.y ? from.y : to.y;
    submit(item);
}

std::unique_ptr<IRenderer> createRenderer() {
    return std::make_unique<GlRenderer>();
}

}  // namespace fa::render
