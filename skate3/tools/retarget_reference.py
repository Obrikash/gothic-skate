"""Reference retarget: the worker's skater pose onto Gothic's human skeleton.

The worker publishes, per tick, a world rotation (rows) and position for each
of 36 rig bones (skate-runtime retarget.rs world_pose), Z-up meters. Gothic's
human skeleton (HUMANS.MDH) is a 3ds Max Biped: Y-up centimeters, bones along
their local +X, facing +Z, arms hanging in the rest pose.

Per Gothic bone:
  1. Reference (T-pose) rotation: the Gothic bind rotation, swung so the bone
     points along the rig's T-pose direction for the same joint.
  2. Posed rotation: the rig bone's rotation away from its own T-pose (twist
     included), applied to that reference.
  3. Aim: swing the bone onto the rig's actual joint-to-child direction.
  4. Legs: two-bone IK with Gothic's lengths onto the rig's foot targets, so
     Marvin's longer legs still put his soles on the deck.
Results are model-space globals (rotation, position in cm) for Marvin's vob.
This file is the oracle for the bridge's C port.
"""
import json
import math
import struct
from pathlib import Path

# Gothic bone -> (rig joint, rig child used to aim it or None)
MAPPING = {
    "BIP01 PELVIS": (1, 2), "BIP01 SPINE": (2, 9), "BIP01 SPINE1": (9, 10), "BIP01 SPINE2": (10, 11),
    "BIP01 NECK": (11, 12), "BIP01 HEAD": (12, None),
    "BIP01 L CLAVICLE": (19, 20), "BIP01 L UPPERARM": (20, 21), "BIP01 L FOREARM": (21, 22), "BIP01 L HAND": (22, None),
    "BIP01 R CLAVICLE": (27, 28), "BIP01 R UPPERARM": (28, 29), "BIP01 R FOREARM": (29, 30), "BIP01 R HAND": (30, None),
    "BIP01 L THIGH": (3, 4), "BIP01 L CALF": (4, 5), "BIP01 L FOOT": (5, None),
    "BIP01 R THIGH": (6, 7), "BIP01 R CALF": (7, 8), "BIP01 R FOOT": (8, None),
}
LEGS = [("BIP01 L THIGH", "BIP01 L CALF", "BIP01 L FOOT", 5, 4), ("BIP01 R THIGH", "BIP01 R CALF", "BIP01 R FOOT", 8, 7)]
# Gothic's spine segments (2.4, 9.8, 12.8, 30 cm) are distributed unlike the
# rig's (20, 20, 20, 10 cm). Copying segment directions one to one puts the
# rig's short, strongly bent upper segment on Gothic's long chest bone and
# tips the whole torso. Instead each Gothic spine joint goes to the point at
# the same fraction along the rig's pelvis-to-neck curve.
SPINE_CHAIN = ["BIP01 PELVIS", "BIP01 SPINE", "BIP01 SPINE1", "BIP01 SPINE2", "BIP01 NECK"]
RIG_SPINE = [1, 2, 9, 10, 11]
TICK_FLOATS = 14 + 36 * 13 + 2 * 13


# ---- small 3x3 / vector helpers (row-major 3x3 as lists of rows) ----
def mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def mat_vec(a, v):
    return [sum(a[r][k] * v[k] for k in range(3)) for r in range(3)]


def transpose(a):
    return [[a[c][r] for c in range(3)] for r in range(3)]


def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def add(a, b):
    return [a[i] + b[i] for i in range(3)]


def scale(a, s):
    return [x * s for x in a]


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def norm(a):
    length = math.sqrt(dot(a, a))
    return [x / length for x in a] if length > 1e-9 else [0.0, 0.0, 0.0]


def column(m, c):
    return [m[0][c], m[1][c], m[2][c]]


