#include "raylib.h"

// Legacy array replaced by PawnSystem dynamic entity manager.
// EnemyTexture retained for legacy texture/sound handle loading.
class EnemyTexture{
    public:
        Texture2D Frame1;
        Sound Scream;
};

static EnemyTexture EnemyTextures;