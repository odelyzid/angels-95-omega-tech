// ============================================================================
// PawnPanel.cpp
//
// FRAGMENT - not a standalone translation unit. Included by ../UiShell.cpp,
// which is the single TU for the whole UI layer. That is deliberate: see
// UiShell.cpp for why the layer is built as a unity build rather than as 16
// separate objects.
// ============================================================================

// =====================================================================
// Pawn Manager â€” Hierarchical Tree View
// =====================================================================
static const int ID_PAWN_CLOSE     = 100;
static const int ID_PAWN_SPAWN     = 103;
static const int ID_PAWN_REFRESH   = 104;
static const int ID_PAWN_SHOW_HIDDEN = 105;
static const int ID_PAWN_TREE      = 105;

static HTREEITEM AddTreeItem(HWND hTree, HTREEITEM hParent, const wchar_t* text, LPARAM lParam) {
    TVINSERTSTRUCTW tvis = {};
    tvis.hParent = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.itemex.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.itemex.pszText = const_cast<wchar_t*>(text);
    tvis.itemex.lParam = lParam;
    return (HTREEITEM)SendMessage(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

// Recursive tree filler. File scope, NOT a static member of a function-local
// struct: PopulateTreeView() rebuilt the whole tree on ID_PAWN_SHOW_HIDDEN and
// needed the same helper, and a struct declared inside PopulateTreeView was
// invisible from PawnMgrProc, so that call failed to compile.
static void AddChildren(HWND hTree, HTREEITEM hParent, const PawnTreeNode& node) {
    for (const auto& child : node.children) {
        std::wstring wlabel(child.label.begin(), child.label.end());
        // lParam encodes type + defName: "type|defName" or just "leaf|defName" or "category|"
        std::string paramStr = child.typeTag + "|" + child.defName;
        // Owned by the treeview and released with it (TVN_DESTROYED /
        // TVM_DELETEITEM reclaim it). Freed explicitly when the item is removed
        // via the destroy callback in InitActorHierarchy.
        LPARAM lParam = (LPARAM)_strdup(paramStr.c_str());
        HTREEITEM hItem = AddTreeItem(hTree, hParent, wlabel.c_str(), lParam);
        if (child.isExpanded)
            SendMessage(hTree, TVM_EXPAND, TVE_EXPAND, (LPARAM)hItem);
        AddChildren(hTree, hItem, child);
    }
}

static void PopulateTreeView(HWND hTree) {
    SendMessage(hTree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    PawnTreeNode root = BuildPawnTree();
    AddChildren(hTree, TVI_ROOT, root);
    // Expand root
    HTREEITEM hRoot = (HTREEITEM)SendMessage(hTree, TVM_GETNEXTITEM, TVGN_ROOT, 0);
    if (hRoot) SendMessage(hTree, TVM_EXPAND, TVE_EXPAND, (LPARAM)hRoot);
}

void ShowPawnManager(bool show) {
    g_editorPanels.showPawnMgr = show;
    if (g_editorPanels.hPawnMgr)
        ShowWindow((HWND)g_editorPanels.hPawnMgr, show ? SW_SHOW : SW_HIDE);
}

void RefreshPawnManager() {
    if (g_editorPanels.hPawnMgr)
        SendMessage((HWND)g_editorPanels.hPawnMgr, WM_USER + 50, 0, 0);
}

// Split a tree item's "typeTag|defName" lParam into its two parts WITHOUT
// mutating the stored buffer (mutating it broke every spawn after the first).
static void SplitTreeParam(const char* paramStr, std::string& typeTag, std::string& defName) {
    typeTag.clear();
    defName.clear();
    if (!paramStr) return;
    const char* pipe = strchr(paramStr, '|');
    if (!pipe) { typeTag = paramStr; return; }
    typeTag.assign(paramStr, pipe - paramStr);
    defName = pipe + 1;
}

void PawnManagerAddPawn(const char*, const char*) {
    // Legacy no-op â€” defs managed by PawnSystem
}

PawnTreeNode BuildPawnTree() {
    PawnTreeNode root;
    root.label = "Actor";
    root.isExpanded = true;

    // Pawn branch
    PawnTreeNode pawnBranch;
    pawnBranch.label = "Pawn";
    pawnBranch.isExpanded = true;
    pawnBranch.typeTag = "category";

    // PlayerPawn sub-branch
    PawnTreeNode playerBranch;
    playerBranch.label = "PlayerPawn";
    playerBranch.isExpanded = false;
    playerBranch.typeTag = "category";
    PawnTreeNode omegaPlayer;
    omegaPlayer.label = "AngelPlayer";
    omegaPlayer.defName = "AngelPlayer";  // Special: not in defs
    omegaPlayer.typeTag = "playerstart";
    playerBranch.children.push_back(omegaPlayer);
    pawnBranch.children.push_back(playerBranch);

    // EnemyPawn branch â€” populated from PawnSystem registered defs
    PawnTreeNode enemyBranch;
    enemyBranch.label = "EnemyPawn";
    enemyBranch.isExpanded = true;
    enemyBranch.typeTag = "category";
    const auto& defs = PawnSystem::Instance().GetDefs();
    for (const auto& def : defs) {
        PawnTreeNode leaf;
        leaf.label = def.name;
        leaf.defName = def.name;
        leaf.typeTag = "enemy";
        enemyBranch.children.push_back(leaf);
    }
    pawnBranch.children.push_back(enemyBranch);

    // InventoryPawn branch â€” pickups (data-driven from LightningScript entity registry)
    PawnTreeNode invBranch;
    invBranch.label = "InventoryPawn";
    invBranch.isExpanded = false;
    invBranch.typeTag = "category";

    PawnTreeNode pickupBranch;
    pickupBranch.label = "Pickups";
    pickupBranch.isExpanded = false;
    pickupBranch.typeTag = "category";
    {
        std::vector<const EntityDef*> pickupDefs;
        LightningEntityRegistry::Instance().FindByType(EntityType::PICKUP, pickupDefs);
        for (auto* def : pickupDefs) {
            PawnTreeNode leaf;
            leaf.label = def->name;
            leaf.defName = def->name;
            leaf.typeTag = "pickup";
            pickupBranch.children.push_back(leaf);
        }
    }
    invBranch.children.push_back(pickupBranch);

    // Weapons branch â€” weapon .ozls defs; placing one creates a weapon pickup
    // (world files represent weapons as `pickup <weaponDefName>`).
    PawnTreeNode weaponBranch;
    weaponBranch.label = "Weapons";
    weaponBranch.isExpanded = false;
    weaponBranch.typeTag = "category";
    {
        std::vector<const EntityDef*> weaponDefs;
        LightningEntityRegistry::Instance().FindByType(EntityType::WEAPON, weaponDefs);
        for (auto* def : weaponDefs) {
            PawnTreeNode leaf;
            leaf.label = def->name;
            leaf.defName = def->name;
            leaf.typeTag = "pickup";
            weaponBranch.children.push_back(leaf);
        }
    }
    pawnBranch.children.push_back(invBranch);
    pawnBranch.children.push_back(weaponBranch);

    // Volume & Node Markers branch
    PawnTreeNode volBranch;
    volBranch.label = "Volume & Node Markers";
    volBranch.isExpanded = false;
    volBranch.typeTag = "category";

    PawnTreeNode psNode;
    psNode.label = "PlayerStartNode";
    psNode.defName = "PlayerStartNode";
    psNode.typeTag = "playerstart";
    volBranch.children.push_back(psNode);

    PawnTreeNode emitBranch;
    emitBranch.label = "EmitterNode";
    emitBranch.isExpanded = false;
    emitBranch.typeTag = "category";
    const char* emitTypes[] = {"SoundEmitter", "MusicEmitter"};
    for (auto& et : emitTypes) {
        PawnTreeNode leaf;
        leaf.label = et;
        leaf.defName = et;
        leaf.typeTag = "emitter";
        emitBranch.children.push_back(leaf);
    }
    volBranch.children.push_back(emitBranch);

    PawnTreeNode zoneBranch;
    zoneBranch.label = "ZoneVolumeNode";
    zoneBranch.isExpanded = false;
    zoneBranch.typeTag = "category";
    const char* zoneTypes[] = {"ZONE_WATER", "ZONE_LADDER", "ZONE_SKY", "ZONE_REVERB", "ZONE_GAMEPLAY_SOUND"};
    for (auto& zt : zoneTypes) {
        PawnTreeNode leaf;
        leaf.label = zt;
        leaf.defName = zt;
        leaf.typeTag = "zone";
        zoneBranch.children.push_back(leaf);
    }
    volBranch.children.push_back(zoneBranch);

    // GameEngine.Mesh branch â€” places the model currently selected in the
    // Model Browser as a Mesh.Static / Mesh.Skeletal world object.
    PawnTreeNode meshBranch;
    meshBranch.label = "GameEngine.Mesh";
    meshBranch.isExpanded = false;
    meshBranch.typeTag = "category";
    {
        PawnTreeNode s;
        s.label = "Mesh.Static";
        s.defName = "Mesh.Static";
        s.typeTag = "mesh_static";
        meshBranch.children.push_back(s);
        PawnTreeNode k;
        k.label = "Mesh.Skeletal";
        k.defName = "Mesh.Skeletal";
        k.typeTag = "mesh_skeletal";
        meshBranch.children.push_back(k);
    }
    volBranch.children.push_back(meshBranch);

    // GameEngine.Light â€” light nodes. Placement writes an OZONE
    // `light <point|spot|directional>` line; every other property (colour,
    // intensity, radius, effect, flare, corona, spot cone) lives in the
    // Entity Properties panel on the selected light.
    PawnTreeNode lightBranch;
    lightBranch.label = "GameEngine.Light";
    lightBranch.isExpanded = false;
    lightBranch.typeTag = "category";
    {
        struct { const char* label; const char* defName; const char* typeTag; } lightLeaves[] = {
            { "Light.Directional", "Light.Directional", "light_directional" },
            { "Light.Point",       "Light.Point",       "light_point" },
            { "Light.Spot",        "Light.Spot",        "light_spot" },
        };
        for (auto& ll : lightLeaves) {
            PawnTreeNode leaf;
            leaf.label = ll.label;
            leaf.defName = ll.defName;
            leaf.typeTag = ll.typeTag;
            lightBranch.children.push_back(leaf);
        }
    }
    volBranch.children.push_back(lightBranch);

    // GameEngine.ParticleEmitter â€” local 3D particles (fire/sparks/smoke)
    PawnTreeNode particleLeaf;
    particleLeaf.label = "ParticleEmitter";
    particleLeaf.defName = "ParticleEmitter";
    particleLeaf.typeTag = "particle";
    volBranch.children.push_back(particleLeaf);

    // GameEngine.PathNode â€” NPC patrol waypoint
    PawnTreeNode pathLeaf;
    pathLeaf.label = "PathNode";
    pathLeaf.defName = "PathNode";
    pathLeaf.typeTag = "pathnode";
    volBranch.children.push_back(pathLeaf);

    // WindZone â€” foliage sway region
    PawnTreeNode windLeaf;
    windLeaf.label = "WindZone";
    windLeaf.defName = "WindZone";
    windLeaf.typeTag = "windzone";
    volBranch.children.push_back(windLeaf);

    pawnBranch.children.push_back(volBranch);

    // Metadata-only defs (EntityType::GAMETYPE). Not placeable; shown only
    // when the Pawn Manager's "Show Hidden" toggle is on so the tree stays
    // focused on placeable actors by default.
    if (g_editorPanels.showHidden) {
        PawnTreeNode metaBranch;
        metaBranch.label = "Metadata";
        metaBranch.isExpanded = false;
        metaBranch.typeTag = "category";
        std::vector<const EntityDef*> metaDefs;
        LightningEntityRegistry::Instance().FindByType(EntityType::GAMETYPE, metaDefs);
        for (auto* def : metaDefs) {
            PawnTreeNode leaf;
            leaf.label = def->name + " [metadata]";
            leaf.defName = def->name;
            leaf.typeTag = "metadata";
            metaBranch.children.push_back(leaf);
        }
        pawnBranch.children.push_back(metaBranch);
    }

    root.children.push_back(pawnBranch);
    return root;
}

// Spawn the currently selected Actor-Hierarchy leaf DIRECTLY into PawnSystem.
// Done synchronously inside the panel's window proc (rather than via main-loop
// action flags) so it works regardless of frame/flag timing. Returns false when
// nothing could be spawned (e.g. a category is selected, or no model chosen).
static bool SpawnSelectedPawnTreeItem(HWND hTree) {
    TVITEMW item;
    item.hItem = (HTREEITEM)SendMessage(hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
    item.mask = TVIF_PARAM;
    if (!item.hItem || !SendMessage(hTree, TVM_GETITEMW, 0, (LPARAM)&item))
        return false;

    std::string typeTag, defName;
    SplitTreeParam((const char*)item.lParam, typeTag, defName);
    if (defName.empty()) return false;

    auto& ps = PawnSystem::Instance();
    Vector3 pos = { g_editorPanels.spawnPos[0], g_editorPanels.spawnPos[1], g_editorPanels.spawnPos[2] };

    if (typeTag == "enemy") {
        ps.Spawn(pos, defName.c_str());
    } else if (typeTag == "pickup") {
        PickupNode n; n.position = pos; n.typeName = defName; ps.AddPickup(n);
    } else if (typeTag == "playerstart") {
        PlayerStartNode n; n.position = pos; n.yaw = 0.0f; ps.AddPlayerStart(n);
    } else if (typeTag == "emitter") {
        EmitterNode n;
        n.type = (defName == "MusicEmitter") ? EmitterType::MUSIC : EmitterType::SOUND;
        n.position = pos; ps.AddEmitter(n);
    } else if (typeTag == "zone") {
        ZoneVolumeNode n;
        n.bounds.min = {pos.x - 4.0f, pos.y - 2.0f, pos.z - 4.0f};
        n.bounds.max = {pos.x + 4.0f, pos.y + 2.0f, pos.z + 4.0f};
        if      (defName == "ZONE_LADDER")         n.zoneType = ZoneType::ZONE_LADDER;
        else if (defName == "ZONE_SKY")            n.zoneType = ZoneType::ZONE_SKY;
        else if (defName == "ZONE_REVERB")         n.zoneType = ZoneType::ZONE_REVERB;
        else if (defName == "ZONE_GAMEPLAY_SOUND") n.zoneType = ZoneType::ZONE_GAMEPLAY_SOUND;
        else                                        n.zoneType = ZoneType::ZONE_WATER;
        ZoneManager::Instance().AddZone(n);
    } else if (typeTag == "mesh_static" || typeTag == "mesh_skeletal") {
        int idx = g_editorPanels.selectedModel;
        if (idx < 0 || idx >= (int)g_editorPanels.modelEntries.size()) return false;
        MeshObjectNode n;
        n.meshPath = g_editorPanels.modelEntries[idx].path;
        n.skeletal = (typeTag == "mesh_skeletal");
        n.position = pos; n.yaw = 0.0f; n.scale = 1.0f;
        ps.AddMeshObject(n);
    } else if (typeTag == "light_point" || typeTag == "light_spot" ||
               typeTag == "light_directional") {
        LightNode n;
        n.active = true;
        n.position = pos;
        n.color = WHITE;
        n.intensity = 1.0f;
        n.radius = 20.0f;
        if (typeTag == "light_directional") {
            // A directional light is authored by its SOURCE point and aimed at
            // the world origin (see OzOzoneLoader), so seed it above the aim
            // point rather than at the emitter position a point light uses.
            n.type = LitLightType::DIRECTIONAL;
            n.position = {pos.x, pos.y + 40.0f, pos.z};
            n.target = {0.0f, 0.0f, 0.0f};
            n.intensity = 0.8f;
        } else if (typeTag == "light_spot") {
            n.type = LitLightType::SPOT;
            // Default aim: straight down from the emitter. inner_cone/outer_cone
            // are left at their engine defaults here and filled from the def
            // below, so Light.Spot.ozls owns the cone instead of this function.
            n.target = {pos.x, pos.y - 10.0f, pos.z};
        } else {
            n.type = LitLightType::POINT;
        }
        n.name = defName;
        // Same defaults layer the OZONE load path uses, so editing
        // Light.Spot.ozls affects newly placed lights as well as loaded worlds.
        // color / intensity / radius / position / target stay local: they are
        // line-owned on every `light` line, so no def may move them.
        ApplyLightDefDefaultsToNode(n);
        ps.AddLight(n);
    } else if (typeTag == "particle") {
        ParticleEmitterNode n;
        n.type = "fire"; n.position = pos; n.direction = {0, 1, 0};
        n.rate = 30.0f; n.lifetime = 0.9f; n.speed = 2.0f; n.spread = 0.5f;
        n.sizeStart = 0.5f; n.sizeEnd = 0.0f;
        n.colorStart = {255, 170, 60, 255}; n.colorEnd = {80, 20, 10, 0}; n.radius = 0.2f;
        ps.AddParticleEmitter(n);
    } else if (typeTag == "pathnode") {
        static int s_pathCounter = 0;
        PathNode n; n.name = "path_" + std::to_string(s_pathCounter++);
        n.position = pos; n.radius = 1.0f;
        ps.AddPathNode(n);
    } else if (typeTag == "windzone") {
        WindZoneNode n;
        n.bounds.min = {pos.x - 5.0f, pos.y - 5.0f, pos.z - 5.0f};
        n.bounds.max = {pos.x + 5.0f, pos.y + 5.0f, pos.z + 5.0f};
        n.direction = {1, 0, 0}; n.strength = 1.0f; n.frequency = 1.0f;
        ps.AddWindZone(n);
    } else if (typeTag == "metadata") {
        // Metadata-only defs (EntityType::GAMETYPE) are not placeable.
        // Clicking one is a no-op: no OZONE export entry, no placement.
        return false;
    } else {
        ps.Spawn(pos, defName.c_str());
    }
    return true;
}

// Lay the Actor-Hierarchy controls out to fill the current client area so the
// tree (and buttons) track window resizes.
static void LayoutPawnMgr(HWND hwnd, HWND hLabel, HWND hTree,
                          HWND hSpawn, HWND hRefresh, HWND hClose) {
    if (!hwnd) return;
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (hLabel) MoveWindow(hLabel, 10, 10, W - 20, 20, TRUE);
    int top = 35, btnH = 26, pad = 10;
    int treeH = H - top - (btnH + 12) - pad;
    if (treeH < 60) treeH = 60;
    if (hTree) MoveWindow(hTree, 10, top, W - 28, treeH, TRUE);
    int by = top + treeH + 6;
    if (hSpawn)   MoveWindow(hSpawn,   10, by, 110, btnH, TRUE);
    if (hRefresh) MoveWindow(hRefresh, 128, by, 80, btnH, TRUE);
    if (hClose)   MoveWindow(hClose, (W - 10 - 80 < 216) ? 216 : (W - 10 - 80), by, 80, btnH, TRUE);
}

static LRESULT CALLBACK PawnMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hTree, hLabel, hSpawn, hRefresh, hClose;
    switch (msg) {
    case WM_CREATE: {
        hLabel = CreateLabel(hwnd, L"Actor Hierarchy (select a leaf, then Spawn Selected):", 10, 10, 360, 20, 1);
        hTree = CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            10, 35, 370, 185, hwnd, (HMENU)(INT_PTR)ID_PAWN_TREE, g_hInst, nullptr);
        hSpawn   = CreateButton(hwnd, L"Spawn Selected", 10, 228, 110, 28, ID_PAWN_SPAWN);
        hRefresh = CreateButton(hwnd, L"Refresh", 128, 228, 80, 28, ID_PAWN_REFRESH);
        hClose   = CreateButton(hwnd, L"Close", 216, 228, 80, 28, ID_PAWN_CLOSE);
        CreateCtrl(hwnd, L"BUTTON", L"Show Hidden", 304, 228, 90, 24, ID_PAWN_SHOW_HIDDEN, BS_AUTOCHECKBOX);
        LayoutPawnMgr(hwnd, hLabel, hTree, hSpawn, hRefresh, hClose);
        SendMessage(hwnd, WM_USER + 50, 0, 0);
        break;
    }
    case WM_SIZE:
        LayoutPawnMgr(hwnd, hLabel, hTree, hSpawn, hRefresh, hClose);
        break;
    case WM_USER + 50: {
        PopulateTreeView(hTree);
        break;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->idFrom == ID_PAWN_TREE && nm->code == NM_DBLCLK) {
            // Informational only â€” spawning is done via the "Spawn Selected"
            // button so a context-menu right-click never adds duplicates.
            TVITEMW item;
            item.hItem = (HTREEITEM)SendMessage(hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
            item.mask = TVIF_PARAM;
            if (item.hItem && SendMessage(hTree, TVM_GETITEMW, 0, (LPARAM)&item)) {
                std::string typeTag, defName;
                SplitTreeParam((const char*)item.lParam, typeTag, defName);
                if (!defName.empty()) {
                    char msgBuf[256];
                    snprintf(msgBuf, sizeof(msgBuf),
                             "Type: %s\nDefinition: %s\n\nUse \"Spawn Selected\" to place it.",
                             typeTag.c_str(), defName.c_str());
                    MessageBoxA(hwnd, msgBuf, "Entity Info", MB_OK);
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PAWN_CLOSE) ShowPawnManager(false);
        else if (id == ID_PAWN_SHOW_HIDDEN) {
            g_editorPanels.showHidden = (SendMessage((HWND)l, BM_GETCHECK, 0, 0) == BST_CHECKED);
            PopulateTreeView(hTree);
        }
        else if (id == ID_PAWN_REFRESH) {
            SendMessage(hwnd, WM_USER + 50, 0, 0);
        } else if (id == ID_PAWN_SPAWN) {
            if (!SpawnSelectedPawnTreeItem(hTree))
                MessageBoxA(hwnd, "Select a leaf (and, for meshes, a model in the\n"
                                  "Model Browser) before spawning.", "Spawn", MB_OK | MB_ICONINFORMATION);
        }
        break;
    }
    case WM_CLOSE:
        ShowPawnManager(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hPawnMgr = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

