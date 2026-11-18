import bpy, bmesh, os, sys, json, struct, math
from mathutils import Vector

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

import importlib
mpfb = sys.modules.get("bl_ext.user_default.mpfb")
from bl_ext.user_default.mpfb.services.humanservice import HumanService
from bl_ext.user_default.mpfb.services.assetservice import AssetService
from bl_ext.user_default.mpfb.services.targetservice import TargetService

BONES = ["pelvis", "spine", "chest", "neck", "head",
         "upperarm_l", "lowerarm_l", "hand_l", "fingers_l",
         "upperarm_r", "lowerarm_r", "hand_r", "fingers_r",
         "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r"]
PARENT = [-1, 0, 1, 2, 3, 2, 5, 6, 7, 2, 9, 10, 11, 0, 13, 14, 0, 16, 17]
MAPS = {
    "Root": "pelvis", "pelvis": "pelvis", "spine_01": "spine", "spine_02": "spine", "spine_03": "chest",
    "clavicle_l": "chest", "clavicle_r": "chest", "neck_01": "neck", "head": "head",
    "upperarm_l": "upperarm_l", "lowerarm_l": "lowerarm_l", "hand_l": "hand_l",
    "upperarm_r": "upperarm_r", "lowerarm_r": "lowerarm_r", "hand_r": "hand_r",
    "thigh_l": "thigh_l", "calf_l": "calf_l", "foot_l": "foot_l", "ball_l": "foot_l",
    "thigh_r": "thigh_r", "calf_r": "calf_r", "foot_r": "foot_r", "ball_r": "foot_r",
    "breast_l": "chest", "breast_r": "chest",
}
for side in "lr":
    for f in ["index", "middle", "ring", "pinky"]:
        for k in "123":
            MAPS["%s_0%s_%s" % (f, k, side)] = "fingers_" + side
    for k in "123":
        MAPS["thumb_0%s_%s" % (k, side)] = "hand_" + side
JOINT_FROM = {"pelvis": "pelvis", "spine": "spine_01", "chest": "spine_03", "neck": "neck_01", "head": "head",
              "upperarm_l": "upperarm_l", "lowerarm_l": "lowerarm_l", "hand_l": "hand_l", "fingers_l": "middle_01_l",
              "upperarm_r": "upperarm_r", "lowerarm_r": "lowerarm_r", "hand_r": "hand_r", "fingers_r": "middle_01_r",
              "thigh_l": "thigh_l", "calf_l": "calf_l", "foot_l": "foot_l", "thigh_r": "thigh_r", "calf_r": "calf_r", "foot_r": "foot_r"}

U = 1 / 2.8


def to_game(v):
    return (-v.y * U, v.x * U, v.z * U)


def find(sub, name):
    p = AssetService.find_asset_absolute_path(name, asset_subdir=sub)
    if not p:
        raise RuntimeError("missing asset %s/%s" % (sub, name))
    return p


def base_image(mat):
    if mat is None or not mat.use_nodes:
        return None
    best = None
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image is not None:
            nm = (n.name + " " + n.label + " " + n.image.name).lower()
            if any(k in nm for k in ("normal", "bump", "rough", "spec", "sss", "displace", "ao", "metal")):
                continue
            if best is None or "diffuse" in nm or "color" in nm or "albedo" in nm:
                best = n.image
    return best


def build(spec):
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    info = HumanService._create_default_human_info_dict()
    ph = info["phenotype"]
    for k, v in spec["phenotype"].items():
        if k == "race":
            ph["race"].update(v)
        else:
            ph[k] = v
    info["rig"] = "game_engine"
    info["proxy"] = spec["proxy"]
    info["skin_mhmat"] = spec["skin"]
    info["skin_material_type"] = "GAMEENGINE"
    info["eyes"] = spec.get("eyes", "brown")
    info["eyebrows"] = spec.get("eyebrows", "eyebrow001")
    info["hair"] = spec.get("hair", "")
    info["clothes"] = spec["clothes"]
    settings = HumanService.get_default_deserialization_settings()
    settings["subdiv_levels"] = 0
    settings["override_clothes_model"] = "MAKESKIN"
    settings["override_eyes_model"] = "MAKESKIN"
    basemesh = HumanService.deserialize_from_dict(info, settings)
    return basemesh


def collect(basemesh):
    rig = basemesh.parent
    parts = []
    for o in bpy.data.objects:
        if o.type != 'MESH' or o == basemesh:
            continue
        n = o.name.lower()
        if "eyelash" in n or "tongue" in n or "teeth" in n:
            continue
        parts.append(o)
    return rig, parts


TARGET = {"male1591": 1450, "female1605": 1450, "casualsuit": 760, "sportsuit": 760, "elegantsuit": 800, "worksuit": 760, "shoes": 90, "short": 380, "ponytail": 420, "bob": 420, "long": 480, "afro": 420, "braid": 420, "eyebrow": 40, "low-poly": 100}


def part_target(o):
    n = o.name.lower()
    for k, v in TARGET.items():
        if k in n:
            return v
    return 600


