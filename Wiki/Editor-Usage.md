# Editor Usage (AngelEd)

AngelEd is a Windows-only level editor combining Win32 native panels with a raylib 3D viewport. It requires `_WIN32` — use w64devkit or MSYS2 to build.

## Getting Started

Launch `System\AngelEd.exe`. The editor opens with:

- **3D viewport** — raylib render window (center)
- **Menu bar** — File/View/Camera/Settings/Help menus
- **Win32 panels** — dockable side windows (Model Browser, Texture Manager, etc.)

## File Formats

Supports both WDL (`.wdl`) and OZONE (`.ozone`) world formats. Open/Save dialogs accept both. OZONE export includes CSG brush geometry and entity definitions in a combined plain-text format.

## Menu Bar Reference

### File
| Item | Shortcut | Action |
|---|---|---|
| New | N | Create a new world |
| Open... | O | Load a world from file |
| Save | S | Save the current world |
| Save As... | | Save to a new path |
| Play Test | P | Launch Angels95.exe with the current world |
| Exit | Q | Close editor |

### View
| Item | Shortcut | Action |
|---|---|---|
| Model Browser | F5 | Toggle model browser |
| Sound Manager | F6 | Toggle sound manager |
| Texture Manager | F7 | Toggle texture manager |
| Pawn Manager | F8 | Toggle pawn manager |
| Script Manager | F9 | Toggle script manager |
| Zone Properties | F12 | Toggle zone/env panel |
| Node Panel | | Toggle node placement |
| Pickups | F10 | Toggle pickup panel |
| Light Properties | | Toggle light properties |
| Heightmap Editor | H | Toggle heightmap editor |
| World Graph Explorer | | Toggle world graph |

### Camera
| Item | Shortcut | Action |
|---|---|---|
| Reset Camera | Home | Reset to default position |
| Top | Numpad 7 | Orthographic top-down view |
| Bottom | Numpad 1 | Orthographic bottom-up view |
| Right | Numpad 3 | Orthographic right view |
| Left | Numpad 9 | Orthographic left view |
| Perspective | Numpad 5 | Restore perspective view |

## Entity Selection (Click + Right-Click)

Click on any entity in the 3D viewport to select it (highlighted red):

- **Brush** — click on CSG collision geometry / OZONE primitives
- **Model** — click on a placed 3D model
- **NPC** — click on a pawn's billboard
- **Pickup** — click on a pickup node
- **Light** — click on a light node
- **Zone** — click on a zone volume boundary
- **Spawn** — click on a player start node

**Right-click** a selected entity to open the native context menu:

| Option | Action |
|---|---|
| Properties | Opens the Properties panel with entity details |
| Delete | Removes the selected entity from the world |
| Duplicate | Creates a copy offset 2 units on X+Z |
| Apply Texture to Surface | Applies the currently selected texture (if a texture is active in the Texture Manager and the target is a Brush or Model) |

Right-click drag (without an entity under the cursor) orbits the camera.

## Lighting Modes

| Button | Action |
|---|---|
| Lit | Models render with LitFogShader (lighting + fog) |
| Unlit | Models render with default unlit shader |

Toggle between modes from the ViewMode toolbar combo.

## CSG Brushes

The CSG Brushes panel provides:

- **Primitive buttons**: Box, Cylinder, Sphere, Pyramid, Plane
- **Operation dropdown**: Add (0), Sub (1), Intersect (2), De-Resc (3)
- **Edit fields**: Position (X/Y/Z), Size (W/H/D), Rotation, Scale
- **Place Brush** — commits the brush to the collision volume list
- **Enable Collision** — toggle collision for placed brush

The CSG operation value is stored but the backend `CsgProcessor` boolean operations are not yet integrated for render-time geometry.

## Panels

### Model Browser
- Lists all `.obj`/`.gltf`/`.glb`/`.iqm`/`.vox`/`.m3d` files from `GameData/` and packages
- Previews selected model in a 256x256 render texture
- Select a model, click a target slot to place in world

### Texture Manager
- Lists all `.png`/`.tga`/`.bmp`/`.jpg`/`.jpeg` files from filesystem and packages
- **Grid view** with 64x64 thumbnail previews in a custom scrollable control
- Click a texture to see full-size preview and file info
- Select target model from dropdown (populated from loaded world models)
- Click **Apply** to set texture on model; **Apply to All** checkbox applies to all model instances
- **Add Package** button loads additional `.oztex`/`.ozpak` files at runtime