def arc(a, b):
    """Shortest rotation taking unit vector a onto unit vector b."""
    v, c = cross(a, b), dot(a, b)
    if c < -0.999999:  # opposite: rotate 180 degrees about any perpendicular axis
        axis = norm(cross(a, [1, 0, 0] if abs(a[0]) < 0.9 else [0, 1, 0]))
        return [[2 * axis[r] * axis[k] - (1 if r == k else 0) for k in range(3)] for r in range(3)]
    k = 1 / (1 + c)
    skew = [[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]]
    skew2 = mat_mul(skew, skew)
    return [[(1 if r == q else 0) + skew[r][q] + skew2[r][q] * k for q in range(3)] for r in range(3)]


# Rig (wire: x, y, z-up) -> Gothic (x, y-up, z): swap y and z.
P = [[1, 0, 0], [0, 0, 1], [0, 1, 0]]
# The rig's T-pose faces -Y (wire) = -Z (Gothic); Gothic's model faces +Z.
RY180 = [[-1, 0, 0], [0, 1, 0], [0, 0, -1]]


def to_gothic_rotation(r):
    return mat_mul(mat_mul(P, r), P)


def to_gothic_vector(v):
    return [v[0], v[2], v[1]]


def archive_file(archive, name):
    """One file from a Gothic VDF/MOD volume (read-only)."""
    with open(archive, "rb") as f:
        head = f.read(296)
        count, _, _, _, root, _ = struct.unpack_from("<6I", head, 272)
        f.seek(root)
        for _ in range(count):
            entry, offset, length, kind, _ = struct.unpack("<64s4I", f.read(80))
            if not kind & 0x80000000 and entry.rstrip(b" \0").decode("latin1").upper() == name.upper():
                f.seek(offset)
                return f.read(length)
    raise FileNotFoundError(f"{name} not in {archive}")


def parse_mdh(data):
    """Nodes of a compiled model hierarchy (.MDH chunk 0xD100): name, parent, local zMAT4."""
    chunk, _ = struct.unpack_from("<HI", data, 0)
    if chunk != 0xD100:
        raise ValueError("Not a model hierarchy")
    position = 6
    _, count = struct.unpack_from("<IH", data, position)
    position += 6
    nodes = []
    for _ in range(count):
        end = data.index(b"\n", position)
        name = data[position:end].decode("latin1")
        parent = struct.unpack_from("<H", data, end + 1)[0]
        matrix = struct.unpack_from("<16f", data, end + 3)
        position = end + 3 + 64
        nodes.append({"name": name, "parent": None if parent == 0xFFFF else parent, "matrix": matrix})
    return nodes


class GothicSkeleton:
    def __init__(self, source):
        """source: a list of parsed .MDH nodes, or a JSON file holding {"nodes": [...]}."""
        self.nodes = source if isinstance(source, list) else json.loads(Path(source).read_text())["nodes"]
        self.names = [n["name"] for n in self.nodes]
        self.parent = [n["parent"] for n in self.nodes]
        self.local_rot, self.local_pos = [], []
        for n in self.nodes:
            m = n["matrix"]  # zMAT4 row-major, translation in m[3], m[7], m[11]
            self.local_rot.append([[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]])
            self.local_pos.append([m[3], m[7], m[11]])
        self.bind_rot, self.bind_pos = [], []
        for i in range(len(self.nodes)):
            p = self.parent[i]
            if p is None:
                self.bind_rot.append(self.local_rot[i]); self.bind_pos.append(self.local_pos[i])
            else:
                self.bind_rot.append(mat_mul(self.bind_rot[p], self.local_rot[i]))
                self.bind_pos.append(add(self.bind_pos[p], mat_vec(self.bind_rot[p], self.local_pos[i])))
        self.index = {name: i for i, name in enumerate(self.names)}


def rig_bind(path):
    bones = json.loads(Path(path).read_text())
    rotations = [transpose([b["matrix"][0], b["matrix"][1], b["matrix"][2]]) for b in bones]  # columns -> rows
    positions = [b["matrix"][3] for b in bones]
    return rotations, positions


