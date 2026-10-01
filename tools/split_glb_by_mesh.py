#!/usr/bin/env python3
"""Split a glTF/GLB into one file per top-level mesh node.

Usage: python split_glb_by_mesh.py <model.glb> <out_dir>
Writes <out_dir>/<node_name>.glb for every scene node that directly references a mesh.
The original buffer is preserved in each output so accessors/bufferViews remain valid.
"""
import json
import struct
import sys
import os

GLB_MAGIC = 0x46546C67
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


def read_glb(path):
    with open(path, "rb") as f:
        magic, ver, length = struct.unpack("<III", f.read(12))
        if magic != GLB_MAGIC:
            raise ValueError(f"{path}: not a GLB")
        chunks = {}
        while f.tell() < length:
            clen, ctype = struct.unpack("<II", f.read(8))
            chunks[ctype] = f.read(clen)
    js = json.loads(chunks[JSON_CHUNK].decode("utf-8"))
    return js, bytearray(chunks.get(BIN_CHUNK, b""))


def write_glb(path, js, bin_data):
    js_bytes = json.dumps(js, separators=(",", ":")).encode("utf-8")
    js_bytes += b" " * ((4 - (len(js_bytes) % 4)) % 4)
    bin_data = bytes(bin_data)
    bin_data += b"\x00" * ((4 - (len(bin_data) % 4)) % 4)
    total = 12 + 8 + len(js_bytes) + (8 + len(bin_data) if bin_data else 0)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", GLB_MAGIC, 2, total))
        f.write(struct.pack("<II", len(js_bytes), JSON_CHUNK))
        f.write(js_bytes)
        if bin_data:
            f.write(struct.pack("<II", len(bin_data), BIN_CHUNK))
            f.write(bin_data)


def split(path, out_dir):
    js, bin_data = read_glb(path)
    os.makedirs(out_dir, exist_ok=True)
    nodes = js.get("nodes", [])
    meshes = js.get("meshes", [])

    def mesh_node_indices():
        """Yield every node index that directly references a mesh."""
        visited = set()
        stack = list(range(len(nodes)))
        while stack:
            ni = stack.pop()
            if ni in visited:
                continue
            visited.add(ni)
            node = nodes[ni]
            if node.get("mesh") is not None:
                yield ni
            stack.extend(node.get("children", []))

    written = []
    for ni in mesh_node_indices():
        node = nodes[ni]
        mesh_idx = node["mesh"]
        mesh = meshes[mesh_idx]
        name = node.get("name", f"mesh_{mesh_idx}")
        out_name = f"{name}.glb"
        out_path = os.path.join(out_dir, out_name)

        # Build minimal glTF referencing only this mesh/node.
        new_js = dict(js)
        new_js["asset"] = {"version": "2.0"}
        new_js["nodes"] = [{"name": name, "mesh": 0}]
        if "translation" in node:
            new_js["nodes"][0]["translation"] = node["translation"]
        if "rotation" in node:
            new_js["nodes"][0]["rotation"] = node["rotation"]
        if "scale" in node:
            new_js["nodes"][0]["scale"] = node["scale"]
        new_js["meshes"] = [mesh]
        new_js["scenes"] = [{"nodes": [0]}]
        new_js["scene"] = 0

        # Keep everything needed for the mesh to render: accessors, bufferViews,
        # buffers, materials, textures, images, samplers. Skins/animations are
        # not needed for static Mesh.Static placement.
        for key in ["animations", "skins"]:
            new_js.pop(key, None)

        write_glb(out_path, new_js, bin_data)
        written.append(out_name)
        print(f"  {out_path}")
    return written


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    written = split(sys.argv[1], sys.argv[2])
    print(f"Wrote {len(written)} mesh GLBs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
