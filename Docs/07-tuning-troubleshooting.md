# 7. Tuning and troubleshooting

[← The apartment and the Blender pipeline](06-apartment-blender.md) · [Docs index](README.md)

## Tuning

All of these are in **Project Settings › Plugins › X-Ray Cube** (and on the
subsystem at runtime, see [C++ reference](04-cpp-reference.md#runtime-settings)).

### Field resolution and size

The voxel size is `FieldSizeWorld / FieldResolution`. The default, 1600 × 1600
× 800 uu over 128 × 128 × 64 voxels, gives **12.5 cm voxels**.

- **Flat faces are exact at any voxel size.** Trilinear interpolation of a
  plane's distance field reproduces the plane exactly, so the walls of the
  cut are perfectly flat even with coarse voxels.
- **Edges and corners round off** to about one voxel. A cube's edge has
  a kink in its distance field that a 12.5 cm voxel can't represent, so cut
  edges look slightly bevelled up close. Halve the voxel size to sharpen them;
  memory and compute grow 8×.
- **The field must contain the cutters.** It follows them (centred on their
  bounding box), but the bounding box of all cutters has to fit inside
  `FieldSizeWorld`, or the cut stops at the field's edge. The subsystem warns
  once if it doesn't fit.

| Resolution | Field memory (2× R32F + RGBA16F) |
|---|---|
| 64 × 64 × 32 | 2 MB |
| 128 × 128 × 64 (default) | 16 MB |
| 256 × 256 × 128 | 128 MB |

If you configure a different size, resize `RTV_XRayField` to match. The output
asset's size wins over `FieldResolution`.

### Response (open/heal speed)

`Response` is the rate of the exponential ease, in 1/s. The cut is 95 %
there after `3 / Response` seconds: **0.25 s** at the default 12. Higher
values are snappier. `0` turns easing off entirely: the field jumps straight to
the cutters, and the subsystem dispatches only on frames where something
moved.

### BlendRadius

`0` (default) unions overlapping cubes with a hard crease. A positive
value (say 30–80 uu) melts them into one rounded cavity. See
[smooth union](03-shader-walkthrough.md#combining-cutters-smooth-union).

### MaxDistance

The clamp on stored distances, and the "nothing here" value. It only needs
to be larger than the widest effect you drive from the distance (the glow is
6 cm by default), so the 200 uu default leaves plenty of headroom. Too small
and effects that read `Distance` (the `B` channel) saturate early.

### Material look

On `M_XRayCutaway` instances: `GlowWidth` (cm), `GlowColor`,
`GlowIntensity`, `Cuttable`. On `M_XRayCube`: `Color`, `LinePixels`,
`EdgeIntensity`, `FillOpacity`.

## Troubleshooting

### Nothing is cut

Check, in order:

1. **Is there a cutter?** An `AXRayCubeActor`, or any actor with an
   `X-Ray Cutter` component with *Cutter Enabled* on.
2. **Is the material cuttable?** It must be (an instance of) `M_XRayCutaway`
   or contain the Custom node, with `Cuttable = 1`, and be **Masked**.
3. **Does the material sample the same render target the subsystem writes?**
   The output log at world start says
   `X-ray field allocated: ... output: RTV_XRayField`. If it says
   `output: transient`, the configured asset failed to load: check the
   *Field Render Target* setting.
4. **Is the field running in this world?** In the editor viewport,
   *Run In Editor* must be on. While PIE runs, the editor world deliberately
   stops updating (the PIE world owns the field).
5. **Are the shaders compiled?** Right after opening a level, materials
   render with the default grey material until their shaders finish
   compiling (watch the *Compiling Shaders* counter, bottom right).

### Warnings in the output log

| Message | Meaning / fix |
|---|---|
| `Created X-ray field render target ...` | The configured RTV didn't exist, so the editor made one. **Save it.** |
| `... could not be loaded; falling back to a transient target` | The configured asset exists on disk but failed to load, or this is a cooked build without it. Materials that reference the asset see no cut. |
| `... is not RGBA16f` | The RTV's format is fixed-point, which clamps away negative distances (the inside of every cut). Set its *Override Format* to `PF_FloatRGBA`. |
| `... did not have Supports UAV set; enabling it for this session` | Tick *Supports UAV* on the RTV and save it. |
| `Material Parameter Collection ... needs vector parameters named ...` | Add `XRayFieldCenter` and `XRayFieldSize` vectors to the MPC, or fix the names in settings. |
| `X-ray cutters span ... but the field only covers ...` | Cutters are spread further apart than the field is large. Raise `FieldSizeWorld` (and the resolution, to keep the voxel size). |

### The editor won't start after a shader change

A global shader (`XRayCube.usf`) that fails to compile stops the editor at
startup. Before restarting, run the offline check:

```bash
Tools/ShaderCheck/check_shaders.sh   # compiles both kernels + the material helpers with the engine's DXC
```

It needs only the engine install (`UE_ROOT` env var, default
`/Users/Shared/Epic Games/UE_5.8`), not a running editor. If the editor is
already stuck, revert the shader change, or temporarily disable the plugin in
`MyProject.uproject`.

### Saved volume render target loses its UAV flag

`UTextureRenderTarget::bCanCreateUAV` is **not serialised**. For a saved
`UTextureRenderTargetVolume` asset the persistent flag is `bSupportsUAV`
(*Supports UAV* in the details panel), which `CreateResource()` copies into
`bCanCreateUAV`. The plugin sets both, but if you make your own RTV asset,
tick *Supports UAV*.

### Cut edges flicker or look stepped

- Stepping at cut **edges** is the voxel size (see above). Raise the
  resolution or shrink the field.
- Flicker while **moving** is the ease (`Response`) catching up. Raise
  `Response`, or set it to 0 for an instant cut.
- Shimmer on **distant** cuts is ordinary masked-material aliasing; TSR/TAA
  resolves it.

### Port 8000 / MCP (development only)

When driving the editor through the Model Context Protocol plugin, start it
with `-ModelContextProtocolStartServer`. If the editor is relaunched within a
few seconds of quitting, the old socket may still hold port 8000
(`HttpListener unable to bind to 127.0.0.1:8000` in the log). Wait a moment
and relaunch.