### Sound Manager
- Lists `.wav`/`.mp3`/`.ogg` files organized by category tabs: **SFX** / **Music** / **Ambience**
- Source path shown for each category
- Volume slider for preview volume
- **Loop** checkbox for continuous playback
- **Play** / **Stop** / **Refresh** buttons

### Pawn Manager
- Hierarchical **tree view** of all actor types:
  - **PlayerPawn** > OmegaPlayer (player start)
  - **EnemyPawn** > registered NPC defs (Walker, Skaarj, Brute, Floater, etc.)
  - **InventoryPawn** > Pickups (from LightningScript registry) + Weapons
  - **Volume & Node Markers** > PlayerStartNode, EmitterNodes (Sound/Music), ZoneVolumeNode types (Water/Ladder/Sky/Reverb/GameplaySound)
- Double-click a leaf node to view entity info
- **Spawn Selected** places the chosen actor at camera position
- **Refresh** reloads the tree from current definitions

### Script Manager
- Lists `.ps`/`.wdl`/`.ozone` files from `GameData/` and packages
- Double-click to view file info and path

### Zone Properties / Environment Settings
- **Fog**: color, density, start/end distance
- **Ambient**: color, intensity
- **Game Type**: Singleplayer, Coop, Etheral Match, Angel Team Game, Angel Run, Capture the Orb, Time Shift
- **Max Players**, **Respawn Time**, **Time Limit**, **Score Limit**, **Friendly Fire**
- **Particles**: type (None/Snow/Rain/Void Realm/Psychic Realm), density, speed, color, wind
- **Skybox**: custom skybox texture path
- Per-zone overrides for fog/ambient/reverb when editing zone volumes

### Pickup Panel
- Select and place pickup nodes by type (from LightningScript entity registry)
- Configures `actionPickupType` for the main loop

### Node Panel
- Place node markers: Player Start, NPC Spawn, Point Light, Zone Volume
- Configures `actionNodeType` for the main loop

### Heightmap Editor
- Browse for grayscale heightmap image
- Browse for terrain texture overlay
- Configure: position (X/Y/Z), size (Sx/Sy/Sz), scale
- Click **Generate** to build terrain mesh

### Light Properties
- Configure point lights: color (R/G/B), intensity, radius
- Light type: directional, point, spot
- Light effect: none, watery, torch, fire, lamp
- Toggle: flare, corona

### World Graph Explorer
- Lists all placed models in the current world
- Shows each model's position, rotation, scale, and name
- Click to select/jump to that model in the viewport

### Properties Panel
- Context-sensitive panel showing selected entity details
- Edit position (X/Y/Z), scale, rotation
- For brushes/zones: edit size (W/H/D)
- Texture mapping: U/V scale and offset

## Keyboard Shortcuts

| Key | Action |
|---|---|
| U/J | Move placement ghost X |
| H/K | Move placement ghost Z |
| Y/I | Move placement ghost Y |
| O/L | Rotate placement ghost |
| T/G | Scale placement ghost up/down |
| Enter | Commit placement |
| Double-click | Commit placement |
| Middle Mouse | Pan camera |
| Shift+Middle Mouse | Pan camera vertically |
| Alt+Middle Mouse | Orbit camera |
| Scroll Wheel | Dolly camera (zoom) |
| Home | Reset camera position |
| F5 | Model Browser |
| F6 | Sound Manager |
| F7 | Texture Manager |
| F8 | Pawn Manager |
| F9 | Script Manager |
| F10 | Pickup Panel |
| F11 | Toggle Fullscreen |
| F12 | Zone Properties |
| H | Heightmap Editor |
| C | Toggle collision visibility |

## World Saving

Worlds are saved in WDL or OZONE text format stored in `OTEditor.WorldData`. OZONE export includes CSG brush primitives, heightmap, and all entity types (player starts, pickups, NPCs, zones, emitters) with per-zone environment overrides.

## Known Limitations

- Lighting toggle (Lit/Unlit) does not actually unset shader from model materials
- CSG operation booleans are stored as metadata but not processed into geometry by the backend `CsgProcessor`
- No undo/redo system
- No test-play save prompts ("Reload world from playtest changes?")
- Model/Texture preview rendering requires the raylib viewport to be focused
- Lighting effects (watery, torch, fire, lamp) are UI only — not rendered in viewport
