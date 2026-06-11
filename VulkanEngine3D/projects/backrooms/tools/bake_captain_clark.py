#!/usr/bin/env python3
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "projects/backrooms/assets/models/captain_clark_rigged_fixed.glb"
OUT = ROOT / "projects/backrooms/assets/models/captain_clark_baked"
TEX = "Material.001_baseColor.png"

COMP_SIZE = {5123: 2, 5125: 4, 5126: 4}
COMP_FMT = {5123: "H", 5125: "I", 5126: "f"}
TYPE_COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def align4(data, pad=b"\0"):
    return data + pad * ((4 - len(data) % 4) % 4)


def read_glb(path):
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2:
        raise RuntimeError("Expected GLB v2")
    off = 12
    chunks = {}
    while off < length:
        chunk_len, chunk_type = struct.unpack_from("<II", data, off)
        off += 8
        chunks[chunk_type] = data[off:off + chunk_len]
        off += chunk_len
    doc = json.loads(chunks[0x4E4F534A].decode("utf-8"))
    return doc, chunks[0x004E4942]


def read_accessor(doc, blob, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    comp = acc["componentType"]
    count = TYPE_COUNT[acc["type"]]
    stride = acc.get("byteStride") or view.get("byteStride") or COMP_SIZE[comp] * count
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    fmt = "<" + COMP_FMT[comp] * count
    out = []
    for i in range(acc["count"]):
        out.append(struct.unpack_from(fmt, blob, base + i * stride))
    return out


def mat_identity():
    return [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def mat_vec(m, v):
    return tuple(sum(m[r][c] * v[c] for c in range(4)) for r in range(4))


def mat_from_gltf(vals):
    return [[vals[c * 4 + r] for c in range(4)] for r in range(4)]


def tmat(t):
    m = mat_identity()
    m[0][3], m[1][3], m[2][3] = t
    return m


def smat(s):
    m = mat_identity()
    m[0][0], m[1][1], m[2][2] = s
    return m


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def qaxis(axis, angle):
    x, y, z = axis
    l = math.sqrt(x * x + y * y + z * z) or 1.0
    s = math.sin(angle * 0.5) / l
    return (x * s, y * s, z * s, math.cos(angle * 0.5))


def qmat(q):
    x, y, z, w = q
    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z
    return [
        [1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy), 0],
        [2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx), 0],
        [2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy), 0],
        [0, 0, 0, 1],
    ]


def local_matrix(node, deltas):
    if "matrix" in node and not node.get("name") in deltas:
        return mat_from_gltf(node["matrix"])
    t = node.get("translation", [0.0, 0.0, 0.0])
    r = tuple(node.get("rotation", [0.0, 0.0, 0.0, 1.0]))
    s = node.get("scale", [1.0, 1.0, 1.0])
    if node.get("name") in deltas:
        r = qmul(r, deltas[node["name"]])
    return mat_mul(mat_mul(tmat(t), qmat(r)), smat(s))


def world_mats(doc, deltas):
    nodes = doc["nodes"]
    parents = {i: None for i in range(len(nodes))}
    for i, n in enumerate(nodes):
        for ch in n.get("children", []):
            parents[ch] = i
    locals_ = [local_matrix(n, deltas) for n in nodes]
    cache = {}
    def world(i):
        if i in cache:
            return cache[i]
        p = parents[i]
        cache[i] = locals_[i] if p is None else mat_mul(world(p), locals_[i])
        return cache[i]
    return [world(i) for i in range(len(nodes))]


def find_node(doc, prefix):
    for i, n in enumerate(doc["nodes"]):
        if n.get("name", "").startswith(prefix):
            return n.get("name")
    raise KeyError(prefix)


