# 3. Shader walkthrough

[← GPU compute pipeline](02-gpu-compute-pipeline.md) · Next: [C++ reference →](04-cpp-reference.md)

`Shaders/Private/XRayCube.usf`, kernel by kernel, with the maths derived.

## Voxel → world

```hlsl
const float3 UVW      = (float3(Voxel) + 0.5f) / float3(FieldResolution);
const float3 LocalPos = (UVW - 0.5f) * FieldSize;
```

`+0.5` puts the sample at the voxel **centre**. `UVW` is in 0..1. Subtracting
0.5 and scaling by the field size gives the position relative to the field
centre, in world units. Materials invert exactly this mapping
(`UVW = LocalPos / FieldSize + 0.5`), so writer and reader always agree on
where a voxel is.

## The oriented box SDF

```hlsl
float SdOrientedBox(float3 P, FXRayCutterGPU C)
{
    const float3 Rel = P - C.Center;
    const float3 Q = float3(dot(Rel, C.AxisX), dot(Rel, C.AxisY), dot(Rel, C.AxisZ));
    const float3 HalfSize = float3(C.HalfX, C.HalfY, C.HalfZ) - C.CornerRadius;
    const float3 A = abs(Q) - HalfSize;
    return length(max(A, 0.0f)) + min(max(A.x, max(A.y, A.z)), 0.0f) - C.CornerRadius;
}
```

**Into box space.** `Rel` is the point relative to the box centre. Dotting it
with the box's three unit axes projects it onto them, which is the same as
multiplying by the inverse rotation (for a rotation matrix, the inverse is the
transpose, and its rows are the axes). Now the box is axis-aligned at the origin.

**Fold into one octant.** A box is symmetric, so `abs(Q)` maps every point into
the positive octant. `A = abs(Q) - HalfSize` is then, per axis, how far past
the face the point is (positive) or how far inside it is (negative).

**Outside.** If any component of `A` is positive the point is outside.
`max(A, 0)` keeps only the axes where it's past the face, and `length()` of that
is the Euclidean distance to the nearest point on the box. That handles faces
(one positive axis), edges (two) and corners (three) in one expression.

**Inside.** All components are negative. The nearest face is the one you're
closest to, which is the *largest* (least negative) component:
`max(A.x, max(A.y, A.z))`. `min(..., 0)` zeroes this term when outside, so the
two halves add without interfering.

**Rounding.** Shrinking the box by `r` and then subtracting `r` from the
distance inflates it back to its original size, but with every edge and corner
rounded to radius `r`. It's a free extra from distance fields.

## Combining cutters: smooth union

```hlsl
float SmoothMin(float A, float B, float K)
{
    if (K <= 0.0f) return min(A, B);
    const float H = saturate(0.5f + 0.5f * (B - A) / K);
    return lerp(B, A, H) - K * H * (1.0f - H);
}
```

The union of two shapes is `min(dA, dB)`: a point is inside the union if it's
inside either. A hard `min` makes a sharp crease where two cubes overlap.
The polynomial smooth-min blends the two over a band of width `K`: `H` goes
from 0 to 1 across that band, `lerp` picks between the two distances, and the
`-K·H·(1-H)` term (largest at `H = 0.5`) pulls the surface outwards, filling
the crease with a rounded fillet. With `BlendRadius = 0` (the default) it's a
plain `min`.

The loop starts from `Target = MaxDistance`, meaning "nowhere near a cutter",
and folds every cutter in. The result is clamped to `±MaxDistance` so far-away
voxels hold a tidy constant instead of large numbers.

## Reprojection: keeping the field anchored to the world

```hlsl
float Prev = MaxDistance;
const int3 PrevVoxel = Voxel + PrevVoxelOffset;
if (ClearMask > 0.0f && all(PrevVoxel >= 0) && all(PrevVoxel < FieldResolution))
{
    Prev = PrevField.Load(int4(PrevVoxel, 0));
}
```

When the field centre moves by `Δ` world units, the voxel that now holds world
point `W` used to be at index `Voxel + Δ / VoxelSize`. The C++ side only ever
moves the centre in **whole voxels**, so that offset is an integer
(`PrevVoxelOffset`) and the lookup is an exact `Load`: no filtering, no blur,
however long the cube is dragged. Voxels that come into view from outside last
frame's volume start as "nothing here" (`MaxDistance`).

`ClearMask = 0` on the very first dispatch skips the read entirely. Last
frame's volume holds whatever the allocator left there.

## Easing: cuts that open and heal

```hlsl
OutField[Voxel] = lerp(Prev, Target, BlendAlpha);       // BlendAlpha = 1 - exp(-Response · dt)
```

Each frame the stored field moves a fraction of the way towards the exact
target. Choosing `α = 1 − e^(−k·dt)` makes this frame-rate independent:
after time `t` the remaining error is `e^(−k·t)` whether that took 10 frames or
1000. With the default `Response = 12` the cut is 95 % there after a quarter of
a second.

Easing a **distance field** gives a nice result for free. The zero crossing of
`lerp(dPrev, dTarget, α)` sweeps continuously from one surface to the other:

- **A cube arrives:** `dPrev = +MaxDistance` everywhere, and the zero crossing
  appears first where `dTarget` is most negative (the cube's middle) and grows
  outwards to the faces.
- **A cube leaves:** the hole shrinks closed from its faces inwards.
- **A cube moves:** the cut slides after it with a short, soft trail.

`Response = 0` turns easing off (`α = 1`): the field snaps to the cutters, and
the subsystem dispatches only on frames where something changed.

## The gradient pass

```hlsl
const float3 Gradient = float3(DX1 - DX0, DY1 - DY0, DZ1 - DZ0) / (2.0f * VoxelSize);
const float3 Normal   = GradLen > 1e-5f ? Gradient / GradLen : float3(0, 0, 1);
OutFieldData[Voxel]   = float4(D, Normal);
```

Central differences on the six neighbours (clamped at the volume border). The
gradient of a distance field points directly away from the nearest surface,
so its normalised direction is the **normal of the cut face** at that point.
It's divided by `VoxelSize` per axis because voxels need not be cubes (the
default is 12.5 cm in every axis, but a 128×128×32 field over the same
volume would have 25 cm in Z).

It's packed next to the distance into one RGBA16F texel, so a material that
wants both pays for one fetch. The current materials use `R` (clip and glow);
`GBA` is there for section caps (shading the inside of a cut wall as a solid
face), which is on the [TODO](../TODO.md).

## Material side

`Shaders/Private/XRayCubeMaterial.ush` is the reader:

```hlsl
float3 UVW = LocalPos / FieldSize.xyz + 0.5f;          // inverse of the voxel → world mapping
float  D   = inside && active && cuttable ? Field.SampleLevel(FieldSampler, UVW, 0).r : MaxDistance;
float  Mask = D > 0 ? 1 : 0;                            // OpacityMask
float  Glow = Mask * pow2(1 - saturate(D / GlowWidth)); // Emissive, peaks at the cut face
```

`SampleLevel(…, 0)` because the volume has one mip and we never want a blurrier
one. The default sampler is trilinear, which is what reconstructs the flat
faces exactly (see [why a distance field](01-architecture.md#why-a-distance-field-and-not-a-01-mask)).

`XRayCube_BoxEdge` draws the glass cube's outline: on each face one of
`|p|/half` is 1, and on an edge two are, so the *middle* of the three
per-axis distances-to-face is 0 exactly on an edge. `fwidth()` of that value
gives its per-pixel rate of change, so `smoothstep(0, fwidth·N, mid)` is a line
`N` pixels wide at any distance or actor scale.
