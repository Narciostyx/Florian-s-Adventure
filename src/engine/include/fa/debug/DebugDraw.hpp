#pragma once

#include <span>

namespace fa::physics {
struct DebugShape;
}

namespace fa::render {
class IRenderer;
}

namespace fa::debug {

/// \brief Submits M13 debug shapes through M06's debug draw helpers.
///
/// M13 produces geometry and never touches GL; M27 is the adapter that turns that
/// list into renderer calls. The caller decides when physics debugging is visible.
void drawPhysicsShapes(render::IRenderer& renderer, std::span<const physics::DebugShape> shapes);

}  // namespace fa::debug
