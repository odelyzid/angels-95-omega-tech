// ---------------------------------------------------------------------------
// OzoneHeightmap.cpp -- OZONE terrain (heightmap) subsystem for OzoneLoader.
//
// Extracted from OzOzoneLoader.cpp. Holds the CPU-side height grid, the
// grayscale source image, the overlay texture and the generated raylib Model,
// plus mesh (re)generation, bilinear sampling and per-cell editing used by the
// editor. All state lives on OzoneLoader (see OzOzoneLoader.hpp).
// ---------------------------------------------------------------------------
#include "OzOzoneLoader.hpp"
#include "../Log.hpp"
#include "raymath.h"
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// BuildHeightmapMesh -- build a tessellated grid mesh from a height array
// Vertices are placed with cellX/cellZ spacing, height = h * heightScale.
// Mesh is centered at origin. Normals computed from adjacent triangle faces.
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildHeightmapMesh(const std::vector<float>& heights,
                                      int gw, int gh,
                                      float cellX, float cellZ,
                                      float heightScale, float uvTileSize) {
    if (gw < 2 || gh < 2 || heights.size() < (size_t)(gw * gh))
        return Model{0};
    if (uvTileSize <= 0.0f) uvTileSize = 8.0f;

    int quadsW = gw - 1;
    int quadsH = gh - 1;
    int triCount = quadsW * quadsH * 2;
    int vertCount = triCount * 3;

    Mesh mesh = {0};
    mesh.triangleCount = triCount;
    mesh.vertexCount = vertCount;

    mesh.vertices  = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    mesh.normals   = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(vertCount * 2 * sizeof(float));

    float halfW = cellX * quadsW * 0.5f;
    float halfD = cellZ * quadsH * 0.5f;

    auto vertexPos = [&](int col, int row) -> Vector3 {
        return {
            col * cellX - halfW,
            heights[row * gw + col] * heightScale,
            row * cellZ - halfD
        };
    };

    // Compute normal for a triangle given three vertices
    auto triNormal = [](Vector3 a, Vector3 b, Vector3 c) -> Vector3 {
        Vector3 ab = {b.x - a.x, b.y - a.y, b.z - a.z};
        Vector3 ac = {c.x - a.x, c.y - a.y, c.z - a.z};
        Vector3 n = Vector3CrossProduct(ab, ac);
        return Vector3Normalize(n);
    };

    int vi = 0;
    for (int iz = 0; iz < quadsH; iz++) {
        for (int ix = 0; ix < quadsW; ix++) {
            Vector3 v00 = vertexPos(ix,     iz);
            Vector3 v10 = vertexPos(ix + 1, iz);
            Vector3 v11 = vertexPos(ix + 1, iz + 1);
            Vector3 v01 = vertexPos(ix,     iz + 1);

            // Tri 1: v00, v11, v10 -- CCW seen from above so the face
            // normal points up (lit shader needs upward normals)
            Vector3 n1 = triNormal(v00, v11, v10);
            // Tri 2: v00, v01, v11
            Vector3 n2 = triNormal(v00, v01, v11);

            // World-space tiled UVs (uvTileSize world units per repeat)
            float u0 = ((float) ix * cellX)      / uvTileSize;
            float u1 = ((float)(ix + 1) * cellX) / uvTileSize;
            float v0 = ((float) iz * cellZ)      / uvTileSize;
            float v1 = ((float)(iz + 1) * cellZ) / uvTileSize;

            // Triangle 1: v00, v11, v10
            mesh.vertices[vi * 3 + 0] = v00.x;
            mesh.vertices[vi * 3 + 1] = v00.y;
            mesh.vertices[vi * 3 + 2] = v00.z;
            mesh.normals[vi * 3 + 0] = n1.x;
            mesh.normals[vi * 3 + 1] = n1.y;
            mesh.normals[vi * 3 + 2] = n1.z;
            mesh.texcoords[vi * 2 + 0] = u0;
            mesh.texcoords[vi * 2 + 1] = v0;
            vi++;

            mesh.vertices[vi * 3 + 0] = v11.x;
            mesh.vertices[vi * 3 + 1] = v11.y;
            mesh.vertices[vi * 3 + 2] = v11.z;
            mesh.normals[vi * 3 + 0] = n1.x;
            mesh.normals[vi * 3 + 1] = n1.y;
            mesh.normals[vi * 3 + 2] = n1.z;
            mesh.texcoords[vi * 2 + 0] = u1;
            mesh.texcoords[vi * 2 + 1] = v1;
            vi++;

            mesh.vertices[vi * 3 + 0] = v10.x;
            mesh.vertices[vi * 3 + 1] = v10.y;
            mesh.vertices[vi * 3 + 2] = v10.z;
            mesh.normals[vi * 3 + 0] = n1.x;
            mesh.normals[vi * 3 + 1] = n1.y;
            mesh.normals[vi * 3 + 2] = n1.z;
            mesh.texcoords[vi * 2 + 0] = u1;
            mesh.texcoords[vi * 2 + 1] = v0;
            vi++;

            // Triangle 2: v00, v01, v11
            mesh.vertices[vi * 3 + 0] = v00.x;
            mesh.vertices[vi * 3 + 1] = v00.y;
            mesh.vertices[vi * 3 + 2] = v00.z;
            mesh.normals[vi * 3 + 0] = n2.x;
            mesh.normals[vi * 3 + 1] = n2.y;
            mesh.normals[vi * 3 + 2] = n2.z;
            mesh.texcoords[vi * 2 + 0] = u0;
            mesh.texcoords[vi * 2 + 1] = v0;
            vi++;

            mesh.vertices[vi * 3 + 0] = v01.x;
            mesh.vertices[vi * 3 + 1] = v01.y;
            mesh.vertices[vi * 3 + 2] = v01.z;
            mesh.normals[vi * 3 + 0] = n2.x;
            mesh.normals[vi * 3 + 1] = n2.y;
            mesh.normals[vi * 3 + 2] = n2.z;
            mesh.texcoords[vi * 2 + 0] = u0;
            mesh.texcoords[vi * 2 + 1] = v1;
            vi++;

            mesh.vertices[vi * 3 + 0] = v11.x;
            mesh.vertices[vi * 3 + 1] = v11.y;
            mesh.vertices[vi * 3 + 2] = v11.z;
            mesh.normals[vi * 3 + 0] = n2.x;
            mesh.normals[vi * 3 + 1] = n2.y;
            mesh.normals[vi * 3 + 2] = n2.z;
            mesh.texcoords[vi * 2 + 0] = u1;
            mesh.texcoords[vi * 2 + 1] = v1;
            vi++;
        }
    }

    // raylib 5.5's LoadModelFromMesh does NOT upload the mesh to the GPU
    // (unlike GenMesh*/LoadOBJ) -- upload explicitly or nothing will draw
    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    return model;
}

