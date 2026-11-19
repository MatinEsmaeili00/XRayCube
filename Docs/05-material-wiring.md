# 5. Material wiring

[← C++ reference](04-cpp-reference.md) · Next: [The apartment and the Blender pipeline →](06-apartment-blender.md)

How a material reads the field, node by node, and how to make your own
materials cuttable.

## The assets

| Asset | Type | Role |
|---|---|---|
| `Materials/RTV_XRayField` | Texture Render Target Volume, 128×128×64, RGBA16F, *Supports UAV* | the field; written by the compute passes, sampled by materials |
| `Materials/MPC_XRayField` | Material Parameter Collection | `XRayFieldCenter` (xyz centre, w = valid), `XRayFieldSize` (xyz size, w = MaxDistance) |
| `Materials/M_XRayCutaway` | Material, Masked, Two Sided | master material for anything that should be cut |
| `Materials/M_XRayCube` | Material, Translucent, Unlit, Two Sided | the glass cube |
| `Apartment/Materials/MI_*` | Material instances of `M_XRayCutaway` | one per apartment surface |

## `M_XRayCutaway`, node by node

```
TextureObject (RTV_XRayField) ───────────────────────────► Field
WorldPosition ──► Subtract.A                                         ┌──────────────────────┐
CollectionParameter XRayFieldCenter ──► Mask(RGB) ──► Subtract.B     │ Custom               │
                       Subtract ──────────────────────► LocalPos     │ XRayCube_Evaluate    │──► Mask(R) ──► Opacity Mask
CollectionParameter XRayFieldCenter ──────────────────► FieldCenter  │ Output: Float4       │──► Mask(G) × GlowColor × GlowIntensity ──► Emissive
CollectionParameter XRayFieldSize ────────────────────► FieldSize    │ Include: /XRayCube…  │
ScalarParameter Cuttable (1) ─────────────────────────► Cuttable     └──────────────────────┘
ScalarParameter GlowWidth (6) ────────────────────────► GlowWidth
VectorParameter BaseColor ──► Base Color        ScalarParameter Roughness ──► Roughness
```

The Custom node:

| Field | Value |
|---|---|
| Code | `return XRayCube_Evaluate(Field, FieldSampler, LocalPos, FieldCenter, FieldSize, Cuttable, GlowWidth);` |
| Output Type | `CMOT Float 4` |
| Include File Paths | `/XRayCubeShaders/Private/XRayCubeMaterial.ush` |
| Inputs | `Field`, `LocalPos`, `FieldCenter`, `FieldSize`, `Cuttable`, `GlowWidth` |