def pose_deltas(doc, clip, frame, total):
    phase = frame / total * math.tau
    s = math.sin(phase)
    c = math.cos(phase)
    names = {key: find_node(doc, key) for key in [
        "mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine2", "mixamorig:Head",
        "mixamorig:LeftArm", "mixamorig:RightArm", "mixamorig:LeftForeArm", "mixamorig:RightForeArm",
        "mixamorig:LeftUpLeg", "mixamorig:RightUpLeg", "mixamorig:LeftLeg", "mixamorig:RightLeg",
        "mixamorig:LeftFoot", "mixamorig:RightFoot",
    ]}
    d = {}
    if clip == "walk":
        d[names["mixamorig:Spine"]] = qaxis((0, 0, 1), s * 0.08)
        d[names["mixamorig:LeftUpLeg"]] = qaxis((1, 0, 0), s * 0.55)
        d[names["mixamorig:RightUpLeg"]] = qaxis((1, 0, 0), -s * 0.55)
        d[names["mixamorig:LeftLeg"]] = qaxis((1, 0, 0), max(0.0, -s) * 0.65)
        d[names["mixamorig:RightLeg"]] = qaxis((1, 0, 0), max(0.0, s) * 0.65)
        d[names["mixamorig:LeftArm"]] = qaxis((1, 0, 0), -s * 0.45)
        d[names["mixamorig:RightArm"]] = qaxis((1, 0, 0), s * 0.45)
        d[names["mixamorig:LeftForeArm"]] = qaxis((1, 0, 0), 0.25 + max(0.0, s) * 0.25)
        d[names["mixamorig:RightForeArm"]] = qaxis((1, 0, 0), 0.25 + max(0.0, -s) * 0.25)
    elif clip == "run":
        d[names["mixamorig:Spine"]] = qaxis((1, 0, 0), -0.22)
        d[names["mixamorig:Spine2"]] = qaxis((0, 0, 1), s * 0.16)
        d[names["mixamorig:LeftUpLeg"]] = qaxis((1, 0, 0), s * 0.95)
        d[names["mixamorig:RightUpLeg"]] = qaxis((1, 0, 0), -s * 0.95)
        d[names["mixamorig:LeftLeg"]] = qaxis((1, 0, 0), max(0.0, -s) * 1.05)
        d[names["mixamorig:RightLeg"]] = qaxis((1, 0, 0), max(0.0, s) * 1.05)
        d[names["mixamorig:LeftArm"]] = qaxis((1, 0, 0), -s * 0.85)
        d[names["mixamorig:RightArm"]] = qaxis((1, 0, 0), s * 0.85)
        d[names["mixamorig:LeftForeArm"]] = qaxis((1, 0, 0), 0.55 + max(0.0, s) * 0.45)
        d[names["mixamorig:RightForeArm"]] = qaxis((1, 0, 0), 0.55 + max(0.0, -s) * 0.45)
    else:
        p = frame / max(1, total - 1)
        reach = math.sin(p * math.pi)
        d[names["mixamorig:Spine"]] = qaxis((1, 0, 0), -0.45 * reach)
        d[names["mixamorig:Spine2"]] = qaxis((1, 0, 0), -0.25 * reach)
        d[names["mixamorig:Head"]] = qaxis((1, 0, 0), 0.18 * reach)
        d[names["mixamorig:LeftArm"]] = qmul(qaxis((1, 0, 0), -1.25 * reach), qaxis((0, 0, 1), -0.35 * reach))
        d[names["mixamorig:RightArm"]] = qmul(qaxis((1, 0, 0), -1.25 * reach), qaxis((0, 0, 1), 0.35 * reach))
        d[names["mixamorig:LeftForeArm"]] = qaxis((1, 0, 0), -0.75 * reach)
        d[names["mixamorig:RightForeArm"]] = qaxis((1, 0, 0), -0.75 * reach)
        d[names["mixamorig:LeftUpLeg"]] = qaxis((1, 0, 0), 0.22 * reach)
        d[names["mixamorig:RightUpLeg"]] = qaxis((1, 0, 0), -0.22 * reach)
    return d


def normalize(v):
    l = math.sqrt(sum(x * x for x in v))
    return tuple(x / l for x in v) if l > 1e-8 else (0.0, 1.0, 0.0)