// ---------------------------------------------------------------------------
// BuildHeightmap -- load grayscale PNG, generate terrain mesh
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildHeightmap(const std::string& imagePath,
                                  const std::string& texPath,
                                  const std::vector<float>& args) {
    // args: x y z scale sizeX sizeY sizeZ
    if (args.size() < 6) return Model{0};

// Load grayscale heightmap image
    m_hmImage = LoadImage(imagePath.c_str());
    if (m_hmImage.data == 0) {
        OZ_WARN("OZONE heightmap: LoadImage failed for '%s'", imagePath.c_str());
        return Model{0};
    }
    OZ_INFO("OZONE heightmap: image loaded %dx%d from '%s'", m_hmImage.width, m_hmImage.height, imagePath.c_str());
    ImageFormat(&m_hmImage, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);

    // Load texture overlay
    m_hmTexture = LoadTexture(texPath.c_str());
    if (!m_hmTexture.id) {
        OZ_WARN("OZONE heightmap: texture load failed for '%s'", texPath.c_str());
    } else {
        // Tiled UVs need repeat wrapping; mipmaps + trilinear reduce shimmer
        SetTextureWrap(m_hmTexture, TEXTURE_WRAP_REPEAT);
        GenTextureMipmaps(&m_hmTexture);
        SetTextureFilter(m_hmTexture, TEXTURE_FILTER_TRILINEAR);
    }

    // Position (Z-up -> Y-up swap: args[1]=OZONE Y becomes raylib Z)
    m_hmPosition = {args[0], args[2], args[1]};
    m_hmScale = (args.size() > 3) ? args[3] : 1.0f;
    m_hmSize = {(args.size() > 4) ? args[4] : 100.0f,
                (args.size() > 5) ? args[5] : 50.0f,
                (args.size() > 6) ? args[6] : 100.0f};

    // Read heights from grayscale image into m_hmHeights
    m_hmGridW = m_hmImage.width;
    m_hmGridH = m_hmImage.height;
    uint8_t* pixels = (uint8_t*)m_hmImage.data;
    m_hmHeights.resize(m_hmGridW * m_hmGridH);
    for (int i = 0; i < m_hmGridW * m_hmGridH; i++)
        m_hmHeights[i] = pixels[i] / 255.0f;

    float cellX = (m_hmGridW > 1) ? (m_hmSize.x / (float)(m_hmGridW - 1)) : 0;
    float cellZ = (m_hmGridH > 1) ? (m_hmSize.z / (float)(m_hmGridH - 1)) : 0;
    Model model = BuildHeightmapMesh(m_hmHeights, m_hmGridW, m_hmGridH,
                                     cellX, cellZ, m_hmSize.y);
    if (m_hmTexture.id)
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = m_hmTexture;
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    if (GetLitFogShader().id > 0)
        model.materials[0].shader = GetLitFogShader();

    // Keep the model for rendering (Draw/DrawWorldGeometry draw m_hmModel)
    if (m_hmModel.meshCount > 0)
        UnloadModel(m_hmModel);
    m_hmModel = model;

    m_hmReady = (m_hmImage.data != 0);
    return model;
}