`/XRayCubeShaders` is the virtual path the module maps to the plugin's
`Shaders/` folder at `PostConfigInit` (see
[the compute pipeline](02-gpu-compute-pipeline.md#global-shaders-and-the-virtual-shader-path)).
Keeping the HLSL in a file rather than in the Custom node's text box means one
copy of the logic, version control that diffs, and the offline DXC check in
`Tools/ShaderCheck/` covers it.

A **TextureObject** wired into a Custom node input arrives in HLSL as a
`Texture3D Field` plus a matching `SamplerState FieldSampler`. That's why the
code passes `FieldSampler` without a node feeding it.

### Why subtract in the graph

`WorldPosition` is a large-world-coordinate (double precision) value. The
subtraction `WorldPosition − XRayFieldCenter` happens in the material graph,
where UE handles LWC, so the Custom node only ever receives a small offset
from the field centre that fits a float comfortably. Doing the subtraction
inside the Custom node would truncate the world position to a float first,
which is fine near the origin and jittery kilometres away.

### What the four outputs mean

`XRayCube_Evaluate` returns `float4(Mask, Glow, Distance, Inside)`:

| Channel | Meaning | Used for |
|---|---|---|
| `R` Mask | 0 inside a cutter, 1 outside | Opacity Mask (clip value 0.5) |
| `G` Glow | peaks at the cut face, fades over `GlowWidth` cm | Emissive, tinted by `GlowColor` |
| `B` Distance | signed distance to the nearest cutter (cm) | free for your own effects |
| `A` Inside | 1 when the pixel is inside the field's volume | debugging, fades |

Outside the field, or when `XRayFieldCenter.w` is 0 (no subsystem has
written the field yet), or when `Cuttable` is 0, the distance is `MaxDistance`,
which means "nowhere near a cutter". Nothing is clipped, so materials look
normal in thumbnails, asset previews and levels without a cube.

### Why Masked and Two Sided

- **Masked**, not Translucent: the cut has a hard edge, keeps depth writes,
  shadows, SSAO and every other opaque-pass feature, and sorts correctly.
- **Two Sided**: once the front face of a wall is clipped you look *into*
  the wall and see the back faces of its other side. Single-sided, the wall
  would just vanish. Two-sided, you see the far face of the wall, which reads as
  the wall's thickness at the cut.

Masked materials also clip their **shadows**. Sunlight falls through the
opening into the room, which for a cutaway is what you want.

## Per-surface instances

The apartment has one material slot per surface type (from the Blender
materials), each with a `MI_*` instance:

| Instance | Cuttable | Why |
|---|---|---|
| `MI_WallExterior`, `MI_WallInterior`, `MI_Ceiling` | 1 | the things you want to see through |
| `MI_FloorWood`, `MI_FloorTile` | 0 | a cut floor would show the void underneath |
| `MI_Wood`, `MI_Fabric`, `MI_Bedding`, `MI_Counter`, `MI_Metal`, `MI_Ceramic`, `MI_Rug` | 0 | furniture stays whole, so the room reads as a room |

`Cuttable` is a plain scalar, so you can flip it per instance without a
shader recompile. `/XRayCube/Maps/MI_DemoGround` (the demo level's ground)
is another instance of the same master with `Cuttable = 0`.

## Making your own material cuttable

**Option A: instance the master.** Create a Material Instance of
`M_XRayCutaway` and set `BaseColor` / `Roughness`. Fine for flat-coloured
geometry like the demo apartment.

**Option B: add the cut to an existing material.** Copy these nodes from
`M_XRayCutaway` into your material:

1. `TextureObject` (RTV_XRayField), `WorldPosition`, the two
   `CollectionParameter` nodes, the `ComponentMask` + `Subtract`.
2. The Custom node (same code, include path and inputs).
3. Set the material to **Masked** + **Two Sided** and wire `Mask(R)` into
   Opacity Mask. If your material is already masked, multiply the two masks.
4. Optionally add `Mask(G) × colour` to your emissive.

**Option C: dynamic material instances.** If a material can't reference the
plugin assets, give it a `TextureObjectParameter` named `XRayField` and two
vector parameters named `XRayFieldCenter` / `XRayFieldSize`. Then call
`UXRayCubeSubsystem::ApplyFieldParametersToMaterial(MID)` each frame (or when
`GetFieldCenterParameter()` changes).

## `M_XRayCube`: the glass

A translucent, unlit, two-sided material on the engine cube:

```hlsl
float Edge = XRayCube_BoxEdge(LocalPos, 50.0f, LinePixels);         // constant-width outline
float3 Emissive = Color * (Edge * EdgeIntensity + Fresnel * 1.5f + 0.1f);
float Opacity = saturate(Edge + Fresnel * 0.5f + FillOpacity);
return float4(Emissive, Opacity);
```

`LocalPos` is the `LocalPosition` node (object space, before actor scale), so
`HalfSize = 50` is correct at any actor scale. The outline is `LinePixels`
wide on screen at any distance, because `XRayCube_BoxEdge` uses `fwidth()`
(see [the shader walkthrough](03-shader-walkthrough.md#material-side)).

| Parameter | Default |
|---|---|
| `Color` | (0.1, 0.75, 1.0) |
| `LinePixels` | 1.5 |
| `EdgeIntensity` | 8 |
| `FillOpacity` | 0.03 |

## Nanite

Nanite supports masked materials, at a cost (it has to evaluate the
mask during rasterisation). The demo meshes aren't Nanite. For a Nanite-heavy
scene, see the Nanite/WPO item in the [TODO](../TODO.md).
