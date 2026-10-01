#!/usr/bin/env python3
"""Report a GLB's per-node world-space bounding box.

glTF stores an exact accessor min/max for every POSITION attribute, so the
bounds are read from the file rather than triangulated. Node transforms from
the scene graph are composed so the result is a world-space AABB, which is
what OZONE placement math needs.

Used by tools/worldcheck.cpp (via --glb-bounds JSON) and standalone:

    python tools/glb_bounds.py GameData/Worlds/World_endless_snow/Models/industrial/Building_A.glb
"""
import glob
import json
import os
import struct
import sys

COMP = {
    5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
    5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4),
}


def _minmax(node, gltf, accessors, out):
    """Collect THIS node's own POSITION accessor bounds.

    Deliberately does not recurse: the caller walks the scene graph itself and
    pushes each child onto its stack, so recursing here would visit every
    descendant twice. The duplicate visit is not harmless -- it re-transforms
    the same accessor under the parent's world matrix, which corrupts the
    resulting AABB with untransformed coordinates and inflates the reported
    size (a 7u rock once measured as 336u).
    """
    if "mesh" in node:
        for prim in gltf["meshes"][node["mesh"]]["primitives"]:
            idx = prim.get("attributes", {}).get("POSITION")
            if idx is None:
                continue
            acc = accessors[idx]
            if "min" in acc and "max" in acc:
                out.append(tuple(acc["min"]) + tuple(acc["max"]))


def _matrix(node):
    """Local TRS or 'matrix' as a row-major 4x4 (glTF matrices are column-major)."""
    if "matrix" in node:
        m = node["matrix"]
        return [[m[0], m[4], m[8], m[12]],
                [m[1], m[5], m[9], m[13]],
                [m[2], m[6], m[10], m[14]],
                [m[3], m[7], m[11], m[15]]]
    t = node.get("translation", [0, 0, 0])
    r = node.get("rotation", [0, 0, 0, 1])
    s = node.get("scale", [1, 1, 1])
    x, y, z, w = r
    rot = [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]
    m = [[rot[r][c] * s[c] for c in range(3)] + [t[r]] for r in range(3)]
    m.append([0.0, 0.0, 0.0, 1.0])
    return m


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)]
            for i in range(4)]


def _apply(m, v):
    return [sum(m[i][j] * v[j] for j in range(3)) + m[i][3] for i in range(3)]


def glb_bounds(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"glTF":
        raise ValueError("not a GLB")
    version, total = struct.unpack_from("<II", data, 4)
    if version != 2:
        raise ValueError("glTF v%d unsupported" % version)
    offset, gltf, binary = 12, None, None
    while offset < min(total, len(data)):
        length, kind = struct.unpack_from("<II", data, offset)
        chunk = data[offset + 8: offset + 8 + length]
        if kind == 0x4E4F534A:
            gltf = json.loads(chunk.decode("utf-8"))
        elif kind == 0x004E4942:
            binary = chunk
        offset += 8 + length + ((4 - length % 4) % 4 if length % 4 else 0)
    if gltf is None:
        raise ValueError("no JSON chunk")
    accessors = gltf.get("accessors", [])

    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    names = []
    scene = gltf.get("scenes", [{}])[gltf.get("scene", 0)]
    stack = [(gltf["nodes"][i], _identity()) for i in reversed(scene.get("nodes", []))]
    while stack:
        node, parent = stack.pop()
        world = _mul(parent, _matrix(node))
        raw = []
        _minmax(node, gltf, accessors, raw)
        for mn0, mn1, mn2, mx0, mx1, mx2 in raw:
            lo_v, hi_v = (mn0, mn1, mn2), (mx0, mx1, mx2)
            for cx in (lo_v[0], hi_v[0]):
                for cy in (lo_v[1], hi_v[1]):
                    for cz in (lo_v[2], hi_v[2]):
                        p = _apply(world, [cx, cy, cz])
                        for a in range(3):
                            lo[a] = min(lo[a], p[a])
                            hi[a] = max(hi[a], p[a])
            # Once per node-with-mesh, not once per corner.
            names.append(node.get("name", "<unnamed>"))
        for child in reversed(node.get("children", [])):
            stack.append((gltf["nodes"][child], world))
    if lo[0] == float("inf"):
        raise ValueError("no POSITION accessors")
    return {"path": path, "nodes": names, "min": lo, "max": hi,
            "size": [hi[i] - lo[i] for i in range(3)],
            "center": [(lo[i] + hi[i]) / 2 for i in range(3)]}


def _identity():
    return [[1.0, 0, 0, 0], [0, 1.0, 0, 0], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]


def emit_json(world_dir, out_path):
    """Write a worldcheck --glb-bounds JSON covering every GLB under world_dir.

    Paths are stored relative to world_dir so the file stays valid regardless
    of where the repo is checked out.

        python tools/glb_bounds.py --emit-json GameData/Worlds/<World> <out.json>
    """
    world_dir = world_dir.rstrip("/\\")
    pattern = world_dir + "/Models/**/*.glb"
    entries = []
    for path in sorted(glob.glob(pattern, recursive=True)):
        b = glb_bounds(path)
        rel = os.path.relpath(path, world_dir).replace("\\", "/")
        b["path"] = rel
        entries.append(b)
    if not entries:
        raise ValueError("no GLBs found under %s" % world_dir)
    with open(out_path, "w", encoding="utf-8") as fh:
        json.dump(entries, fh, indent=1)
        fh.write("\n")
    return entries


if __name__ == "__main__":
    argv = sys.argv[1:]
    if argv and argv[0] == "--emit-json":
        if len(argv) != 3:
            sys.exit("usage: glb_bounds.py --emit-json <world-dir> <out.json>")
        rows = emit_json(argv[1], argv[2])
        print("wrote %d model bounds to %s" % (len(rows), argv[2]))
        sys.exit(0)
    for arg in argv:
        b = glb_bounds(arg)
        print("%s\n  nodes: %s\n  min: %s\n  max: %s\n  size: %s"
              % (b["path"], ", ".join(b["nodes"]),
                 ["%.3f" % v for v in b["min"]],
                 ["%.3f" % v for v in b["max"]],
                 ["%.3f" % v for v in b["size"]]))