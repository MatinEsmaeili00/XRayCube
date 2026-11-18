# X-Ray Cube

An Unreal Engine 5 plugin for **compute-shader cut-away volumes**: drop a cube
into a building, move it around, and every wall and ceiling inside the cube
disappears so you can see through the apartment.

Built in the same low-level style as
[GPULab](https://github.com/MatinEsmaeili00/GPULab) and
[SnowDeformation](https://github.com/MatinEsmaeili00/SnowDeformation)
(`FGlobalShader` + `SHADER_PARAMETER_STRUCT` + RDG + `ENQUEUE_RENDER_COMMAND`,
a world subsystem, ping-pong persistent state), taken from a 2D height field
to a **3D signed distance field**.

Developed against **UE 5.8**. Work in progress: see [TODO.md](TODO.md).

## How it works

```
UXRayCutterComponent (a UBoxComponent; AXRayCubeActor has one as its root)
        │  OnRegister → registers with the world's subsystem (editor and game)
        ▼
UXRayCubeSubsystem::Tick()   (game thread, once per world per frame)
        │  1. pull every cutter's oriented box
        │  2. centre the field on them, snapped to whole voxels
        │  3. convert to field-local FXRayCutterGPU (64 bytes each)
        │  4. skip entirely if nothing moved and the field has settled
        │  5. ENQUEUE_RENDER_COMMAND → XRayCube::Dispatch_RenderThread
        │  6. push XRayFieldCenter / XRayFieldSize into the MPC
        ▼
XRayCube.usf (render thread, GPU, 4×4×4 thread groups, one voxel each)
        │  Pass 1 XRayFieldUpdateCS:   reproject last frame's volume (integer
        │         voxel shift), evaluate exact oriented-box SDFs, smooth-union
        │         them, ease the stored field towards the result
        │  Pass 2 XRayFieldGradientCS: distance → RGBA16F (R = distance,
        │         GBA = direction out of the cut)
        ▼
RTV_XRayField (volume render target) → any material using XRayCubeMaterial.ush
        OpacityMask = distance > 0, Emissive = glow near distance ≈ 0
```

The material does **one** 3D texture fetch per pixel no matter how many cubes
there are. The compute shader does the per-cutter work once per voxel, and only
when something has changed.

## Layout

```
XRayCube/
├── XRayCube.uplugin
├── Shaders/Private/XRayCube.usf            - XRayFieldUpdateCS, XRayFieldGradientCS
├── Shaders/Private/XRayCubeMaterial.ush    - material-side helpers (Custom node include)
├── Source/XRayCube/
│   ├── Public/XRayCubeComputePass.h        - FXRayCutterGPU, dispatch params, shader classes
│   ├── Private/XRayCubeComputePass.cpp     - shader registration + RDG dispatch
│   ├── Public/XRayCubeSettings.h           - Project Settings > Plugins > X-Ray Cube
│   ├── Public/XRayCubeSubsystem.h          - UTickableWorldSubsystem that owns the field
│   ├── Public/XRayCutterComponent.h        - box component that cuts
│   └── Public/XRayCubeActor.h              - the cube: cutter + glass mesh (+ follow-camera mode)
├── Content/                                - MPC, volume RT, materials, apartment, demo map
├── Art/Apartment.fbx, Apartment.blend      - demo apartment source art
├── Tools/Blender/build_apartment.py        - regenerates the apartment procedurally
└── Docs/                                   - deep-dive documentation
```

## License

Copyright Matin. All rights reserved.
