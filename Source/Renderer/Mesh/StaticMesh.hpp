#pragma once
#include "Mesh.hpp"

namespace oz {

// GameEngine.Mesh.Static — non-animated props and map objects.
class StaticMesh : public Mesh {
public:
    StaticMesh() = default;
    ~StaticMesh() override = default;
};

} // namespace oz
