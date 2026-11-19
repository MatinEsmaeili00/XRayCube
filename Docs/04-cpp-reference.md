# 4. C++ reference

[← Shader walkthrough](03-shader-walkthrough.md) · Next: [Material wiring →](05-material-wiring.md)

Every public type in `Source/XRayCube/`, what it's for, and how to use it from
C++ or Blueprint.

## `AXRayCubeActor`

`Public/XRayCubeActor.h`. The thing you drag into a level.

| Component | Type | Role |
|---|---|---|
| `Cutter` (root) | `UXRayCutterComponent` | the box that cuts; default extent 150 (a 3 m cube) |
| `CubeMesh` | `UStaticMeshComponent` | `/Engine/BasicShapes/Cube`, scaled to match the cutter, no collision, no shadow |

| Property | Default | Notes |
|---|---|---|
| `CubeMaterial` | `/XRayCube/Materials/M_XRayCube` | soft reference, loaded in `OnConstruction` |
| `bShowCube` | `true` | hide the glass and keep the cut |
| `bFollowPlayerCamera` | `false` | in play, keep the cube in front of player 0's camera ("x-ray goggles") |
| `FollowDistance` | 250 | distance from the camera to the cube's centre |
| `FollowSmoothing` | 0.08 | exponential smoothing time; 0 = rigidly attached |

**Sizing.** Scale the actor, or change the `Cutter`'s *Box Extent*. Both work:
`SyncCubeMesh()` sets the mesh's relative scale to `BoxExtent / 50` (the
engine cube is 100 units across), so the glass always matches the cut.

**Follow mode** only rotates the cube in yaw. A cube that pitched with the
camera would cut floors and ceilings at an angle as you looked up and down,
which is disorienting.

## `UXRayCutterComponent`

`Public/XRayCutterComponent.h`. A `UBoxComponent` that registers itself as a
cutter. **Add it to any actor** to make that actor cut: a door that reveals the
room behind it, a character with an x-ray radius, a spline of section boxes.

| Property | Default | Notes |
|---|---|---|
| `bCutterEnabled` | `true` | toggle at runtime; the field eases the hole closed |
| `CornerRadius` | 0 | rounds the box's edges and corners (free with an SDF) |
| *Box Extent* (inherited) | | the half size, multiplied by the component's world scale |

It never ticks and has no collision. It's drawn in cyan in the editor and
hidden in game. `OnRegister` / `OnUnregister` add and remove it from the
world's `UXRayCubeSubsystem`, so it works in the editor viewport, in PIE and
in packaged games with no setup.

```cpp
bool GetCutterShape(FXRayCutterShape& OutShape) const;
// Center (world), Rotation (FQuat), HalfExtent (scaled, abs), CornerRadius.
// Returns false when disabled or the box is degenerate.
```

## `UXRayCubeSubsystem`

