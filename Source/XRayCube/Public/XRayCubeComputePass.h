// Copyright Matin. All Rights Reserved.
//
// X-ray field compute passes: cutter boxes -> 3D signed distance field.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRHICommandListImmediate;
class FRHITexture;

namespace XRayCube
{
	/**
	 * Compute thread group edge. Groups are 4x4x4 = 64 threads, one voxel each.
	 * Shared so the HLSL defines and the dispatch group count can never drift apart.
	 */
	static constexpr int32 ThreadGroupSize = 4;
}

/**
 * One cut-away box, in field-local space (relative to the field centre).
 * Layout must match FXRayCutterGPU in XRayCube.usf: four float4s, 64 bytes.
 *
 * The rotation is stored as three unit axes rather than a quaternion so the
 * shader's world->box transform is three dot products, with no per-voxel
 * quaternion maths.
 */
struct FXRayCutterGPU
{
	FVector3f	Center = FVector3f::ZeroVector;
	float		CornerRadius = 0.0f;
	FVector3f	AxisX = FVector3f(1.0f, 0.0f, 0.0f);
	float		HalfX = 100.0f;
	FVector3f	AxisY = FVector3f(0.0f, 1.0f, 0.0f);
	float		HalfY = 100.0f;
	FVector3f	AxisZ = FVector3f(0.0f, 0.0f, 1.0f);
	float		HalfZ = 100.0f;
};
static_assert(sizeof(FXRayCutterGPU) == 64, "FXRayCutterGPU must stay tightly packed to match the HLSL struct");

/** Everything the render thread needs for one field update. */
struct FXRayFieldDispatchParams
{
	FRHITexture*	PrevFieldTexture = nullptr;	// R32F, last frame's distance field
	FRHITexture*	NextFieldTexture = nullptr;	// R32F, this frame's distance field
	FRHITexture*	FieldDataTexture = nullptr;	// RGBA16F, what materials sample

	FIntVector		Resolution = FIntVector(128, 128, 64);
	FVector3f		FieldSize = FVector3f(1600.0f, 1600.0f, 800.0f);
	FIntVector		PrevVoxelOffset = FIntVector::ZeroValue;
	float			MaxDistance = 200.0f;
	float			BlendRadius = 0.0f;
	float			BlendAlpha = 1.0f;
	float			ClearMask = 1.0f;

	TArray<FXRayCutterGPU> Cutters;
};

// ---------------------------------------------------------------------------
// Pass 1: reproject last frame's field, rasterise cutters, ease towards them
// ---------------------------------------------------------------------------

BEGIN_SHADER_PARAMETER_STRUCT(FXRayFieldUpdateParams, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, PrevField)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutField)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FXRayCutterGPU>, Cutters)
	SHADER_PARAMETER(FIntVector, FieldResolution)
	SHADER_PARAMETER(FVector3f, FieldSize)
	SHADER_PARAMETER(FIntVector, PrevVoxelOffset)
	SHADER_PARAMETER(int32, NumCutters)
	SHADER_PARAMETER(float, MaxDistance)
	SHADER_PARAMETER(float, BlendRadius)
	SHADER_PARAMETER(float, BlendAlpha)
	SHADER_PARAMETER(float, ClearMask)
END_SHADER_PARAMETER_STRUCT()

class FXRayFieldUpdateCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FXRayFieldUpdateCS);
	SHADER_USE_PARAMETER_STRUCT(FXRayFieldUpdateCS, FGlobalShader);

	using FParameters = FXRayFieldUpdateParams;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		SET_SHADER_DEFINE(OutEnvironment, THREADS_X, XRayCube::ThreadGroupSize);
		SET_SHADER_DEFINE(OutEnvironment, THREADS_Y, XRayCube::ThreadGroupSize);
		SET_SHADER_DEFINE(OutEnvironment, THREADS_Z, XRayCube::ThreadGroupSize);
	}
};

// ---------------------------------------------------------------------------
// Pass 2: distance field -> packed distance + gradient texture for materials
// ---------------------------------------------------------------------------

BEGIN_SHADER_PARAMETER_STRUCT(FXRayFieldGradientParams, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, Field)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutFieldData)
	SHADER_PARAMETER(FIntVector, FieldResolution)
	SHADER_PARAMETER(FVector3f, FieldSize)
END_SHADER_PARAMETER_STRUCT()

class FXRayFieldGradientCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FXRayFieldGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FXRayFieldGradientCS, FGlobalShader);

	using FParameters = FXRayFieldGradientParams;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		SET_SHADER_DEFINE(OutEnvironment, THREADS_X, XRayCube::ThreadGroupSize);
		SET_SHADER_DEFINE(OutEnvironment, THREADS_Y, XRayCube::ThreadGroupSize);
		SET_SHADER_DEFINE(OutEnvironment, THREADS_Z, XRayCube::ThreadGroupSize);
	}
};

namespace XRayCube
{
	/** Builds and executes the render graph for one field update. Render thread only. */
	XRAYCUBE_API void Dispatch_RenderThread(FRHICommandListImmediate& RHICmdList, const FXRayFieldDispatchParams& Params);
}
