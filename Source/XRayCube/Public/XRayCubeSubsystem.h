// Copyright Matin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "XRayCubeComputePass.h"
#include "XRayCubeSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UTextureRenderTargetVolume;
class UXRayCutterComponent;

/**
 * Owns a world's X-ray distance field: a 3D volume, centred on the active
 * cutters, holding the signed distance to the union of every
 * UXRayCutterComponent's box. Materials sample it to clip themselves away
 * inside the cubes.
 *
 * Same overall shape as USnowDeformationSubsystem - a game-thread tick that
 * gathers inputs, converts them to a GPU struct array, and enqueues an RDG
 * dispatch with ping-ponged persistent state - taken from 2D to 3D:
 *
 *   - the field scrolls with the cutters in whole-voxel steps (exact
 *     reprojection, never blurs),
 *   - the stored field eases towards the cutters' exact SDF (cuts open and
 *     heal smoothly instead of popping),
 *   - nothing is dispatched once the field has settled and nothing moved,
 *   - it also runs in editor worlds, so dragging a cube in the viewport
 *     updates the cut live. While PIE is running the editor world's field
 *     steps aside, since both worlds would write the same render target asset.
 */
UCLASS()
class XRAYCUBE_API UXRayCubeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem / FTickableGameObject
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Called by UXRayCutterComponent::OnRegister - no need to call directly. */
	void RegisterCutter(UXRayCutterComponent* Cutter);
	void UnregisterCutter(UXRayCutterComponent* Cutter);

	// --- Material wiring --------------------------------------------------

	/** RGBA16F volume: R = signed distance to the cut (world units), GBA = direction out of the cut. */
	UFUNCTION(BlueprintPure, Category = "X-Ray Cube|Material")
	UTextureRenderTargetVolume* GetFieldRenderTarget() const { return FieldDataTarget; }

	/** RGB = field centre (world), A = 1 once the field holds valid data. */
	UFUNCTION(BlueprintPure, Category = "X-Ray Cube|Material")
	FLinearColor GetFieldCenterParameter() const;

	/** RGB = field size (world), A = MaxDistance. */
	UFUNCTION(BlueprintPure, Category = "X-Ray Cube|Material")
	FLinearColor GetFieldSizeParameter() const;

	/** Pushes the field texture + both vectors onto a dynamic material instance once. */
	UFUNCTION(BlueprintCallable, Category = "X-Ray Cube|Material")
	void ApplyFieldParametersToMaterial(UMaterialInstanceDynamic* Material) const;

	/** Number of enabled cutters that went into the last field update. */
	UFUNCTION(BlueprintPure, Category = "X-Ray Cube")
	int32 GetNumActiveCutters() const { return LastCutters.Num(); }

	// --- Runtime overrides (seeded from UXRayCubeSettings) ----------------

	/** Voxel count. Changing it at runtime reallocates the field (ignored when a render target asset is assigned). */
	UPROPERTY(BlueprintReadWrite, Category = "X-Ray Cube|Settings")
	FIntVector FieldResolution = FIntVector(128, 128, 64);

	/** World size of the simulated volume. Changing it at runtime restarts the field. */
	UPROPERTY(BlueprintReadWrite, Category = "X-Ray Cube|Settings")
	FVector FieldSizeWorld = FVector(1600.0, 1600.0, 800.0);

	UPROPERTY(BlueprintReadWrite, Category = "X-Ray Cube|Settings")
	float MaxDistance = 200.0f;

	UPROPERTY(BlueprintReadWrite, Category = "X-Ray Cube|Settings")
	float BlendRadius = 0.0f;

	/** Per-second convergence rate of the stored field towards the cutters. 0 = instant. */
	UPROPERTY(BlueprintReadWrite, Category = "X-Ray Cube|Settings")
	float Response = 12.0f;

private:
	void EnsureRenderTargets();
	void ReleaseRenderTargets();
	void PrepareOutputAsset(UTextureRenderTargetVolume* Asset);
	void PushMaterialParameters();
	bool IsSuspendedForPIE() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> FieldTargets[2] = { nullptr, nullptr };

	/** Either the asset from the settings, or a transient one we made ourselves. */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> FieldDataTarget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> ConfiguredOutputAsset = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ParameterCollection = nullptr;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UXRayCutterComponent>> RegisteredCutters;

	FName CenterParameterName;
	FName SizeParameterName;
	FName FieldTextureParameterName;
	bool bCenterParameterValid = false;
	bool bSizeParameterValid = false;
	bool bRunInEditor = true;
	bool bUsingExternalOutput = false;

	/** Field centre, in whole voxels from the world origin. Snapping is what makes reprojection exact. */
	FIntVector CenterVoxel = FIntVector::ZeroValue;
	FVector FieldCenterWorld = FVector::ZeroVector;
	FVector AllocatedFieldSize = FVector::ZeroVector;

	/** Last cutter set dispatched, in field-local space - compared each tick to detect change. */
	TArray<FXRayCutterGPU> LastCutters;
	float LastMaxDistance = -1.0f;
	float LastBlendRadius = -1.0f;

	/** Seconds the field still needs to finish easing after the last change. */
	float SettleTimeRemaining = 0.0f;

	int32 CurrentFieldIndex = 0;
	bool bTargetsInitialised = false;
	bool bFirstFrame = true;
	bool bFieldValid = false;
	bool bWasSuspended = false;
	bool bWarnedOutOfBounds = false;
};
