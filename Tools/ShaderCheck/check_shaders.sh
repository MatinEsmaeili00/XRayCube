#!/usr/bin/env bash
# Compiles every shader entry point in the plugin with the engine's DXC
# (Shader Model 6), without launching Unreal. Run it before restarting the
# editor after a shader change: a global shader that fails to compile stops
# the editor from starting.
#
#   Tools/ShaderCheck/check_shaders.sh
#
# The engine's Common.ush is stripped, since it needs the full UE shader
# environment and XRayCube.usf uses only HLSL intrinsics. The material
# helpers are compiled through a small pixel-shader wrapper that calls them the
# way a Custom node does.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SHADERS="$HERE/../../Shaders/Private"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

grep -v '#include "/Engine/Private/Common.ush"' "$SHADERS/XRayCube.usf" > "$WORK/cs.hlsl"

{
  grep -v '#pragma once' "$SHADERS/XRayCubeMaterial.ush"
  cat <<'EOF'
Texture3D XRayField; SamplerState XRayFieldSampler;
float4 XRayFieldCenter; float4 XRayFieldSize;
float4 MainPS(float3 WorldPos : TEXCOORD0, float3 LocalPos : TEXCOORD1) : SV_Target0
{
    float4 R = XRayCube_Evaluate(XRayField, XRayFieldSampler, WorldPos - XRayFieldCenter.xyz,
                                 XRayFieldCenter, XRayFieldSize, 1.0f, 4.0f);
    clip(R.x - 0.5f);
    return float4(R.y + XRayCube_BoxEdge(LocalPos, 50.0f, 1.5f), R.zw, 1);
}
EOF
} > "$WORK/ps.hlsl"

fail=0
run() {
  if ! python3 -I "$HERE/dxc_check.py" "$@" > "$WORK/out.txt" 2>&1; then fail=1; fi
  grep -av "DXIL signing library" "$WORK/out.txt" | sed '/^[[:space:]]*$/d'
}

run "$WORK/cs.hlsl" XRayFieldUpdateCS   cs_6_0 -DTHREADS_X=4 -DTHREADS_Y=4 -DTHREADS_Z=4
run "$WORK/cs.hlsl" XRayFieldGradientCS cs_6_0 -DTHREADS_X=4 -DTHREADS_Y=4 -DTHREADS_Z=4
run "$WORK/ps.hlsl" MainPS              ps_6_0

exit $fail
