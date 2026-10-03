// ============================================================================
// PickupPanel.cpp
//
// FRAGMENT - not a standalone translation unit. Included by ../UiShell.cpp,
// which is the single TU for the whole UI layer. That is deliberate: see
// UiShell.cpp for why the layer is built as a unity build rather than as 16
// separate objects.
// ============================================================================

// =====================================================================
// Pickup Panel â€” dynamically generated from LightningScript entity registry
// =====================================================================
static const int ID_PICK_CLOSE   = 100;
static const int ID_PICKUP_BASE  = 101;

static int g_lastPickupType = 0;

void ShowPickupPanel(bool show) {
    g_editorPanels.showPickupPanel = show;
    if (g_editorPanels.hPickupPanel)
        ShowWindow((HWND)g_editorPanels.hPickupPanel, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK PickupPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Pickups", 10, 10, 100, 20, 1);
        std::vector<const EntityDef*> pickupDefs;
        LightningEntityRegistry::Instance().FindByType(EntityType::PICKUP, pickupDefs);
        int y = 35;
        for (size_t i = 0; i < pickupDefs.size(); i++, y += 28) {
            std::wstring label(pickupDefs[i]->name.begin(), pickupDefs[i]->name.end());
            CreateButton(hwnd, label.c_str(), 10, y, 160, 24, ID_PICKUP_BASE + (int)i);
        }
        CreateButton(hwnd, L"Close", 70, y + 8, 100, 28, ID_PICK_CLOSE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PICK_CLOSE) ShowPickupPanel(false);
        else if (id >= ID_PICKUP_BASE) {
            int type = id - ID_PICKUP_BASE;
            g_lastPickupType = type;
            // Resolve the def NAME here, not in the main loop. The button IDs are
            // minted from this same FindByType order (see WM_CREATE), so the lookup
            // is exact. The old handler resolved the name at DRAIN time, so a panel
            // rebuild between the click and the frame placed a different pickup.
            std::vector<const EntityDef*> pickupDefs;
            LightningEntityRegistry::Instance().FindByType(EntityType::PICKUP, pickupDefs);
            if (type < 0 || type >= (int)pickupDefs.size()) break;
            ed::PlacementRequest pr;
            pr.kind = ed::PlacementRequest::Kind::Pickup;
            pr.key  = pickupDefs[type]->name;
            ed::EventBus::instance().post(ed::Ev::BeginPlacement, pr);
        }
        break;
    }
    case WM_CLOSE:
        ShowPickupPanel(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hPickupPanel = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

