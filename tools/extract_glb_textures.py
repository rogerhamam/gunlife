"""Pull the base-colour texture out of a .glb and write it as a PNG.

raylib is built without JPEG support, and the Meshy exports store their
base_color map as JPEG inside the GLB -- so the models load geometry fine but
come out flat white. This extracts each model's base_color image and writes it
next to the .glb as <name>_albedo.png, which the game loads and binds to the
material at startup.

  python tools/extract_glb_textures.py
"""
import io
import json
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS = os.path.join(ROOT, "assets", "models")

GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942


def read_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, version, total = struct.unpack("<III", data[:12])
    if magic != GLB_MAGIC:
        raise SystemExit(f"{path}: not a GLB")
    js, binary = None, b""
    off = 12
    while off < total:
        clen, ctype = struct.unpack("<II", data[off:off + 8])
        chunk = data[off + 8:off + 8 + clen]
        if ctype == CHUNK_JSON:
            js = json.loads(chunk)
        elif ctype == CHUNK_BIN:
            binary = chunk
        off += 8 + clen
    return js, binary


def image_bytes(js, binary, index):
    img = js["images"][index]
    if "bufferView" not in img:
        raise SystemExit("image is a URI, not embedded -- unsupported here")
    bv = js["bufferViews"][img["bufferView"]]
    start = bv.get("byteOffset", 0)
    return binary[start:start + bv["byteLength"]]


def texture_indices(js):
    """{slot: image index} for every map the material references."""
    out = {}
    for mat in js.get("materials", []):
        pbr = mat.get("pbrMetallicRoughness", {})
        for slot, holder, key in (
                ("albedo", pbr, "baseColorTexture"),
                ("metalrough", pbr, "metallicRoughnessTexture"),
                ("normal", mat, "normalTexture"),
                ("emissive", mat, "emissiveTexture"),
                ("occlusion", mat, "occlusionTexture")):
            tex = holder.get(key)
            if tex is not None and slot not in out:
                out[slot] = js["textures"][tex["index"]]["source"]
    # Anything embedded but not referenced still gets written, by its own name.
    for i, img in enumerate(js.get("images", [])):
        name = (img.get("name") or "").lower()
        for slot, needle in (("albedo", "base_color"), ("normal", "normal"),
                             ("emissive", "emissive"),
                             ("metalrough", "metallic")):
            if needle in name and slot not in out:
                out[slot] = i
    return out


def main():
    if not os.path.isdir(MODELS):
        sys.exit(f"no {MODELS}")
    count = 0
    for f in sorted(os.listdir(MODELS)):
        if not f.lower().endswith(".glb"):
            continue
        path = os.path.join(MODELS, f)
        stem = os.path.splitext(f)[0]
        js, binary = read_glb(path)
        maps = texture_indices(js)
        if not maps:
            print(f"{f}: no textures found, skipped")
            continue
        for slot, idx in sorted(maps.items()):
            im = Image.open(io.BytesIO(image_bytes(js, binary, idx)))
            im = im.convert("RGB")
            # These maps are 2048 or 4096 square; half size is plenty at the
            # sizes a car is drawn on screen, and it keeps startup quick.
            if max(im.size) > 1024:
                im = im.resize((im.width // 2, im.height // 2), Image.LANCZOS)
            out = os.path.join(MODELS, f"{stem}_{slot}.png")
            im.save(out, optimize=True)
            print(f"{f}: {slot:<10} -> {os.path.basename(out)} "
                  f"({im.width}x{im.height})")
            count += 1
    print(f"{count} texture(s) written")
    print("NOTE: the world shader samples the albedo only. The other maps are "
          "written so nothing is left inside the GLB, but normal/metallic/"
          "emissive will not show until the shader is taught to read them.")


if __name__ == "__main__":
    main()
