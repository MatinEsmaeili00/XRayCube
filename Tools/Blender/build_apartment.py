"""
Builds the XRayCube demo apartment procedurally and (optionally) exports it to FBX.

Run inside Blender (Scripting tab -> Open -> Run Script), or headless:

    blender --background --python build_apartment.py -- --export /path/to/Apartment.fbx

Everything is made of axis-aligned boxes and a few cylinders, so the geometry is
clean, watertight and cheap - exactly what you want for a cut-away test scene:
every wall has real thickness, so when the X-ray cube slices through it you can
see the wall's inside faces instead of a paper-thin surface.

Units: Blender metres. The default FBX export writes metres, which Unreal
imports as 100 uu per metre, so the apartment arrives at its real size.

Layout (top view, +X east, +Y north, origin = apartment centre at floor level):

    y= 4.5 +-------------------------------+---------------+
           |                               |               |
           |        LIVING ROOM            |    BEDROOM    |
           |                               |               |
    y= 0.5 |                               +----D----+-----+
           |                               |         |     |
    y=-1.0 |- - - - - - - - - - - - - - - -D  HALL   D BATH|
           |        KITCHEN / DINING       |         |     |
    y=-4.5 +-------------------------------+---D-----+-----+
         x=-6                             x=1      x=3.5  x=6
"""

import math
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

# ---------------------------------------------------------------------------
# Dimensions (metres)
# ---------------------------------------------------------------------------

X_MIN, X_MAX = -6.0, 6.0
Y_MIN, Y_MAX = -4.5, 4.5
WALL_H = 2.8           # floor to ceiling
EXT_T = 0.25           # exterior wall thickness
INT_T = 0.12           # interior wall thickness
SLAB_T = 0.15          # floor / ceiling slab thickness
DOOR_H = 2.1
WIN_SILL, WIN_HEAD = 0.9, 2.2

# name -> (base colour RGB, roughness). These names become the FBX material
# slots, which the Unreal side maps onto XRayCutaway material instances.
MATERIALS = {
    "M_WallExterior": ((0.82, 0.80, 0.76), 0.85),
    "M_WallInterior": ((0.93, 0.92, 0.89), 0.90),
    "M_Ceiling":      ((0.96, 0.96, 0.96), 0.95),
    "M_FloorWood":    ((0.55, 0.38, 0.24), 0.55),
    "M_FloorTile":    ((0.72, 0.74, 0.76), 0.35),
    "M_Wood":         ((0.42, 0.28, 0.17), 0.60),
    "M_Fabric":       ((0.25, 0.35, 0.50), 0.95),
    "M_Bedding":      ((0.90, 0.88, 0.84), 0.95),
    "M_Counter":      ((0.20, 0.20, 0.22), 0.30),
    "M_Metal":        ((0.70, 0.71, 0.73), 0.25),
    "M_Ceramic":      ((0.97, 0.97, 0.97), 0.15),
    "M_Rug":          ((0.60, 0.22, 0.18), 1.00),
}


# ---------------------------------------------------------------------------
# Mesh builder: accumulate boxes/cylinders into one bmesh per output object,
# each face tagged with a material slot.
# ---------------------------------------------------------------------------