def reference_rotations(skeleton, rig_positions):
    """Gothic T-pose reference: each mapped bone swung onto the rig's T-pose direction."""
    reference = {}
    for name, (joint, child) in MAPPING.items():
        i = skeleton.index[name]
        bind = skeleton.bind_rot[i]
        if child is None:
            reference[name] = bind
            continue
        direction = mat_vec(RY180, to_gothic_vector(norm(sub(rig_positions[child], rig_positions[joint]))))
        reference[name] = mat_mul(arc(norm(column(bind, 0)), direction), bind)
    return reference


def read_ticks(path):
    data = Path(path).read_bytes()
    count = len(data) // (TICK_FLOATS * 4)
    for t in range(count):
        values = struct.unpack_from(f"<{TICK_FLOATS}f", data, t * TICK_FLOATS * 4)
        output = values[:14]
        pose = [values[14 + b * 13:14 + (b + 1) * 13] for b in range(36)]
        board = [values[14 + 468 + k * 13:14 + 468 + (k + 1) * 13] for k in range(2)]
        yield output, pose, board


def marvin_rotation(yaw):
    """The bridge's Marvin trafo rotation (apply_output): columns X=(c,0,s), Y=up, Z=(-s,0,c)."""
    c, s = math.cos(yaw), math.sin(yaw)
    return [[c, 0, -s], [0, 1, 0], [s, 0, c]]


