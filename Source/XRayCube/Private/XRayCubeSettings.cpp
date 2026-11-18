// Copyright Matin. All Rights Reserved.

#include "XRayCubeSettings.h"

#include "Engine/TextureRenderTargetVolume.h"
#include "Materials/MaterialParameterCollection.h"

UXRayCubeSettings::UXRayCubeSettings()
	// 128 x 128 x 64 over 16 x 16 x 8 m = 12.5 cm voxels: enough to cover a
	// whole apartment storey around the cube, 1M voxels, ~12 MB in total.
	: FieldResolution(128, 128, 64)
	, FieldSizeWorld(1600.0, 1600.0, 800.0)
	, MaxDistance(200.0f)
	, BlendRadius(0.0f)
	, Response(12.0f)
	, bRunInEditor(true)
	, FieldRenderTarget(FSoftObjectPath(TEXT("/XRayCube/Materials/RTV_XRayField.RTV_XRayField")))
	, ParameterCollection(FSoftObjectPath(TEXT("/XRayCube/Materials/MPC_XRayField.MPC_XRayField")))
	, CenterParameterName(TEXT("XRayFieldCenter"))
	, SizeParameterName(TEXT("XRayFieldSize"))
	, FieldTextureParameterName(TEXT("XRayField"))
{
}

FName UXRayCubeSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

const UXRayCubeSettings& UXRayCubeSettings::Get()
{
	return *GetDefault<UXRayCubeSettings>();
}