class MeshBuilder:
    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.slots = []  # material names in slot order

    def _slot(self, material):
        if material not in self.slots:
            self.slots.append(material)
        return self.slots.index(material)

    def box(self, x0, y0, z0, x1, y1, z1, material):
        """Axis-aligned box from min corner to max corner."""
        if x1 - x0 <= 1e-4 or y1 - y0 <= 1e-4 or z1 - z0 <= 1e-4:
            return
        size = Vector((x1 - x0, y1 - y0, z1 - z0))
        centre = Vector(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
        m = Matrix.Translation(centre) @ Matrix.Diagonal(size.to_4d())
        geom = bmesh.ops.create_cube(self.bm, size=1.0, matrix=m)
        idx = self._slot(material)
        for v in geom["verts"]:
            for f in v.link_faces:
                f.material_index = idx

    def cylinder(self, cx, cy, z0, z1, radius, material, segments=24):
        m = Matrix.Translation(Vector((cx, cy, (z0 + z1) / 2)))
        geom = bmesh.ops.create_cone(
            self.bm, cap_ends=True, cap_tris=False, segments=segments,
            radius1=radius, radius2=radius, depth=z1 - z0, matrix=m)
        idx = self._slot(material)
        for v in geom["verts"]:
            for f in v.link_faces:
                f.material_index = idx

    def build(self, collection):
        mesh = bpy.data.meshes.new(self.name)
        self.bm.normal_update()
        self.bm.to_mesh(mesh)
        self.bm.free()
        for slot in self.slots:
            mesh.materials.append(get_material(slot))
        for poly in mesh.polygons:
            poly.use_smooth = False
        obj = bpy.data.objects.new(self.name, mesh)
        collection.objects.link(obj)
        return obj


def get_material(name):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
    colour, rough = MATERIALS[name]
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    mat.diffuse_color = (*colour, 1.0)
    return mat


# ---------------------------------------------------------------------------
# Walls with openings
# ---------------------------------------------------------------------------

def wall(mb, a, b, t, material, openings=(), axis=None):
    """
    Straight axis-aligned wall from point a to point b (2D, metres), thickness t,
    full storey height. `openings` is a list of (start, end, bottom, top)
    measured along the wall from `a`. The wall is split into solid boxes around
    each opening, so no booleans are needed.
    """
    (ax, ay), (bx, by) = a, b
    horizontal = abs(by - ay) < 1e-6
    length = abs(bx - ax) if horizontal else abs(by - ay)
    sign = 1.0 if (bx - ax if horizontal else by - ay) >= 0 else -1.0

    def emit(s0, s1, z0, z1):
        if s1 - s0 <= 1e-4 or z1 - z0 <= 1e-4:
            return
        if horizontal:
            p0, p1 = ax + sign * s0, ax + sign * s1
            mb.box(min(p0, p1), ay - t / 2, z0, max(p0, p1), ay + t / 2, z1, material)
        else:
            p0, p1 = ay + sign * s0, ay + sign * s1
            mb.box(ax - t / 2, min(p0, p1), z0, ax + t / 2, max(p0, p1), z1, material)

    cursor = 0.0
    for (s0, s1, z0, z1) in sorted(openings):
        emit(cursor, s0, 0.0, WALL_H)        # solid run before the opening
        emit(s0, s1, 0.0, z0)                # below (window sill)
        emit(s0, s1, z1, WALL_H)             # above (lintel)
        cursor = s1
    emit(cursor, length, 0.0, WALL_H)


def window(s0, width):
    return (s0, s0 + width, WIN_SILL, WIN_HEAD)


def door(s0, width=0.9):
    return (s0, s0 + width, 0.0, DOOR_H)


# ---------------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------------

def build_apartment():
    # Fresh collection so re-running the script replaces the apartment cleanly.
    old = bpy.data.collections.get("Apartment")
    if old:
        for obj in list(old.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(old)
    coll = bpy.data.collections.new("Apartment")
    bpy.context.scene.collection.children.link(coll)

    walls = MeshBuilder("SM_Apartment_Walls")
    ceiling = MeshBuilder("SM_Apartment_Ceiling")
    floor = MeshBuilder("SM_Apartment_Floor")
    furn = MeshBuilder("SM_Apartment_Furniture")

    # --- Exterior shell (centre-lines inset by half thickness) -------------
    xo0, xo1 = X_MIN + EXT_T / 2, X_MAX - EXT_T / 2
    yo0, yo1 = Y_MIN + EXT_T / 2, Y_MAX - EXT_T / 2
    # Extend horizontal walls by half thickness so the corners are filled.
    e = EXT_T / 2
    wall(walls, (xo0 - e, yo1), (xo1 + e, yo1), EXT_T, "M_WallExterior",          # north
         [window(1.6, 1.8), window(4.4, 1.8), window(8.6, 1.4)])
    wall(walls, (xo0 - e, yo0), (xo1 + e, yo0), EXT_T, "M_WallExterior",          # south
         [window(1.4, 1.6), door(7.8, 1.0), window(10.0, 0.7)])
    wall(walls, (xo0, yo0 + e), (xo0, yo1 - e), EXT_T, "M_WallExterior",          # west
         [window(5.0, 2.0)])
    wall(walls, (xo1, yo0 + e), (xo1, yo1 - e), EXT_T, "M_WallExterior",          # east
         [window(1.6, 0.8), window(5.6, 1.6)])

    # --- Interior partitions -------------------------------------------------
    # x = 1: living/kitchen | hall + bedroom. Door from hall into the kitchen.
    wall(walls, (1.0, Y_MIN + EXT_T), (1.0, Y_MAX - EXT_T), INT_T, "M_WallInterior",
         [door(2.6, 0.9)])
    # y = 0.5: bedroom | hall + bath. Door from hall into the bedroom.
    wall(walls, (1.0 + INT_T / 2, 0.5), (X_MAX - EXT_T, 0.5), INT_T, "M_WallInterior",
         [door(0.9, 0.9)])
    # x = 3.5: hall | bathroom. Door from hall into the bathroom.
    wall(walls, (3.5, Y_MIN + EXT_T), (3.5, 0.5 - INT_T / 2), INT_T, "M_WallInterior",
         [door(2.4, 0.8)])
    # Low kitchen half-wall separating dining from living (breakfast bar).
    walls.box(-3.2, -1.06, 0.0, -1.0, -0.94, 1.05, "M_WallInterior")

    # --- Slabs ---------------------------------------------------------------
    ceiling.box(X_MIN, Y_MIN, WALL_H, X_MAX, Y_MAX, WALL_H + SLAB_T, "M_Ceiling")
    # Wood in the dry rooms, tile in kitchen and bathroom. Slight overlap is
    # avoided by splitting the footprint into non-overlapping rectangles.
    floor.box(X_MIN, -1.0, -SLAB_T, 1.0, Y_MAX, 0.0, "M_FloorWood")    # living
    floor.box(X_MIN, Y_MIN, -SLAB_T, 1.0, -1.0, 0.0, "M_FloorTile")    # kitchen
    floor.box(1.0, 0.5, -SLAB_T, X_MAX, Y_MAX, 0.0, "M_FloorWood")     # bedroom
    floor.box(1.0, Y_MIN, -SLAB_T, 3.5, 0.5, 0.0, "M_FloorWood")       # hall
    floor.box(3.5, Y_MIN, -SLAB_T, X_MAX, 0.5, 0.0, "M_FloorTile")     # bath

    # --- Living room ---------------------------------------------------------
    f = furn
    f.box(-4.6, 1.2, 0.0, -1.4, 3.3, 0.01, "M_Rug")
    # Sofa facing north-west wall TV
    f.box(-4.4, 0.0, 0.0, -1.6, 0.9, 0.42, "M_Fabric")       # seat base
    f.box(-4.4, 0.0, 0.42, -1.6, 0.25, 0.85, "M_Fabric")     # back
    f.box(-4.6, 0.0, 0.0, -4.4, 0.9, 0.62, "M_Fabric")       # arm L
    f.box(-1.6, 0.0, 0.0, -1.4, 0.9, 0.62, "M_Fabric")       # arm R
    # Coffee table
    f.box(-3.6, 1.5, 0.38, -2.4, 2.2, 0.42, "M_Wood")
    for (x, y) in ((-3.55, 1.55), (-2.45, 1.55), (-3.55, 2.15), (-2.45, 2.15)):
        f.box(x - 0.03, y - 0.03, 0.0, x + 0.03, y + 0.03, 0.38, "M_Wood")
    # TV unit + TV against the north wall
    f.box(-4.2, 3.85, 0.0, -1.8, 4.3, 0.5, "M_Wood")
    f.box(-3.7, 4.12, 0.5, -2.3, 4.18, 1.35, "M_Counter")
    # Floor lamp + bookshelf
    f.cylinder(-5.4, 3.8, 0.0, 0.03, 0.18, "M_Metal")
    f.cylinder(-5.4, 3.8, 0.03, 1.6, 0.015, "M_Metal")
    f.cylinder(-5.4, 3.8, 1.45, 1.75, 0.2, "M_Bedding")
    f.box(-5.85, -0.6, 0.0, -5.5, 1.2, 2.0, "M_Wood")
    for z in (0.5, 1.0, 1.5):
        f.box(-5.84, -0.58, z, -5.51, 1.18, z + 0.03, "M_Bedding")

    # --- Kitchen / dining ----------------------------------------------------
    # L-shaped counter along the south and west walls
    f.box(-5.85, -4.35, 0.0, -2.0, -3.75, 0.88, "M_Wood")
    f.box(-5.87, -4.37, 0.88, -2.0, -3.73, 0.92, "M_Counter")
    f.box(-5.85, -3.75, 0.0, -5.25, -1.6, 0.88, "M_Wood")
    f.box(-5.87, -3.75, 0.88, -5.23, -1.6, 0.92, "M_Counter")
    # Upper cabinets (leave the window over the sink clear)
    f.box(-3.0, -4.35, 1.5, -2.0, -4.0, 2.3, "M_Wood")
    f.box(-5.85, -3.0, 1.5, -5.5, -1.6, 2.3, "M_Wood")
    # Fridge
    f.box(-1.95, -4.35, 0.0, -1.2, -3.65, 2.0, "M_Metal")
    # Sink + hob
    f.box(-4.3, -4.25, 0.86, -3.7, -3.85, 0.93, "M_Metal")
    f.box(-5.75, -3.2, 0.92, -5.35, -2.6, 0.93, "M_Counter")
    # Dining table + 4 chairs
    f.box(-3.4, -3.0, 0.72, -1.8, -2.1, 0.76, "M_Wood")
    for (x, y) in ((-3.35, -2.95), (-1.85, -2.95), (-3.35, -2.15), (-1.85, -2.15)):
        f.box(x - 0.03, y - 0.03, 0.0, x + 0.03, y + 0.03, 0.72, "M_Wood")
    for (x, y, back_dy) in ((-3.0, -3.35, -0.2), (-2.2, -3.35, -0.2), (-3.0, -1.75, 0.2), (-2.2, -1.75, 0.2)):
        f.box(x - 0.21, y - 0.2, 0.43, x + 0.21, y + 0.2, 0.47, "M_Fabric")
        f.box(x - 0.2, y - 0.19, 0.0, x + 0.2, y + 0.19, 0.43, "M_Wood")
        by = y + back_dy
        f.box(x - 0.21, min(by, by - math.copysign(0.03, back_dy)), 0.47,
              x + 0.21, max(by, by - math.copysign(0.03, back_dy)), 0.95, "M_Wood")

    # --- Bedroom ---------------------------------------------------------------
    f.box(2.6, 2.0, 0.0, 4.6, 4.3, 0.35, "M_Wood")          # bed frame
    f.box(2.65, 2.05, 0.35, 4.55, 4.25, 0.6, "M_Bedding")   # mattress
    f.box(2.6, 4.25, 0.0, 4.6, 4.33, 1.1, "M_Wood")         # headboard
    f.box(2.8, 3.75, 0.6, 3.5, 4.15, 0.72, "M_Bedding")     # pillow L
    f.box(3.7, 3.75, 0.6, 4.4, 4.15, 0.72, "M_Bedding")     # pillow R
    f.box(2.65, 2.05, 0.6, 4.55, 3.1, 0.64, "M_Fabric")     # throw
    f.box(2.05, 3.85, 0.0, 2.5, 4.3, 0.55, "M_Wood")        # nightstand L
    f.box(4.7, 3.85, 0.0, 5.15, 4.3, 0.55, "M_Wood")        # nightstand R
    f.box(5.2, 0.7, 0.0, 5.85, 2.6, 2.2, "M_Wood")          # wardrobe
    f.box(1.2, 0.65, 0.73, 2.4, 1.25, 0.77, "M_Wood")       # desk top
    f.box(1.2, 0.65, 0.0, 1.25, 1.25, 0.73, "M_Wood")
    f.box(2.35, 0.65, 0.0, 2.4, 1.25, 0.73, "M_Wood")
    f.box(1.6, 1.35, 0.0, 2.0, 1.75, 0.46, "M_Fabric")      # desk chair

    # --- Bathroom --------------------------------------------------------------
    # Bathtub along the east wall: outer shell minus a hollow, built from boxes.
    tx0, tx1, ty0, ty1, th = 5.1, 5.85, -2.3, -0.6, 0.55
    w = 0.08
    f.box(tx0, ty0, 0.0, tx1, ty1, 0.1, "M_Ceramic")             # base
    f.box(tx0, ty0, 0.1, tx0 + w, ty1, th, "M_Ceramic")          # west rim
    f.box(tx1 - w, ty0, 0.1, tx1, ty1, th, "M_Ceramic")          # east rim
    f.box(tx0 + w, ty0, 0.1, tx1 - w, ty0 + w, th, "M_Ceramic")  # south rim
    f.box(tx0 + w, ty1 - w, 0.1, tx1 - w, ty1, th, "M_Ceramic")  # north rim
    # Toilet
    f.box(5.45, -3.6, 0.0, 5.85, -3.2, 0.4, "M_Ceramic")
    f.box(5.7, -3.65, 0.4, 5.85, -3.15, 0.8, "M_Ceramic")
    # Vanity + basin + mirror
    f.box(3.65, -4.35, 0.0, 4.65, -3.85, 0.82, "M_Wood")
    f.box(3.63, -4.37, 0.82, 4.67, -3.83, 0.86, "M_Ceramic")
    f.box(3.9, -4.36, 1.2, 4.4, -4.34, 1.9, "M_Metal")

    # --- Hall ------------------------------------------------------------------
    f.box(1.15, -4.3, 0.0, 1.55, -3.0, 1.0, "M_Wood")       # shoe cabinet
    f.box(1.2, -1.2, 0.0, 1.6, -0.9, 1.8, "M_Wood")         # coat stand block

    objs = [walls.build(coll), ceiling.build(coll), floor.build(coll), furn.build(coll)]
    return objs


def export_fbx(path, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={"MESH"},
        mesh_smooth_type="FACE",
        # Blender's defaults (metres, -Z forward / Y up) are what Unreal's FBX
        # importer expects: it arrives at 100 uu per metre, Z up, unmirrored.
        apply_scale_options="FBX_SCALE_NONE",
        axis_forward="-Z",
        axis_up="Y",
        add_leaf_bones=False,
        bake_anim=False,
        use_mesh_modifiers=True,
    )


if __name__ == "__main__":
    built = build_apartment()
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if "--export" in argv:
        export_fbx(argv[argv.index("--export") + 1], built)