def skin_frame(doc, blob, clip, frame, total):
    mesh = doc["meshes"][0]["primitives"][0]
    attrs = mesh["attributes"]
    positions = read_accessor(doc, blob, attrs["POSITION"])
    normals = read_accessor(doc, blob, attrs["NORMAL"])
    uvs = read_accessor(doc, blob, attrs["TEXCOORD_0"])
    joints = read_accessor(doc, blob, attrs["JOINTS_0"])
    weights = read_accessor(doc, blob, attrs["WEIGHTS_0"])
    invbind = [mat_from_gltf(m) for m in read_accessor(doc, blob, doc["skins"][0]["inverseBindMatrices"])]
    joint_nodes = doc["skins"][0]["joints"]
    worlds = world_mats(doc, pose_deltas(doc, clip, frame, total))
    joint_mats = [mat_mul(worlds[node_index], invbind[i]) for i, node_index in enumerate(joint_nodes)]
    out_pos = []
    out_nrm = []
    for p, n, js, ws in zip(positions, normals, joints, weights):
        sx = sy = sz = sw = 0.0
        nx = ny = nz = 0.0
        total_w = sum(ws) or 1.0
        for ji, w in zip(js, ws):
            w = w / total_w
            m = joint_mats[int(ji)]
            tp = mat_vec(m, (p[0], p[1], p[2], 1.0))
            tn = mat_vec(m, (n[0], n[1], n[2], 0.0))
            sx += tp[0] * w; sy += tp[1] * w; sz += tp[2] * w; sw += tp[3] * w
            nx += tn[0] * w; ny += tn[1] * w; nz += tn[2] * w
        if abs(sw) > 1e-8:
            sx, sy, sz = sx / sw, sy / sw, sz / sw
        out_pos.append((sx, sy, sz))
        out_nrm.append(normalize((nx, ny, nz)))
    indices = [i[0] for i in read_accessor(doc, blob, mesh["indices"])]
    return out_pos, out_nrm, uvs, indices


def pack_floats(rows):
    return b"".join(struct.pack("<" + "f" * len(r), *r) for r in rows)


def pack_indices(indices):
    return b"".join(struct.pack("<I", int(i)) for i in indices)


def minmax(rows, comps):
    return ([min(r[i] for r in rows) for i in range(comps)],
            [max(r[i] for r in rows) for i in range(comps)])


def write_frame(path, positions, normals, uvs, indices):
    chunks = []
    def add(data, target=None, stride=None, name=None):
        offset = sum(len(c) for c in chunks)
        chunks.append(align4(data))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            view["target"] = target
        if stride:
            view["byteStride"] = stride
        if name:
            view["name"] = name
        return view

    views = [
        add(pack_floats(positions), 34962, 12, "POSITION"),
        add(pack_floats(normals), 34962, 12, "NORMAL"),
        add(pack_floats(uvs), 34962, 8, "TEXCOORD_0"),
        add(pack_indices(indices), 34963, None, "indices"),
    ]
    bin_blob = b"".join(chunks)
    pmin, pmax = minmax(positions, 3)
    nmin, nmax = minmax(normals, 3)
    uvmin, uvmax = minmax(uvs, 2)
    gltf = {
        "asset": {"version": "2.0", "generator": "VKE Captain Clark baked frame"},
        "buffers": [{"byteLength": len(bin_blob)}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3", "min": pmin, "max": pmax},
            {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3", "min": nmin, "max": nmax},
            {"bufferView": 2, "componentType": 5126, "count": len(uvs), "type": "VEC2", "min": uvmin, "max": uvmax},
            {"bufferView": 3, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
        "images": [{"uri": TEX, "mimeType": "image/png"}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "textures": [{"sampler": 0, "source": 0}],
        "materials": [{"doubleSided": True, "name": "Material.001", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.5}}],
        "meshes": [{"name": "CaptainClarkBaked", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0, "mode": 4}]}],
        "nodes": [{"mesh": 0, "name": path.stem}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    json_blob = align4(json.dumps(gltf, separators=(",", ":")).encode("utf-8"), b" ")
    bin_blob = align4(bin_blob)
    total_len = 12 + 8 + len(json_blob) + 8 + len(bin_blob)
    out = struct.pack("<III", 0x46546C67, 2, total_len)
    out += struct.pack("<II", len(json_blob), 0x4E4F534A) + json_blob
    out += struct.pack("<II", len(bin_blob), 0x004E4942) + bin_blob
    path.write_bytes(out)


def extract_texture(doc, blob):
    view = doc["bufferViews"][doc["images"][0]["bufferView"]]
    start = view.get("byteOffset", 0)
    return blob[start:start + view["byteLength"]]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    doc, blob = read_glb(SRC)
    (OUT / TEX).write_bytes(extract_texture(doc, blob))
    clips = {"walk": 8, "run": 8, "catch": 6}
    for clip, count in clips.items():
        for frame in range(count):
            positions, normals, uvs, indices = skin_frame(doc, blob, clip, frame, count)
            write_frame(OUT / f"{clip}_{frame:02d}.glb", positions, normals, uvs, indices)
        print(f"baked {clip}: {count} frames")
    print(f"output: {OUT}")


if __name__ == "__main__":
    main()
