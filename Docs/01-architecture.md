# 1. Architecture

[← Docs index](README.md) · Next: [The GPU compute pipeline →](02-gpu-compute-pipeline.md)

## The problem

You want to place a cube in a building and have every wall, ceiling and
partition *inside* that cube vanish, so you can look into the rooms behind it.
The cube can move, rotate and scale every frame. There may be more than one.

The obvious approach is to give every wall material the cube's transform and
let the pixel shader test "am I inside the box?". That works for one cube.
It stops working well once you want any of the following:

- **Several cutters.** Every material has to loop over every cube, every
  pixel, every frame, and you need a fixed upper bound on the count baked
  into the material.
- **Soft unions.** Overlapping cubes merging into one rounded cavity is a
  smooth-min across all cutters, which again happens per pixel.
- **Animation over time.** A cut that grows open when the cube arrives and
  heals closed when it leaves needs *memory*. A pixel shader has none.

## The central idea: move the per-cutter work into a volume

Instead of every pixel asking every cutter, a compute shader asks every cutter
**once per voxel** and writes the answer into a 3D texture. Materials then do
**one texture fetch** to find out whether they are inside a cut.

What goes into each voxel is the **signed distance** to the union of all the
cutter boxes:

```
   d < 0   inside a cutter    → the material clips itself away
   d = 0   on a cutter face   → the glowing cut edge
   d > 0   outside            → renders normally
```

```
   cutters (exact boxes)           compute shader                 any material
  ┌─────┐                      ┌──────────────────────┐
  │  A  │  ┌───┐   ─────────►  │ 128×128×64 voxels    │  ───►  1 trilinear fetch
  └─────┘  │ B │               │ each = distance to   │        d > 0 ? keep : clip
           └───┘               │ nearest cutter face  │        glow where d ≈ 0
                               └──────────────────────┘
```

This is the SnowDeformation pattern moved up a dimension: there, a compute
shader maintains a world-anchored 2D height field that materials sample;
here, it maintains a world-anchored 3D distance field.

### Why a distance field and not a 0/1 mask

A 0/1 "inside" mask at 12.5 cm voxels would give you 12.5 cm staircase steps
along every cut edge. A signed distance field at the same resolution gives
you **perfectly flat, sharp faces**, because:

- the distance to a plane is a *linear* function of position, and
- trilinear filtering is linear interpolation,
- so interpolating samples of a linear function reproduces it exactly.

Only where the field is *not* linear (the box's edges and corners, where the
nearest face changes) does the voxel grid show, and there it rounds the corner
by about one voxel. That is the whole reason a fairly coarse volume is enough.

## Consequences of that choice

**The field has to cover the cutters, not the whole level.** A volume big
enough for a whole level at useful resolution would be huge. Instead it covers
a fixed box of world space (16 × 16 × 8 m by default) **centred on the active
cutters**, and scrolls along with them. Cuts outside the field just stop.

**Scrolling has to be exact.** When the field moves, last frame's contents
have to be shifted to stay anchored to the world. SnowDeformation re-samples
at a fractional offset with bilinear filtering, which blurs a little every
frame (fine for snow). Here the field centre is **snapped to whole voxels**,
so the shift is an integer `Load()` with no filtering, and the stored distances
never blur no matter how long the cube is dragged around.

**The field needs memory, so two volumes ping-pong.** You cannot read and
write the same texture in one dispatch. The distance field lives in two R32F
volumes: read last frame's, write this frame's, swap.

**The material needs to know where the field is.** The 3D texture has no
inherent position. A Material Parameter Collection carries the field centre
and size so a material can turn world position into UVW.

**Work in field-local space.** Everything inside the shader is relative to
the field centre, so values stay small and precise however far from the
origin the level is built.

**It should run in the editor.** Being able to drag the cube around the
viewport and see the cut follow it is most of the value. So the subsystem
supports editor worlds and cutters register in `OnRegister`, not `BeginPlay`.

## The pieces

```
UXRayCutterComponent (UBoxComponent)              ── game thread
        │  "here is my oriented box" (centre, rotation, scaled extent)
        ▼
UXRayCubeSubsystem::Tick()                        ── game thread
        │  1. pull every registered cutter's box
        │  2. centre the field on their bounds, snapped to whole voxels
        │  3. convert to field-local FXRayCutterGPU
        │  4. skip if nothing changed and the field has settled
        │  5. hand everything to the render thread
        │  6. push centre/size into the MPC
        ▼
XRayCube::Dispatch_RenderThread()                 ── render thread
        │  build an RDG graph: register volumes, upload cutters,
        │  add two compute passes, execute
        ▼
XRayCube.usf                                      ── GPU
        │  Pass 1 XRayFieldUpdateCS   : reproject → rasterise SDF → ease
        │  Pass 2 XRayFieldGradientCS : distance → distance + gradient
        ▼
RTV_XRayField → XRayCubeMaterial.ush in any material
```

## Why each piece is the kind of object it is

**`UXRayCubeSubsystem` is a `UTickableWorldSubsystem`.** The field is per
world, needs a tick, and should exist without anyone placing a manager actor.
Each world, whether editor, PIE or game, gets its own field and its own MPC
instance. It overrides `IsTickableInEditor()` to tick in the editor viewport.

**The editor world steps aside during PIE.** Both worlds would write into the
*same* render target asset, and whichever ticked last would win. So while a PIE
world exists, the editor world's subsystem doesn't dispatch, and it rebuilds
from scratch when PIE ends.

**`UXRayCutterComponent` is a `UBoxComponent`, and it does not tick.** Making it
a box component gives you the extent property, the editor wireframe, and
scaling/rotation handles for free. Like `USnowDeformerComponent`, the subsystem
*pulls* each cutter's shape once per frame, so all cutters land in one dispatch
against one field centre, and an idle cutter costs nothing.

**`AXRayCubeActor` is just a convenience.** It's a cutter root plus a glass cube
mesh sized to match. Any actor can carry a `UXRayCutterComponent`.

**`UXRayCubeSettings` is a `UDeveloperSettings`.** A world subsystem has no
details panel, so Project Settings is where the field is tuned. Its defaults
point at the render target and MPC shipped in the plugin's Content folder, so
the plugin works without any setup.

## The idle skip

Nothing is dispatched once **all** of these hold:

- the cutter set is bit-identical to last frame (an unmoved cutter produces
  identical floats, so an exact `memcmp` is the right test),
- the field centre didn't move,
- `MaxDistance` and `BlendRadius` are unchanged,
- the ease has had `7 / Response` seconds to settle (`e^-7 < 0.001`).

A level with a parked cube costs zero GPU time.
