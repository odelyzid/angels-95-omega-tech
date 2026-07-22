# Editor Usage (AngelEd)

AngelEd is a Windows-only level editor combining Win32 native panels with a raylib 3D viewport. It requires `_WIN32` — use w64devkit or MSYS2 to build.

## Getting Started

Launch `System\AngelEd.exe`. The editor opens with:

- **3D viewport** — raylib render window (center)
- **Toolbar** — top bar with icons and mode buttons
- **Win32 panels** — dockable side windows (Model Browser, Texture Manager, etc.)

The CSG Brushes sidebar is visible by default on the left.

## Toolbar Reference

| Button | Action |
|---|---|
| New | Create a new world |
| Open | Load a world from file |
| Save | Save the current world |
| **Mod** | Toggle Model Browser panel |
| **Snd** | Toggle Sound Manager panel |
| **Tex** | Toggle Texture Manager panel |
| **Pawn** | Toggle Pawn Manager panel |
| **Scr** | Toggle Script Manager panel |
| **Lit / Unlit / Wire** | Toggle lighting mode |
| **ModeAdd/Sub/Intersect/DeResc** | CSG operation mode |
| **BBCube/Cyl/Sphere/Sheet** | Place CSG brush primitive |
| **BBTerrain** | Open Heightmap Editor |
| **Zone** | Toggle Zone Properties panel |
| **Node** | Toggle Node Placement panel |
| **Pickup** | Toggle Pickup Placement panel |
| **Play** | Launch Angels95.exe with current world |

## Lighting Modes

Three modes selectable from the toolbar:

- **Lit** — models render with the `LitFogShader` applied (lighting + fog)
- **Unlit** — all models rendered with the default unlit shader
- **Wireframe** — disables backface culling and depth mask (not true wireframe)

Toggle between modes at any time. The shader state is saved per-model and restored when switching back to Lit.

## Entity Selection (Right-Click)

Right-click on any entity in the 3D viewport to select it:

- **NPC** — click on a pawn's billboard
- **Pickup** — click on a pickup cube
- **Brush** — click on CSG collision geometry

When selected, the entity is highlighted with a pulsing yellow bounding box. A context menu appears:

| Option | Action |
|---|---|
| Properties | (future — opens entity properties dialog) |
| Delete | Removes the selected entity from the world |
| Cancel | Dismiss the context menu |

Right-click drag (when not in entity selection mode) resizes the placement ghost.

## CSG Brushes

The CSG Brushes sidebar provides:

- **Primitive buttons**: Box, Cylinder, Sphere, Pyramid, Plane
- **Operation buttons**: Add, Sub, Intersect, De-Resc
- **Edit fields**: Position (X/Y/Z), Size (W/H/D), Rotation, Scale
- **Place Brush** — commits the brush
- **Enable Collision** — toggles collision for the placed brush

The CSG operation value is stored and displayed but the backend `CsgProcessor` boolean operations are not yet fully integrated for render-time geometry.

## Panels

### Model Browser
- Lists all `.obj`/`.gltf`/`.glb`/`.iqm`/`.vox`/`.m3d` files from `GameData/` and packages
- Owner-drawn preview window shows selected model rendered in a 256×256 render target
- Select a model → click a target slot → model is placed in the world

### Texture Manager
- Lists all `.png`/`.tga`/`.bmp`/`.jpg`/`.jpeg`/`.gif` files from filesystem and packages
- Owner-drawn listbox with **64×64 thumbnail previews** next to filenames
- Click a texture → full-size preview image displayed
- Select target (Model 1–20) → click Apply → texture applied to model

### Sound Manager
- Lists all `.wav`/`.mp3`/`.ogg` files
- Category tabs: All, Sound, Music
- Preview button plays the selected sound
- Volume slider for preview volume

### Pawn Manager
- Lists registered pawn definitions (Walker, Skaarj, Brute, Floater)
- "Add Pawn" button spawns a pawn at the camera position
- Configure via: Name, Mesh, Texture, Scale

### Script Manager
- Lists `.ozls` script files from filesystem and packages
- Edit and reload scripts in-world
- Associates scripts with entity types

### Environment Settings
- **Fog**: color, density, start/end distance
- **Ambient**: color, intensity
- **Skybox**: enable/disable, tint
- **Lighting**: configure active lights
- **Zone Properties**: per-zone gameplay sound profiles

### Heightmap Editor
- Browse for a grayscale heightmap image
- Click "Generate" to build terrain mesh from the image
- Configure: position, scale, size X/Y/Z

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
| F12 | Zone Properties |

## World Saving

Worlds are saved in WDL text format and stored in `OTEditor.WorldData`. The editor also supports OZONE format for CSG brush geometry.

## Known Limitations

- Texture/Model previews require the Win32 panel to be visible (raylib render-to-texture feeds the panel)
- Heightmap Editor has no visual preview of the generated terrain
- CSG operation booleans are stored as metadata but not processed into geometry
- No undo/redo system
- No test-play save prompts ("Reload world from playtest changes?")