def retarget(skeleton, reference, rig_bind_rot, pose, marvin_rot, marvin_pos):
    """Model-space Gothic globals {name: (rotation, position cm)} for one tick."""
    inv_marvin = transpose(marvin_rot)

    def model_point(p):  # rig world meters -> Gothic model cm
        world = scale(to_gothic_vector(p), 100)
        return mat_vec(inv_marvin, sub(world, marvin_pos))

    def rig_rot(b):
        p = pose[b]
        return [[p[0], p[1], p[2]], [p[3], p[4], p[5]], [p[6], p[7], p[8]]]

    def rig_pos(b):
        return [pose[b][9], pose[b][10], pose[b][11]]

    # Rig spine curve in model space, with cumulative lengths for the chain fit.
    curve = [model_point(rig_pos(b)) for b in RIG_SPINE]
    curve_length = [0.0]
    for a, b in zip(curve, curve[1:]):
        curve_length.append(curve_length[-1] + math.sqrt(dot(sub(b, a), sub(b, a))))

    def on_curve(fraction):
        target = fraction * curve_length[-1]
        for k in range(1, len(curve)):
            if target <= curve_length[k] or k == len(curve) - 1:
                span = curve_length[k] - curve_length[k - 1]
                t = (target - curve_length[k - 1]) / span if span > 1e-6 else 0
                return add(curve[k - 1], scale(sub(curve[k], curve[k - 1]), t))

    chain = [skeleton.index[name] for name in SPINE_CHAIN]
    gothic_length = [0.0]
    for i in chain[1:]:
        gothic_length.append(gothic_length[-1] + math.sqrt(dot(skeleton.local_pos[i], skeleton.local_pos[i])))
    spine_target = {SPINE_CHAIN[k]: on_curve(gothic_length[k + 1] / gothic_length[-1]) for k in range(len(chain) - 1)}

    # Compose globals top-down; mapped bones get a delta rotation aimed at the
    # rig, everything else follows the bind.
    globals_rot, globals_pos = [None] * len(skeleton.names), [None] * len(skeleton.names)
    pelvis = skeleton.index["BIP01 PELVIS"]
    for i, name in enumerate(skeleton.names):
        p = skeleton.parent[i]
        if p is None:
            globals_rot[i] = skeleton.bind_rot[i]
            # Place the root so the pelvis lands on the rig pelvis.
            globals_pos[i] = sub(model_point(rig_pos(1)), mat_vec(globals_rot[i], skeleton.local_pos[pelvis]))
            continue
        globals_pos[i] = add(globals_pos[p], mat_vec(globals_rot[p], skeleton.local_pos[i]))
        if name not in MAPPING:
            globals_rot[i] = mat_mul(globals_rot[p], skeleton.local_rot[i])
            continue
        joint, child = MAPPING[name]
        delta = to_gothic_rotation(mat_mul(rig_rot(joint), transpose(rig_bind_rot[joint])))
        candidate = mat_mul(inv_marvin, mat_mul(delta, mat_mul(RY180, reference[name])))
        if name in spine_target:
            want = norm(sub(spine_target[name], globals_pos[i]))
        elif child is not None:
            want = norm(mat_vec(inv_marvin, to_gothic_vector(sub(rig_pos(child), rig_pos(joint)))))
        else:
            want = None
        if want is not None and dot(want, want) > 0.5:
            candidate = mat_mul(arc(norm(column(candidate, 0)), want), candidate)
        globals_rot[i] = candidate

    # Two-bone leg IK onto the rig's foot targets with Gothic's segment lengths.
    for thigh, calf, foot, foot_joint, knee_joint in LEGS:
        a, b, c = skeleton.index[thigh], skeleton.index[calf], skeleton.index[foot]
        l1 = math.sqrt(dot(skeleton.local_pos[b], skeleton.local_pos[b]))
        l2 = math.sqrt(dot(skeleton.local_pos[c], skeleton.local_pos[c]))
        hip, target = globals_pos[a], model_point(rig_pos(foot_joint))
        delta = sub(target, hip)
        distance = math.sqrt(dot(delta, delta))
        if distance < 1e-3:
            continue
        axis = scale(delta, 1 / distance)
        d = min(max(distance, abs(l1 - l2) + 1e-3), l1 + l2 - 1e-3)
        projection = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
        height = math.sqrt(max(l1 * l1 - projection * projection, 0))
        hint = sub(model_point(rig_pos(knee_joint)), hip)
        bend = norm(sub(hint, scale(axis, dot(hint, axis))))
        if dot(bend, bend) < 0.5:
            bend = norm(cross(axis, [1, 0, 0]))
        knee = add(add(hip, scale(axis, projection)), scale(bend, height))
        ankle = add(hip, scale(axis, d))
        for bone, start, end in [(a, hip, knee), (b, knee, ankle)]:
            globals_rot[bone] = mat_mul(arc(norm(column(globals_rot[bone], 0)), norm(sub(end, start))), globals_rot[bone])
        globals_pos[b], globals_pos[c] = knee, ankle
        for i, name in enumerate(skeleton.names):  # toes follow their foot
            if skeleton.parent[i] == c:
                globals_pos[i] = add(globals_pos[c], mat_vec(globals_rot[c], skeleton.local_pos[i]))
    # Re-derive everything below the legs/arms that follows a mapped bone (fingers, slots).
    for i, name in enumerate(skeleton.names):
        p = skeleton.parent[i]
        if p is not None and name not in MAPPING and skeleton.parent[i] is not None:
            globals_rot[i] = mat_mul(globals_rot[p], skeleton.local_rot[i])
            globals_pos[i] = add(globals_pos[p], mat_vec(globals_rot[p], skeleton.local_pos[i]))
    return globals_rot, globals_pos


def locals_from_globals(skeleton, globals_rot, globals_pos):
    """Node-local trafos (rotation, translation) as Gothic stores them per node."""
    result = []
    for i in range(len(skeleton.names)):
        p = skeleton.parent[i]
        if p is None:
            result.append((globals_rot[i], globals_pos[i]))
        else:
            inv = transpose(globals_rot[p])
            result.append((mat_mul(inv, globals_rot[i]), mat_vec(inv, sub(globals_pos[i], globals_pos[p]))))
    return result
