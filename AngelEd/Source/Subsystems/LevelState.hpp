// ============================================================================
// Subsystems/LevelState.hpp
//
// Level state: the game type, the rules, the skyboxes and the ambient particles.
// Persisted through the OZONE `levelinfo` / `particles` lines.
//
// MOVED HERE IN R6 from UI/UiPanels.hpp. The plan document had this right and the
// code did not: "It is level state and belongs in Subsystems/LevelState; it only
// appears to live in UI/ because the deleted Zone window used to mirror it into a
// global." LevelMetadata is read by WorldIO on load and OzoneExport on save, and
// written by the Map row - none of which are UI. Its accessor was even IMPLEMENTED
// inside UI/Panels/LevelState.cpp, so the state a subsystem owns was reachable only
// because the whole UI layer is one translation unit.
//
// ParticleType moves with it for the same reason: it is the ambient-particle kind,
// i.e. level state, and it was only declared in a UI header because LevelMetadata
// lived there. Subsystems/LevelState.hpp must not include UI/UiPanels.hpp - that
// would be a Subsystems-to-UI dependency, which is the layer rule backwards.
// ============================================================================

#include "../../../Source/World/GameType.hpp"

// GameType is now defined in Source/World/GameType.hpp (oz::gametype::GameType)
// so the editor, server and tests share one source of truth. The numeric values
// are identical to the legacy enum, so every saved levelinfo line still loads.
using oz::gametype::GameType;

enum class ParticleType : uint8_t {
    NONE,
    SNOW,
    RAIN,
    VOID_REALM,
    PSYCHIC_REALM
};

// Level metadata — persisted via LevelInfo/Particles instructions in both formats
struct LevelMetadata {
    // GameType / rules
    GameType gameType = GameType::SINGLEPLAYER;
    int maxPlayers = 8;
    float respawnTime = 5.0f;
    bool timeLimitEnabled = false;
    float timeLimitMinutes = 10.0f;
    int scoreLimit = 50;
    bool friendlyFire = false;
    std::string skyboxTexturePath;
    // Side/cap skybox, the second path token on the OZONE `levelinfo` line. It
    // was previously parsed into OzonePrimitive::entitySubType and then dropped:
    // nothing read it on load and ExportToOzone never wrote it, so opening and
    // re-saving a world silently deleted it.
    std::string skyboxSidePath;
    // Ambient particles
    ParticleType particleType = ParticleType::NONE;
    float particleDensity = 50.0f;
    float particleSpeed = 1.0f;
    int particleColorR = 200, particleColorG = 200, particleColorB = 200;
    float particleWindX = 0.0f, particleWindZ = 0.0f;
};

#ifdef _WIN32
// The Map row's only writer. g_levelMeta lives in Subsystems/LevelState.cpp.
LevelMetadata GetLevelMetadata();
void SetLevelMetadata(const LevelMetadata& meta);
#else
// AngelEd requires Win32 (AngelEd/Makefile refuses to build on Linux), so these are
// unreachable stubs kept only so the non-Windows include of this header parses. Same
// pattern as the block in UI/UiPanels.hpp.
inline LevelMetadata GetLevelMetadata() { return {}; }
inline void SetLevelMetadata(const LevelMetadata&) {}
#endif