// ---------------------------------------------------------------------------
// UnloadHeightmap
// ---------------------------------------------------------------------------
void OzoneLoader::UnloadHeightmap() {
    if (m_hmReady) {
        UnloadModel(m_hmModel);
        if (m_hmImage.data) UnloadImage(m_hmImage);
        if (m_hmTexture.id) UnloadTexture(m_hmTexture);
        m_hmReady = false;
        m_hmImage = Image{0};
        m_hmTexture = Texture2D{0};
        m_hmModel = Model{0};
        m_hmGridW = 0;
        m_hmGridH = 0;
        m_hmHeights.clear();
    }
}

// ---------------------------------------------------------------------------
// SampleHeightmapY -- bilinear sample the OZONE-loaded heightmap
// Returns -99999.0f if no heightmap loaded or out of bounds.
// ---------------------------------------------------------------------------
float OzoneLoader::SampleHeightmapY(float px, float pz) const {
    if (!m_hmReady || m_hmImage.data == 0) return -99999.0f;

    Vector3 o = m_hmPosition;
    float scale = m_hmScale;
    float sx = m_hmSize.x * scale;
    float sz = m_hmSize.z * scale;
    int iw = m_hmImage.width;
    int ih = m_hmImage.height;
    if (iw < 1 || ih < 1) return -99999.0f;

    float hx = (px - o.x) / sx;
    float hz = (pz - o.z) / sz;
    float fx = hx * (float)(iw - 1);
    float fz = hz * (float)(ih - 1);
    int ix = (int)fx;
    int iz = (int)fz;
    if (ix < 0 || ix >= iw - 1 || iz < 0 || iz >= ih - 1)
        return o.y;

    float tx = fx - ix;
    float tz = fz - iz;
    uint8_t* p = (uint8_t*)m_hmImage.data;
    float h00 = p[iz * iw + ix] / 255.0f;
    float h10 = p[iz * iw + ix + 1] / 255.0f;
    float h01 = p[(iz + 1) * iw + ix] / 255.0f;
    float h11 = p[(iz + 1) * iw + ix + 1] / 255.0f;
    float ht = h00 * (1 - tx) * (1 - tz)
             + h10 * tx * (1 - tz)
             + h01 * (1 - tx) * tz
             + h11 * tx * tz;
    return o.y + ht * m_hmSize.y * scale;
}

// ---------------------------------------------------------------------------
// RebuildHeightmapMesh -- rebuild the GPU mesh from m_hmHeights without
// re-reading the source image. Called after SetHeightAtGrid() modifies
// individual cell heights. Preserves texture and shader.
// ---------------------------------------------------------------------------
void OzoneLoader::RebuildHeightmapMesh() {
    if (!m_hmReady || m_hmHeights.empty()) return;

    float cellX = (m_hmGridW > 1) ? (m_hmSize.x / (float)(m_hmGridW - 1)) : 0;
    float cellZ = (m_hmGridH > 1) ? (m_hmSize.z / (float)(m_hmGridH - 1)) : 0;
    Model newModel = BuildHeightmapMesh(m_hmHeights, m_hmGridW, m_hmGridH,
                                        cellX, cellZ, m_hmSize.y);

    // Save old texture and shader references
    Texture2D oldTex = m_hmModel.meshCount > 0
        ? m_hmModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture
        : Texture2D{0};
    Shader oldShader = m_hmModel.meshCount > 0
        ? m_hmModel.materials[0].shader
        : Shader{0};

    // Replace the model
    if (m_hmModel.meshCount > 0)
        UnloadModel(m_hmModel);

    m_hmModel = newModel;
    if (oldTex.id || m_hmTexture.id) {
        m_hmModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
            oldTex.id ? oldTex : m_hmTexture;
    }
    m_hmModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    if (oldShader.id > 0)
        m_hmModel.materials[0].shader = oldShader;
    else if (GetLitFogShader().id > 0)
        m_hmModel.materials[0].shader = GetLitFogShader();
}

// ---------------------------------------------------------------------------
// GetHeightAtGrid -- read height value at grid cell (col, row) in [0..1] range
// ---------------------------------------------------------------------------
float OzoneLoader::GetHeightAtGrid(int col, int row) const {
    if (col < 0 || col >= m_hmGridW || row < 0 || row >= m_hmGridH)
        return 0.0f;
    return m_hmHeights[row * m_hmGridW + col];
}

// ---------------------------------------------------------------------------
// SetHeightAtGrid -- set height at grid cell, clamp to [0..1], update image
// pixel, then rebuild the mesh
// ---------------------------------------------------------------------------
void OzoneLoader::SetHeightAtGrid(int col, int row, float height, bool triggerRebuild) {
    if (!m_hmReady || col < 0 || col >= m_hmGridW || row < 0 || row >= m_hmGridH)
        return;
    if (height < 0.0f) height = 0.0f;
    if (height > 1.0f) height = 1.0f;

    int idx = row * m_hmGridW + col;
    m_hmHeights[idx] = height;

    // Keep the grayscale image in sync (for SampleHeightmapY bilinear reads)
    if (m_hmImage.data) {
        uint8_t* p = (uint8_t*)m_hmImage.data;
        p[idx] = (uint8_t)(height * 255.0f);
    }

    if (triggerRebuild)
        RebuildHeightmapMesh();
}
