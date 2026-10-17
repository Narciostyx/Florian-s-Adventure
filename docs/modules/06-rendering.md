# M06 Rendering

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | M00–M05 |
| 被依赖 | M09、M22、M26、M27 |

## 1. 目的

把 OpenGL 4.1 Core 封装成 2D 像素游戏需要的渲染接口：纹理、精灵批处理、相机、图层排序、虚拟分辨率、文字与调试绘制。它是唯一直接调用 OpenGL 的模块。

## 2. 范围

**负责**

- GL 上下文创建（通过 M02 的 native handle）、加载 glad、设置视口/清屏。
- 纹理创建/上传/销毁、纹理参数（nearest、clamp）。
- 精灵批处理：动态 VBO、索引缓冲、按纹理/混合状态分批。
- `Camera2D`：位置、缩放、视口、世界↔屏幕转换。
- `RenderQueue`：收集 `DrawItem`，按 layer / sortY / entityId 稳定排序。
- 虚拟分辨率与整数缩放；像素对齐。
- 低层文字绘制（字形四边形）；布局由 M09 负责。
- 调试线框/矩形绘制（M27 使用）。

**不负责**

- 场景图、实体遍历（M11 的 `renderExtract` 提交绘制项）。
- UI 布局/控件（M09）。
- 资源加载/解码（M05）。
- 光照模型/后处理（后续扩展）。

## 3. 核心概念与数据结构

```cpp
enum class RenderLayer {
    Background, Ground, GroundDetail, Objects, Actors, Foreground, Weather, UI
};

struct DrawItem {
    TextureHandle texture;
    glm::vec4 sourceRect;   // 纹理像素
    glm::vec2 position;     // 世界坐标
    glm::vec2 size;
    glm::vec4 tint;
    float rotation;
    RenderLayer layer;
    float sortY;
    EntityId entity;
};

class Camera2D {
public:
    glm::vec2 position;
    float zoom;
    glm::vec2 viewportSize;
};
```

- 排序键：`(layer, sortY, entityId)`；同层 y 大者后绘制。
- 批处理以纹理与混合模式为断点；同纹理连续绘制项合并。
- 顶点格式：位置、UV、颜色；使用 `glm::vec4`/`vec2` 直接上传。

### 像素基准（ADR-0003）

- 虚拟分辨率 640×360，只允许整数缩放 + letterbox，禁止线性过滤。
- 瓦片 16×16；精灵以脚底中心对齐到实体位置（feet anchor）。
- `Constants.hpp` 定义：`kTileSize = 16`、`kVirtualWidth = 640`、`kVirtualHeight = 360`。
- 相机不支持自由缩放，仅平移 + 整数倍适配窗口。
- 不提供半像素采样；`sortY` 取整后再排序，避免遮挡闪烁。

## 4. 公共接口草案

```cpp
namespace fa::render {

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual Result<void> initialize(IWindow& window) = 0;
    virtual void shutdown() = 0;
    virtual void beginFrame(const Camera2D& camera) = 0;
    virtual void submit(const DrawItem& item) = 0;
    virtual void flush() = 0;
    virtual void endFrame() = 0;
    virtual Result<TextureHandle> createTexture(const TextureDesc& desc) = 0;
    virtual void drawText(const TextDrawCmd& cmd) = 0;
};

} // namespace fa::render
```

## 5. 依赖关系

- 依赖 M01（数学/错误）、M02（窗口）、M05（纹理资源句柄）。
- 只被 L2+ 的提取阶段与 M09/M27 使用；不反向依赖玩法。
- GL 类型只在 `src/` 实现中出现；公共头使用 `TextureHandle` 不透明句柄。

## 6. 扩展点

- 新增批处理类型（九宫格、粒子、光照）作为独立 `RenderPass`。
- 新增着色器/材质：`Material` 描述 + `ShaderProgram` 缓存；v1 只用内置精灵着色器。
- 新增渲染目标：离屏纹理、屏幕后处理；为光照/缩放预留。
- 新增调试绘制：线、圆、文本覆盖层。

## 7. 实现阶段

P0，Phase A。先实现：GL 上下文、清屏、纹理上传、单纹理精灵批处理、相机、虚拟分辨率。验收：显示一张 PNG、移动相机、绘制 1000 个精灵稳定 60 FPS、像素无模糊。

## 8. 风险与开放问题

- SDL3 + OpenGL 上下文创建在 Windows 上需确认（WGL/GLAD 加载顺序）。
- 批处理排序与纹理切换的平衡；v1 先保证正确性，再优化。
- 旋转精灵的像素对齐会破坏像素风；默认只允许 90° 倍数旋转，自由旋转用于特效。
- 文字渲染的字体图集与像素字体需要单独规范（M09）。
