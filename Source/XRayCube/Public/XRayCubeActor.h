// Copyright Matin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "XRayCubeActor.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;
class UXRayCutterComponent;

/**
 * The X-ray cube: drop it into a level, move/rotate/scale it, and every
 * X-ray material inside it is cut away so you can see through.
 *
 * Root is a UXRayCutterComponent (the actual cut). A child cube mesh with the
 * see-through "glass" material shows where the cut is; it is sized to the
 * cutter's box extent automatically.
 *
 * Optional: bFollowPlayerCamera turns it into x-ray goggles in play - it rides
 * in front of player 0's camera and cuts whatever you look at.
 */
UCLASS(meta = (DisplayName = "X-Ray Cube"))
class XRAYCUBE_API AXRayCubeActor : public AActor
{
	GENERATED_BODY()

public:
	AXRayCubeActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "X-Ray")
	TObjectPtr<UXRayCutterComponent> Cutter;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "X-Ray")
	TObjectPtr<UStaticMeshComponent> CubeMesh;

	/** Material for the visible cube. Defaults to the plugin's M_XRayCube glass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "X-Ray")
	TSoftObjectPtr<UMaterialInterface> CubeMaterial;

	/** Show the glass cube. The cut works either way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "X-Ray")
	bool bShowCube = true;

	/** In play, keep the cube in front of player 0's camera (x-ray goggles). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "X-Ray|Follow Camera")
	bool bFollowPlayerCamera = false;

	/** Distance in front of the camera to the cube's centre, world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "X-Ray|Follow Camera", meta = (EditCondition = "bFollowPlayerCamera"))
	float FollowDistance = 250.0f;

	/** Seconds-ish smoothing on the follow; 0 = locked to the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "X-Ray|Follow Camera", meta = (EditCondition = "bFollowPlayerCamera", ClampMin = "0.0"))
	float FollowSmoothing = 0.08f;

private:
	void SyncCubeMesh();
};
