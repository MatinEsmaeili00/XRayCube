// Copyright Matin. All Rights Reserved.

#include "XRayCubeComputePass.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PooledRenderTarget.h"

IMPLEMENT_GLOBAL_SHADER(FXRayFieldUpdateCS,   "/XRayCubeShaders/Private/XRayCube.usf", "XRayFieldUpdateCS",   SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FXRayFieldGradientCS, "/XRayCubeShaders/Private/XRayCube.usf", "XRayFieldGradientCS", SF_Compute);

void XRayCube::Dispatch_RenderThread(FRHICommandListImmediate& RHICmdList, const FXRayFieldDispatchParams& Params)
{
	check(IsInRenderingThread());

	if (!Params.PrevFieldTexture || !Params.NextFieldTexture || !Params.FieldDataTexture)
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	const FRDGTextureRef PrevFieldRDG = RegisterExternalTexture(GraphBuilder, Params.PrevFieldTexture, TEXT("XRayPrevField"));
	const FRDGTextureRef NextFieldRDG = RegisterExternalTexture(GraphBuilder, Params.NextFieldTexture, TEXT("XRayNextField"));
	const FRDGTextureRef FieldDataRDG = RegisterExternalTexture(GraphBuilder, Params.FieldDataTexture, TEXT("XRayFieldData"));

	// Structured buffers can't be zero-sized. With no cutters we still upload
	// one entry (ignored, NumCutters = 0) so the field relaxes back to "uncut".
	const int32 NumCutters = Params.Cutters.Num();
	static const FXRayCutterGPU DummyCutter;
	const FRDGBufferRef CutterBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("XRayCutters"),
		sizeof(FXRayCutterGPU),
		FMath::Max(NumCutters, 1),
		NumCutters > 0 ? Params.Cutters.GetData() : &DummyCutter,
		sizeof(FXRayCutterGPU) * FMath::Max(NumCutters, 1));

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Params.Resolution, FIntVector(XRayCube::ThreadGroupSize));

	// --- Pass 1: reproject + rasterise + ease ------------------------------
	{
		FXRayFieldUpdateCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FXRayFieldUpdateCS::FParameters>();
		PassParameters->PrevField = PrevFieldRDG;
		PassParameters->OutField = GraphBuilder.CreateUAV(NextFieldRDG);
		PassParameters->Cutters = GraphBuilder.CreateSRV(CutterBuffer);
		PassParameters->FieldResolution = Params.Resolution;
		PassParameters->FieldSize = Params.FieldSize;
		PassParameters->PrevVoxelOffset = Params.PrevVoxelOffset;
		PassParameters->NumCutters = NumCutters;
		PassParameters->MaxDistance = Params.MaxDistance;
		PassParameters->BlendRadius = Params.BlendRadius;
		PassParameters->BlendAlpha = Params.BlendAlpha;
		PassParameters->ClearMask = Params.ClearMask;

		TShaderMapRef<FXRayFieldUpdateCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("XRayFieldUpdate(%dx%dx%d, %d cutters)", Params.Resolution.X, Params.Resolution.Y, Params.Resolution.Z, NumCutters),
			ComputeShader,
			PassParameters,
			GroupCount);
	}

	// --- Pass 2: distance -> distance + gradient ------------------------------
	// RDG sees Pass 1 write NextField and Pass 2 read it, and inserts the
	// UAV -> SRV barrier between them for us.
	{
		FXRayFieldGradientCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FXRayFieldGradientCS::FParameters>();
		PassParameters->Field = NextFieldRDG;
		PassParameters->OutFieldData = GraphBuilder.CreateUAV(FieldDataRDG);
		PassParameters->FieldResolution = Params.Resolution;
		PassParameters->FieldSize = Params.FieldSize;

		TShaderMapRef<FXRayFieldGradientCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("XRayFieldGradient(%dx%dx%d)", Params.Resolution.X, Params.Resolution.Y, Params.Resolution.Z),
			ComputeShader,
			PassParameters,
			GroupCount);
	}

	GraphBuilder.Execute();
}
