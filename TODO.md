# TODO

Living checklist for the X-Ray Cube project. Ticked = done and verified in the
editor. Newest work at the top of each section.

## Milestone 1: working demo

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
- [ ] Restart editor so the plugin loads; check global shaders compile for Metal
- [ ] Plugin content: `MPC_XRayField`, `RTV_XRayField` (128×128×64 RGBA16F, UAV)
- [ ] `M_XRayCutaway` master material (masked, two-sided, edge glow) + instances
- [ ] `M_XRayCube` glass material for the cube (fresnel + edge lines)
- [ ] Import the apartment into `/XRayCube/Apartment/`, assign instances per slot
- [ ] Demo map `/XRayCube/Maps/XRayDemo` (apartment, lights, cube, player start)
- [ ] Check in the editor viewport: drag the cube and watch the cut follow it
- [ ] Check in PIE: field runs in the game world, editor world steps aside
- [ ] Screenshots in `Docs/Images/`

## Milestone 2: documentation

- [ ] README: what it is, quick start, how it works (first pass done)
- [x] Docs/01 Architecture (why a 3D SDF, why scrolling + voxel snapping)
- [x] Docs/02 GPU compute pipeline (RDG passes, UAVs, 3D thread groups)
- [x] Docs/03 Shader walkthrough (box SDF, smooth union, temporal ease, gradient)
- [ ] Docs/04 C++ reference
- [ ] Docs/05 Material wiring (Custom node + include, MPC, LWC-safe subtraction)
- [ ] Docs/06 Troubleshooting & tuning

## Ideas / backlog

- [ ] Cut "caps": shade the inside faces of cut walls as a solid section colour
      (use the field gradient as the cap normal) instead of seeing hollow walls
- [ ] More cutter shapes: sphere, capsule, cylinder (all have cheap exact SDFs)
- [ ] Invert mode: show *only* what is inside the cube
- [ ] Per-material falloff: dissolve noise / dither in a band around the cut
- [ ] Multiple independent fields (e.g. one per floor of a building)
- [ ] Hierarchical skip: tile-level culling so voxels far from every cutter
      skip the per-cutter loop (matters once there are dozens of cutters)
- [ ] Runtime gizmo: grab and move the cube with the mouse in PIE
- [ ] Nanite / WPO-friendly variant (masked materials disable some Nanite paths)
