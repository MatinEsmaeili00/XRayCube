# TODO

Living checklist for the X-Ray Cube project. Ticked = done and verified in the
editor. Newest work at the top of each section.

## Milestone 1: working demo ✅

- [x] Procedural apartment in Blender (`Tools/Blender/build_apartment.py`):
      5 rooms, thick walls with door/window openings, ceiling slab, furniture
- [x] Export `Art/Apartment.fbx` + `Art/Apartment.blend`
- [x] Plugin skeleton: module, shader directory mapping, settings, subsystem
- [x] Compute passes: `XRayFieldUpdateCS` (reproject + oriented-box SDF + ease),
      `XRayFieldGradientCS` (distance + gradient → RGBA16F volume)
- [x] `UXRayCutterComponent` (box component that cuts) + `AXRayCubeActor`
- [x] Material HLSL in `Shaders/Private/XRayCubeMaterial.ush`
- [x] Builds cleanly against UE 5.8 (Mac, arm64)
- [x] Shaders compile under DXC SM6 (`Tools/ShaderCheck/check_shaders.sh`):
      both compute kernels + the material helpers
- [x] Apartment imported headless (ImportAssets commandlet, Interchange) into
      `/XRayCube/Apartment/` as 4 static meshes
- [x] Editor restart: plugin loads, both global shaders compile for Metal SM6
- [x] Editor bootstraps `RTV_XRayField` when missing (128×128×64 RGBA16F,
      `bSupportsUAV` saved, since `bCanCreateUAV` isn't serialised)
- [x] `MPC_XRayField` (`XRayFieldCenter`, `XRayFieldSize`)
- [x] `M_XRayCutaway` master (masked, two-sided, Custom node + include, edge glow)
- [x] 12 `MI_*` surface instances with the Blender colours; walls/ceiling
      `Cuttable = 1`, floor/furniture `0`; assigned to the meshes, importer
      placeholder materials removed
- [x] `M_XRayCube` glass (translucent unlit, constant-pixel edges + fresnel)
- [x] Demo map `/XRayCube/Maps/XRayDemo` (Basic template, apartment, matte
      ground, cube on the living-room corner, player start inside)
- [x] Viewport check: moving / rotating the cube moves the cut, old hole heals
- [x] PIE check: PIE world allocates and drives the field, editor world steps
      aside and rebuilds when PIE ends; no errors in the log
- [x] Fresh-launch check: assets load from disk, cut appears, no warnings
- [x] Screenshots in `Docs/Images/`
- [x] Out-of-bounds warning only fires when the cutters really don't fit
      (was counting the `MaxDistance` margin)

## Milestone 2: documentation ✅

- [x] README: what it is, quick start, how it works, screenshots
- [x] Docs/01 Architecture (why a 3D SDF, why scrolling + voxel snapping)
- [x] Docs/02 GPU compute pipeline (RDG passes, UAVs, 3D thread groups)
- [x] Docs/03 Shader walkthrough (box SDF, smooth union, temporal ease, gradient)
- [x] Docs/04 C++ reference
- [x] Docs/05 Material wiring (Custom node + include, MPC, LWC-safe subtraction)
- [x] Docs/06 Apartment & Blender pipeline
- [x] Docs/07 Tuning & troubleshooting
- [x] Docs/README index + relation to GPULab / SnowDeformation

## Next up

- [ ] Cleaner screenshot set: hide editor sprites (Game View) and add a
      PIE / first-person shot through the cut
- [ ] Short GIF/video of the cut opening and healing as the cube moves
- [ ] Automation test: spawn a cutter, tick the subsystem, read back a voxel
      (inside < 0, outside = MaxDistance)
- [ ] Section caps: shade the inside faces of cut walls as a solid section
      colour (use the field gradient, already in GBA, as the cap normal)
      instead of seeing the wall's far face
- [ ] Packaged-build check (cook the demo map, run on Mac)

## Ideas / backlog

- [ ] More cutter shapes: sphere, capsule, cylinder (all have cheap exact SDFs)
- [ ] Invert mode: show *only* what is inside the cube
- [ ] Per-material falloff: dissolve noise / dither in a band around the cut
- [ ] Multiple independent fields (e.g. one per floor of a building)
- [ ] Hierarchical skip: tile-level culling so voxels far from every cutter
      skip the per-cutter loop (matters once there are dozens of cutters)
- [ ] Runtime gizmo: grab and move the cube with the mouse in PIE
- [ ] Nanite / WPO-friendly variant (masked materials disable some Nanite paths)
