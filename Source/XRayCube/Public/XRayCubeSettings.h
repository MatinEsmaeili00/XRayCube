// Copyright Matin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "XRayCubeSettings.generated.h"

class UMaterialParameterCollection;
class UTextureRenderTargetVolume;

/**
 * Project-wide X-ray field settings, under Project Settings > Plugins > X-Ray Cube.
 *
 * A world subsystem has no details panel of its own, so this is where the
 * field is tuned. UXRayCubeSubsystem copies these on Initialize; the copies
 * stay writable at runtime from Blueprint.
 *
 * The two Material Wiring assets default to the ones shipped in the plugin's
 * Content folder, so the plugin works out of the box.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "X-Ray Cube"))
class XRAYCUBE_API UXRayCubeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UXRayCubeSettings();

	virtual FName GetCategoryName() const override;

	static const UXRayCubeSettings& Get();

	// --- Field --------------------------------------------------------------

	/** Voxel count of the distance field. Ignored when FieldRenderTarget is set - that asset's size wins. */
	UPROPERTY(config, EditAnywhere, Category = "Field")
	FIntVector FieldResolution;

	/** World-space size of the volume the field covers, centred on the active cutters. Size / Resolution = voxel size. */
	UPROPERTY(config, EditAnywhere, Category = "Field")
	FVector FieldSizeWorld;

	/** Distances are clamped to +-this many world units. Must exceed the material's glow width. */
	UPROPERTY(config, EditAnywhere, Category = "Field", meta = (ClampMin = "1.0"))
	float MaxDistance;

	/** Smooth-union radius (world units) where cutters overlap. 0 = hard union. */
	UPROPERTY(config, EditAnywhere, Category = "Field", meta = (ClampMin = "0.0"))
	float BlendRadius;

	/**
	 * How fast the stored field chases the cutters, per second. Higher = snappier.
	 * Cuts open and heal over roughly 3 / Response seconds. 0 = instant.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Field", meta = (ClampMin = "0.0"))
	float Response;

	/** Run the field in editor worlds too, so moving a cube in the viewport updates the cut live. */
	UPROPERTY(config, EditAnywhere, Category = "Field")
	bool bRunInEditor;

	// --- Material wiring ----------------------------------------------------

	/**
	 * Volume render target the field is written into, and that materials
	 * sample with a Texture Object node. Must be RGBA16f (signed distances)
	 * with Can Create UAV ticked; the subsystem fixes up the UAV flag at
	 * runtime if you forget, and warns about the format.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Material Wiring", meta = (AllowedClasses = "/Script/Engine.TextureRenderTargetVolume"))
	TSoftObjectPtr<UTextureRenderTargetVolume> FieldRenderTarget;

	/** Receives the two vectors below every frame, so materials can place the field in the world. */
	UPROPERTY(config, EditAnywhere, Category = "Material Wiring", meta = (AllowedClasses = "/Script/Engine.MaterialParameterCollection"))
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	/** Vector parameter: RGB = field centre (world), A = 1 once the field is valid. */
	UPROPERTY(config, EditAnywhere, Category = "Material Wiring")
	FName CenterParameterName;

	/** Vector parameter: RGB = field size (world), A = MaxDistance. */
	UPROPERTY(config, EditAnywhere, Category = "Material Wiring")
	FName SizeParameterName;

	/** Texture parameter name used by ApplyFieldParametersToMaterial. */
	UPROPERTY(config, EditAnywhere, Category = "Material Wiring")
	FName FieldTextureParameterName;
};
