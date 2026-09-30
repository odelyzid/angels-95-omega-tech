#!/usr/bin/env python3
"""Strip skinning + animations from a glTF/GLB, producing a plain static mesh.

The FBX2glTF weapon exports contain multiple skins (one per mesh part). raylib
only supports one skin per model and logs/behaves inconsistently with more than
one, which can crash the client. Since the view-model is rendered static anyway,
we drop the skin bindings and animation channels so raylib loads a clean,
single-skin-free static model (the mesh geometry is already in the correct bind
space, so the visual result is unchanged).

Usage: python flatten_glb.py <model.glb>
Edits the file in place.
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
        order = []
        while f.tell() < length:
            clen, ctype = struct.unpack("<II", f.read(8))
            chunks[ctype] = f.read(clen)
            order.append(ctype)
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


def flatten(path):
    js, bin_data = read_glb(path)
    removed_anim = len(js.get("animations", []))
    removed_skins = len(js.get("skins", []))

    js.pop("animations", None)
    js.pop("skins", None)

    # Nodes: drop the skin binding (now a regular node the mesh transform applies to).
    for n in js.get("nodes", []):
        n.pop("skin", None)

    # Mesh primitives: drop the skinning vertex attributes.
    for m in js.get("meshes", []):
        for p in m.get("primitives", []):
            attrs = p.get("attributes", {})
            for k in [k for k in attrs if k.startswith("JOINTS_") or k.startswith("WEIGHTS_")]:
                attrs.pop(k, None)

    # 'animations'/'skins' may be referenced by names arrays elsewhere; nothing else uses them.
    js.pop("skins", None)

    write_glb(path, js, bin_data)
    print(f"flattened {os.path.basename(path)}: -{removed_anim} anims, -{removed_skins} skins")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for p in sys.argv[1:]:
        flatten(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