`Public/XRayCubeSubsystem.h`. A `UTickableWorldSubsystem`, so there is **one
per world**: the editor world, each PIE world and the game world each own one.
It owns the render targets, gathers cutters, dispatches the compute passes
and pushes material parameters. See
[Architecture](01-architecture.md#the-pieces) for why it's a subsystem.

### Blueprint API

| Function | Returns |
|---|---|
| `GetFieldRenderTarget()` | the volume the materials should sample (`RTV_XRayField`, or a transient one) |
| `GetFieldCenterParameter()` | `xyz` = field centre (world), `w` = 1 when the field is valid |
| `GetFieldSizeParameter()` | `xyz` = field size (world), `w` = `MaxDistance` |
| `ApplyFieldParametersToMaterial(MID)` | sets the texture and both vectors on a dynamic material instance |
| `GetNumActiveCutters()` | cutters that contributed last frame |

`ApplyFieldParametersToMaterial` is the MPC-free path. Use it for a material
that can't reference the plugin's MPC or render target asset, for example one
that lives in another plugin.

### Runtime settings

`FieldResolution`, `FieldSizeWorld`, `MaxDistance`, `BlendRadius` and
`Response` are `BlueprintReadWrite` copies of the project settings, taken in
`Initialize`. Changing `BlendRadius`, `MaxDistance` or `Response` takes effect
on the next tick. Changing `FieldResolution` or `FieldSizeWorld` reallocates
the volumes on the next tick (and the output asset's size wins over
`FieldResolution` when one is configured).

### Lifecycle

```
Initialize            copy settings; load the RTV + MPC (editor: create the RTV if missing)
Tick (every frame)
  ├─ suspended?       the editor world returns early while any PIE world runs
  ├─ gather cutters   AABB of every enabled cutter
  ├─ place field      centre on the AABB, snap to whole voxels → integer reprojection offset
  ├─ changed?         memcmp of the GPU cutter array + settings; else count down the settle timer
  ├─ dispatch         ENQUEUE_RENDER_COMMAND → XRayCube::Dispatch_RenderThread
  └─ push MPC         XRayFieldCenter, XRayFieldSize
Deinitialize          release render targets
```

**Editor vs PIE.** The editor world and a PIE world both write
`RTV_XRayField`, since there is only one asset. `IsSuspendedForPIE()` makes
the editor world stand aside while any PIE world is alive, then rebuild
from scratch when PIE ends.

**Bootstrapping the asset.** If the configured `FieldRenderTarget` path doesn't
exist (a fresh checkout without the content folder, or a renamed asset), the
editor creates it there: 128×128×64, `PF_FloatRGBA`, `bSupportsUAV`. It marks
it dirty and logs `Created X-ray field render target ...`. Save it, then
point your materials at it. Cooked games and commandlets never create assets.

## `UXRayCubeSettings`

`Public/XRayCubeSettings.h`. **Project Settings › Plugins › X-Ray Cube**, saved to
`Config/DefaultGame.ini`. Read once per world in `UXRayCubeSubsystem::Initialize`.

| Setting | Default | |
|---|---|---|
| `FieldResolution` | 128 × 128 × 64 | voxels |
| `FieldSizeWorld` | 1600 × 1600 × 800 | world units covered → 12.5 cm voxels |
| `MaxDistance` | 200 | distances are clamped to ±this; also the "nothing here" value |
| `BlendRadius` | 0 | smooth-union radius between overlapping cutters |
| `Response` | 12 | open/heal speed (1/s); 0 = instant, and dispatch only on change |
| `bRunInEditor` | `true` | update the field in the editor viewport, not only in play |
| `FieldRenderTarget` | `/XRayCube/Materials/RTV_XRayField` | the volume materials sample |
| `ParameterCollection` | `/XRayCube/Materials/MPC_XRayField` | receives the two vectors |
| `CenterParameterName` | `XRayFieldCenter` | |
| `SizeParameterName` | `XRayFieldSize` | |
| `FieldTextureParameterName` | `XRayField` | used by `ApplyFieldParametersToMaterial` |

## Render-thread API

`Public/XRayCubeComputePass.h`. You only need this if you want to drive the
passes yourself (say, from a custom scene view extension).

```cpp
namespace XRayCube
{
    constexpr int32 ThreadGroupSize = 4;   // 4×4×4, fed to HLSL as THREADS_X/Y/Z

    XRAYCUBE_API void Dispatch_RenderThread(FRHICommandListImmediate& RHICmdList,
                                            const FXRayFieldDispatchParams& Params);
}

struct FXRayFieldDispatchParams
{
    FRHITexture* PrevFieldTexture;   // R32F, read
    FRHITexture* NextFieldTexture;   // R32F, written by pass 1, read by pass 2
    FRHITexture* FieldDataTexture;   // RGBA16F, written by pass 2
    FIntVector   Resolution;
    FVector3f    FieldSize;
    FIntVector   PrevVoxelOffset;    // integer reprojection
    float        MaxDistance, BlendRadius, BlendAlpha, ClearMask;
    TArray<FXRayCutterGPU> Cutters;  // field-local
};
```

`FXRayCutterGPU` (64 bytes) is documented in
[the compute pipeline](02-gpu-compute-pipeline.md#the-gpu-struct). Cutter
centres are **relative to the field centre**, so the GPU never sees large world
coordinates (LWC-safe by construction).

## Adding a cutter from C++

```cpp
// MyDoor.h
UPROPERTY(VisibleAnywhere)
TObjectPtr<UXRayCutterComponent> RevealVolume;

// MyDoor.cpp
RevealVolume = CreateDefaultSubobject<UXRayCutterComponent>(TEXT("RevealVolume"));
RevealVolume->SetupAttachment(RootComponent);
RevealVolume->SetBoxExtent(FVector(120, 40, 110));
RevealVolume->CornerRadius = 10.0f;

// Open the wall when the player gets close:
RevealVolume->bCutterEnabled = bPlayerNearby;
```

Add `"XRayCube"` to your module's `PublicDependencyModuleNames`.
