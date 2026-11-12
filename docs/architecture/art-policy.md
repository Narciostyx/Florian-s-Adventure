# 美术资源策略：色块占位优先

本文定义 v1 期间「资源长什么样」的规则。它是 M05 Asset Pipeline 与 M06 Rendering 共同依赖的约束。

## 1. 决定

**仓库可以放占位图片，但程序必须能在缺图时独立运行。** 每张纹理的加载顺序是：

1. catalog 的 `path` → `assets/textures/<类别>/<file>.png`（当前是一批已生成的像素素材）；
2. 文件缺失、无解码器或解码失败 → 用 catalog 的 `color`/`altColor`/`tile`
   **程序化生成**同样的占位。

v1 的 PNG 换成真实美术时只换文件，不改代码；即使换成正式素材，第 2 步的回落仍然保留，
冒烟、CI 与全新克隆都不依赖图片文件。app 注册了 stb_image 解码器，因此 PNG/JPG/TGA/BMP 都能读。

**看不到贴图时先查工作目录**：`data/` 与 `assets/` 都按相对路径解析，目录不对就只是
安静地退回内置 smoke catalog（前 5 条按设计不带 `path`），画面变成色块，很容易被误认为
「素材没换上」。组合根的 `adoptProjectRoot` 会向上找到含 `data/` 的祖先目录并切过去，
日志里会留下 `root: working directory ... switched to ...` 一行。目录与文件命名遵循
[编码与内容规范第 11 节](conventions.md#11-资源命名)。

理由：

- 图片优先让美术接入变成纯资源替换，不再需要改 catalog 或代码。
- 缺图回落保证冒烟、CI、全新克隆都不依赖外部文件。
- 色块/棋盘仍能验证**几何**（尺寸、锚点、瓦片对齐、碰撞盒）。

## 2. 占位规格

一张占位纹理由三个字段决定，全部写在 catalog 里：

| 字段 | 含义 |
|---|---|
| `size: [w, h]` | 占位图像的像素尺寸，必须与将来真实贴图一致 |
| `color` | 主色，`#rrggbb` 或 `#rrggbbaa` |
| `altColor` | 可选第二色；省略时等于 `color` |
| `tile` | 可选棋盘格边长；省略或 `<= 0` 表示纯色块 |

生成规则：

```text
tile <= 0         → 纯色块（color）
tile >  0         → 以 tile 为边长的双色棋盘（color / altColor）
```

| 写法 | 视觉 | 用在哪 |
|---|---|---|
| 纯色块 | 一整块颜色 | 剪影类物件：角色、标记、道具 |
| 棋盘 | 两色交错方格 | 平面类图块：地面、墙、水面 |

棋盘的意义不只是好看：**相邻同色纯色块会糊成一片**，分不清哪块是哪个瓦片、
也看不出是否对齐。地面与墙体一律用棋盘。

## 3. 颜色约定

| 用途 | 约定 | 示例 |
|---|---|---|
| 已实现、行为正确 | 语义色（草绿、土棕、肤色） | `#4a8250` 草地、`#6b4a2f` 耕地 |
| 尚未接入的占位 | **洋红 `#ff00ff`** | catalog 默认值就是洋红，忘填 `color` 会一眼看出来 |
| 调试图元（碰撞盒、锚点） | 半透明纯色 | 由 M06 的 `drawDebugRect` 画，不进 catalog |

洋红是刻意的：它是像素画里最少自然出现的颜色，出现在画面上就是「这里还缺东西」。

## 4. 尺寸必须与将来的真实贴图一致

`size` **不是**随便填的。它决定：

- 精灵在屏幕上的物理尺寸（配合 [像素度量](pixel-metrics.md)）；
- 碰撞盒与锚点的相对位置；
- M06 的整数缩放是否会把精灵切成半像素。

因此改 `size` 等于改玩法手感，必须同时检查 [像素度量](pixel-metrics.md) 的瓦片与碰撞盒基准。
角色占位当前是 16×24，而脚部碰撞盒是 10×6——**这两个数字都要在接入真实美术时保持不变**，
否则所有已经调好的移动与碰撞都要重调。

## 5. 真实美术怎么接进来

真实图片到位时**不改代码**，只改 catalog：

```json
{ "id": "tex.ground.grass", "kind": "texture", "path": "textures/tiles/grass.png",
  "size": [16, 16], "tile": 8, "color": "#4a8250", "altColor": "#5c8a42" }
```

规则：

1. 加上 `path` 后，`color` / `altColor` / `tile` **保留**——它们是「文件读不到时的兜底」，
   删掉等于放弃降级能力。
2. 注册一个 `IImageDecoder`。app 已用 stb_image 注册 PNG/JPG/TGA/BMP 解码器；
   其他宿主需要自己注册。
3. `size` 必须与图片实际尺寸一致；不一致时以图片为准，但要在同一批改动里同步 catalog 与玩法数值。

因为解码器是注入的，**M05 与 M06 都不需要改**；这也是当初拒绝「M05 直接调 SDK 解码」的原因。

## 6. 当前 catalog 清单

`data/catalog.json` 当前有 42 条纹理，全部带 `path` 指向 `assets/textures/<类别>/` 下的 PNG
（下表只列最早的 5 条基准项，
其余是三种作物 × 4 阶段、第二个敌人、挥砍特效、9 张 `tex.item.*` 物品图标、四向行走/待机/划船
角色表、敌人动画表、水/船/菜单素材与花/树/石装饰瓦片；新增流程见
[地图组件协作指南](../guide/adding-map-components.md)）：

| id | 尺寸 | 文件 | 用途 |
|---|---|---|---|
| `tex.ground.grass` | 16×16 | `textures/tiles/grass.png` | 草地瓦片 |
| `tex.ground.soil` | 16×16 | `textures/tiles/soil.png` | 耕地瓦片 |
| `tex.ground.wall` | 16×16 | `textures/tiles/wall.png` | 墙/树 |
| `tex.actor.player` | 16×24 | `textures/characters/player.png` | 玩家 |
| `tex.marker` | 8×8 | `textures/ui/marker.png` | 鼠标位置标记 / 调试图元底色 |

`tex.item.*` 物品图标已接入 **M09 的 `UiContext::icon`**：组合根按 `item.<x> → tex.item.<x>`
解析句柄（三种种子没有专属图标，复用 `tex.crop.<x>_seed` 的播种贴图），背包面板的
背包列与快捷栏列都在每行左侧画 16×16 图标。解析不到图标时画洋红块，符合第 3 节的约定。
商店与任务日志仍只显示文字，接图标是后续工作。

新增资源时按 模块文档 的校验规则填写：
`id` 唯一、`kind` 合法、path-less 纹理必须给 `size`、颜色必须是合法 HEX。

### 6.1 UI 字体图集

界面文字用的不是贴图而是 `assets/fonts/` 下的一对文件，同样受「只提交运行时资源」约束：

| 文件 | 体积 | 来源 |
|---|---|---|
| `ui_atlas.png` | 66×691（471 字形），约 12 KB | `tools/build_font_atlas.py` 离线烘焙 |
| `ui_atlas.json` | 约 55 KB | 同上，逐字形度量 |
| `OFL.txt` | 4.4 KB | Fusion Pixel 的 SIL OFL 1.1 许可与署名 |

字体是 **Fusion Pixel Font**（<https://github.com/TakWolf/fusion-pixel-font>，SIL OFL 1.1，
2026-10-30 取用时的最新 release，10px monospaced zh_hans 变体）。源 TTF 约 4 MB，**不入库**：
它是未压缩的字体源文件，违反第 7 节；仓库里只放烘焙出来的位图，需要加字时按
M09 第 6 节 重新生成。

按 OFL 要求，再分发字形时必须附带许可文本，所以 `OFL.txt` 与图集同目录。

## 7. 不做的事

| 不做 | 原因 |
|---|---|
| 提交大尺寸/未压缩的美术源文件 | 仓库只放运行时 PNG；`.aseprite` 等源文件与 LFS 策略等正式美术阶段再定，目录结构已按[规范第 11 节](conventions.md#11-资源命名)固定 |
| 提交 4 MB 的字体 TTF | 只提交离线烘焙的 `assets/fonts/ui_atlas.*`（约 50 KB）与 `OFL.txt`，见第 6.1 节 |
| 用字体图标 / emoji 当占位 | 渲染器不画文字（文字归 M09），色块是唯一能画的形态 |
| 程序化生成「像样的」贴图（噪声草地等） | 会掩盖对齐问题；色块最容易暴露尺寸与排序错误 |
| 占位尺寸随窗口/缩放变化 | 尺寸是玩法参数，不是显示参数 |