def export_parts(parts, rest, images, img_index, scale):
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()
    cover = []
    for o in parts:
        n = o.name.lower()
        if "suit" in n or "shoes" in n:
            eo = o.evaluated_get(dg)
            me = eo.to_mesh()
            vs = [o.matrix_world @ v.co for v in me.vertices]
            ps = [list(p.vertices) for p in me.polygons]
            cover.append(BVHTree.FromPolygons(vs, ps))
            eo.to_mesh_clear()
    verts, tris = [], []
    for o in parts:
        arm_mods = [m for m in o.modifiers if m.type == 'ARMATURE']
        for m in arm_mods:
            m.show_viewport = False
        ntri = sum(len(p.vertices) - 2 for p in o.data.polygons)
        tgt = part_target(o) * scale
        dec = None
        if ntri > tgt:
            dec = o.modifiers.new("dec", 'DECIMATE')
            dec.ratio = max(0.02, tgt / ntri)
            dec.use_collapse_triangulate = True
        dg = bpy.context.evaluated_depsgraph_get()
        eo = o.evaluated_get(dg)
        me = eo.to_mesh()
        me.calc_loop_triangles()
        uvl = me.uv_layers.active
        is_body = "1591" in o.name or "1605" in o.name
        covered = None
        if is_body and cover:
            covered = []
            for v in me.vertices:
                w = o.matrix_world @ v.co
                c = False
                for t in cover:
                    hit = t.find_nearest(w, 0.03)
                    if hit[0] is not None:
                        c = True
                        break
                covered.append(c)
        vmap = {}
        for lt in me.loop_triangles:
            if covered is not None and all(covered[me.loops[li].vertex_index] for li in lt.loops):
                continue
            mat = o.material_slots[lt.material_index].material if o.material_slots else None
            img = base_image(mat)
            key = img.name if img else "flat:" + (mat.name if mat else o.name)
            if key not in img_index:
                img_index[key] = len(images)
                images.append((key, img, tuple(mat.diffuse_color) if mat else (0.5, 0.5, 0.5, 1)))
            ii = img_index[key]
            tri = []
            for li in lt.loops:
                vi_ = me.loops[li].vertex_index
                uv = tuple(uvl.data[li].uv) if uvl else (0.5, 0.5)
                k = (vi_, round(uv[0], 4), round(uv[1], 4), ii)
                if k not in vmap:
                    v = me.vertices[vi_]
                    acc = {}
                    for g in v.groups:
                        gname = o.vertex_groups[g.group].name if g.group < len(o.vertex_groups) else None
                        if gname in MAPS and g.weight > 0:
                            bn = BONES.index(MAPS[gname])
                            acc[bn] = acc.get(bn, 0) + g.weight
                    if not acc:
                        acc = {BONES.index("pelvis"): 1.0}
                    top = sorted(acc.items(), key=lambda t: -t[1])[:2]
                    s_ = sum(w for _, w in top)
                    top = [(b, w / s_) for b, w in top]
                    if len(top) == 1:
                        top.append((top[0][0], 0.0))
                    vmap[k] = len(verts)
                    verts.append((to_game(o.matrix_world @ v.co), uv, ii, top))
                tri.append(vmap[k])
            tris.append(tri)
        eo.to_mesh_clear()
        if dec is not None:
            o.modifiers.remove(dec)
        for m in arm_mods:
            m.show_viewport = True
    return verts, tris


def main(specs, only=None):
    manifest = []
    for vi, spec in enumerate(specs):
        if only is not None and spec["name"] != only:
            continue
        basemesh = build(spec)
        rig, parts = collect(basemesh)
        dg = bpy.context.evaluated_depsgraph_get()
        rest = {}
        for b in rig.data.bones:
            rest[b.name] = rig.matrix_world @ b.head_local
        images, img_index = [], {}
        lods = [export_parts(parts, rest, images, img_index, 1.0), export_parts(parts, rest, images, img_index, 0.28), export_parts(parts, rest, images, img_index, 0.085)]
        joints = []
        for bn in BONES:
            joints.append(to_game(rest[JOINT_FROM[bn]]))
        imgs_out = []
        for key, img, col in images:
            path = bpy.path.abspath(img.filepath) if img is not None else None
            imgs_out.append({"key": key, "file": path, "color": list(col)})
        data = {"name": spec["name"], "lods": [{"verts": [[list(p), list(uv), ii, [list(t) for t in w]] for p, uv, ii, w in verts], "tris": tris} for verts, tris in lods],
                "images": imgs_out, "joints": joints, "bones": BONES, "parent": PARENT,
                "parts": [o.name for o in parts]}
        json.dump(data, open(os.path.join(OUT, spec["name"] + ".json"), "w"))
        manifest.append({"name": spec["name"], "lods": [[len(v), len(t)] for v, t in lods], "images": len(imgs_out), "parts": data["parts"]})
        print("BAKED", spec["name"], [[len(v), len(t)] for v, t in lods], [i["key"] for i in imgs_out])
    json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w"), indent=1)


SPECS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "specs.json")))
only = None
if "--" in sys.argv:
    a = sys.argv[sys.argv.index("--") + 1:]
    if a:
        only = a[0]
main(SPECS, only)
