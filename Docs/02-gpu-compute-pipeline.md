# 2. The GPU compute pipeline

[← Architecture](01-architecture.md) · Next: [Shader walkthrough →](03-shader-walkthrough.md)

Everything here is the same machinery GPULab teaches (`FGlobalShader`,
`SHADER_PARAMETER_STRUCT`, RDG, `FComputeShaderUtils::AddPass`) and
SnowDeformation uses. This page covers what's different when the target is a
**3D volume** rather than a 2D texture.

## The three threads

| Thread | What runs there | Files |
|---|---|---|
| Game thread | `UXRayCubeSubsystem::Tick`, gathering cutters, MPC updates | `XRayCubeSubsystem.cpp` |
| Render thread | building and executing the RDG graph | `XRayCubeComputePass.cpp` |
| GPU | the two compute kernels | `XRayCube.usf` |

The handoff is a single `ENQUEUE_RENDER_COMMAND`. The lambda captures the
dispatch parameters **by value** (including a copy of the cutter array) and the
three `FTextureRenderTargetResource*`. The RHI textures themselves are resolved
inside the lambda, on the render thread, because a resource can swap its
texture (resize, device reset) between the game thread reading it and the
command running.

## Global shaders and the virtual shader path

```cpp
// XRayCube.cpp - module startup (LoadingPhase = PostConfigInit)
AddShaderSourceDirectoryMapping(TEXT("/XRayCubeShaders"), <Plugin>/Shaders);

// XRayCubeComputePass.cpp
IMPLEMENT_GLOBAL_SHADER(FXRayFieldUpdateCS,   "/XRayCubeShaders/Private/XRayCube.usf", "XRayFieldUpdateCS",   SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FXRayFieldGradientCS, "/XRayCubeShaders/Private/XRayCube.usf", "XRayFieldGradientCS", SF_Compute);
```

`PostConfigInit` matters. Global shaders compile during engine startup, and a
shader type registered after that is an error. The same mapping is also what
lets a **material Custom node** `#include "/XRayCubeShaders/Private/XRayCubeMaterial.ush"`.

## Resources

| Resource | Format | Size (default) | Role |
|---|---|---|---|
| `FieldTargets[0]`, `[1]` | `PF_R32_FLOAT`, 3D, UAV | 128×128×64 × 4 B = 4 MB each | persistent distance field, ping-pong |
| `RTV_XRayField` (asset) | `PF_FloatRGBA`, 3D, UAV | 128×128×64 × 8 B = 8 MB | what materials sample |

All three are `UTextureRenderTargetVolume`s with `bCanCreateUAV = true`.
`FTextureRenderTargetVolumeResource::InitRHI` adds `TexCreate_UAV` when that
flag is set, so a compute shader can write into them directly.

**Why R32F for the state but RGBA16F for the output?** The state is eased
towards its target exponentially: `lerp(prev, target, α)` with a small α. In
half precision those small steps round to zero and the value stalls short of
the target. The output only needs a few millimetres of precision near the
surface, where half floats are densest, so it uses half the memory.

## The RDG graph

```cpp
FRDGBuilder GraphBuilder(RHICmdList);

PrevFieldRDG = RegisterExternalTexture(GraphBuilder, PrevFieldTexture, ...);  // SRV, read
NextFieldRDG = RegisterExternalTexture(GraphBuilder, NextFieldTexture, ...);  // UAV in pass 1, SRV in pass 2
FieldDataRDG = RegisterExternalTexture(GraphBuilder, FieldDataTexture, ...);  // UAV in pass 2

CutterBuffer = CreateStructuredBuffer(GraphBuilder, ..., sizeof(FXRayCutterGPU), N, Data, ...);

AddPass(XRayFieldUpdateCS)    // PrevField, Cutters  →  NextField
AddPass(XRayFieldGradientCS)  // NextField           →  FieldData

GraphBuilder.Execute();
```

RDG sees pass 1 write `NextField` through a UAV and pass 2 read it as an SRV,
and inserts the transition barrier between them. You never write one by hand.

**Structured buffers can't be empty.** With no cutters, one dummy entry is
uploaded and `NumCutters = 0` tells the shader to ignore it. The pass still runs,
so the field can ease back to "nothing cut" after the last cube is removed.

## The GPU struct

```cpp
struct FXRayCutterGPU             // 4 × float4 = 64 bytes
{
    FVector3f Center;  float CornerRadius;
    FVector3f AxisX;   float HalfX;
    FVector3f AxisY;   float HalfY;
    FVector3f AxisZ;   float HalfZ;
};
static_assert(sizeof(FXRayCutterGPU) == 64);
```

Two design points:

- **Each float3 is paired with a float** so every row is a natural 16-byte
  float4. Structured buffers don't strictly need that, but it removes any
  question of padding rules between C++ and HLSL, and the `static_assert`
  catches drift.
- **Rotation is three unit axes, not a quaternion.** World → box space becomes
  three dot products per voxel instead of quaternion maths per voxel, at the
  cost of 12 extra bytes per cutter.

## 3D thread groups

```hlsl
[numthreads(THREADS_X, THREADS_Y, THREADS_Z)]   // 4 × 4 × 4 = 64 threads
void XRayFieldUpdateCS(uint3 DTid : SV_DispatchThreadID)
```

```cpp
SET_SHADER_DEFINE(OutEnvironment, THREADS_X, XRayCube::ThreadGroupSize);   // = 4, also Y and Z
...
FComputeShaderUtils::GetGroupCount(Params.Resolution, FIntVector(XRayCube::ThreadGroupSize));
// 128×128×64 voxels → 32×32×16 = 16,384 groups
```

One thread per voxel. 4×4×4 keeps 64 threads per group (two waves on most
hardware), and the group is a small cube, so neighbouring threads touch
neighbouring voxels in all three axes. That matters for the gradient pass,
which reads six neighbours. The group size lives in one C++ constant fed to the
HLSL through defines, so the two sides can't disagree.

Every kernel starts with `if (any(Voxel >= FieldResolution)) return;`, so
resolutions that aren't multiples of 4 are safe; they just leave a few threads
idle in the edge groups.

## Cost

At the default 128×128×64 = 1,048,576 voxels, a frame with one cutter does
about 1M box-SDF evaluations plus 1M six-tap gradients, which is a small
fraction of a millisecond on any GPU that runs UE5. And a frame where nothing
moved costs **nothing**, because the subsystem doesn't dispatch at all (see
[the idle skip](01-architecture.md#the-idle-skip)).
