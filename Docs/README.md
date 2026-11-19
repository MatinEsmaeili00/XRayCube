# X-Ray Cube documentation

[← Project README](../README.md)

Read in order for the full story, or jump to what you need.

| # | Page | What's in it |
|---|---|---|
| 1 | [Architecture](01-architecture.md) | the problem, why a 3D distance field, why a scrolling voxel-snapped volume, the idle skip |
| 2 | [The GPU compute pipeline](02-gpu-compute-pipeline.md) | global shaders, RDG passes, UAVs on volume render targets, 3D thread groups, cost |
| 3 | [Shader walkthrough](03-shader-walkthrough.md) | oriented-box SDF, smooth union, reprojection, frame-rate-independent easing, gradient |
| 4 | [C++ reference](04-cpp-reference.md) | `AXRayCubeActor`, `UXRayCutterComponent`, `UXRayCubeSubsystem`, settings, render-thread API |
| 5 | [Material wiring](05-material-wiring.md) | the Custom node + include, LWC-safe subtraction, making your own materials cuttable, the glass |
| 6 | [The apartment and the Blender pipeline](06-apartment-blender.md) | the procedural apartment script, layout, units/axes, importing |
| 7 | [Tuning and troubleshooting](07-tuning-troubleshooting.md) | resolution vs voxel size, response, blend radius, every log warning explained |

## Relationship to GPULab and SnowDeformation

| Technique | GPULab / SnowDeformation | X-Ray Cube |
|---|---|---|
| Global compute shader | `FGlobalShader` + `SHADER_PARAMETER_STRUCT` | same, two kernels |
| Graph | RDG, `FComputeShaderUtils::AddPass` | same; RDG inserts the UAV→SRV barrier between the passes |
| Game → render thread | `ENQUEUE_RENDER_COMMAND`, params by value | same |
| Persistent GPU state | 2D ping-pong render targets | **3D** ping-pong volume render targets that follow the cutters, snapped to whole voxels so reprojection is an exact integer `Load` |
| What's stored | a 2D height field | a **signed distance** field (exact flat cuts, free rounding, smooth union, eased open/heal) |
| Consumer | materials sampling the render target | any material, via a Custom node `#include` |
