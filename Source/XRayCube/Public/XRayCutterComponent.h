// Copyright Matin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "XRayCutterComponent.generated.h"

/** A cutter's world-space shape, as the subsystem reads it each frame. */
struct FXRayCutterShape
{
	FVector	Center = FVector::ZeroVector;
	FQuat	Rotation = FQuat::Identity;
	FVector	HalfExtent = FVector(100.0);
	float	CornerRadius = 0.0f;
};

/**
 * A box that cuts away every X-ray material it overlaps.
 *
 * It *is* a UBoxComponent, so you get the familiar box extent property, the
 * editor wireframe and scale handles for free; scale, rotation and extent all
 * feed the cut. Collision is off by default - it is purely visual.
 *
 * Like USnowDeformerComponent it does not tick: it registers with the world's
 * UXRayCubeSubsystem and the subsystem pulls every cutter's shape once per
 * frame, so all cutters land in the same dispatch against the same field centre.
 * It registers in OnRegister rather than BeginPlay so it works in the editor
 * viewport, not only during play.
 */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent, DisplayName = "X-Ray Cutter"))
class XRAYCUBE_API UXRayCutterComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UXRayCutterComponent(const FObjectInitializer& ObjectInitializer);

	/** Turn the cut on/off without removing the component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "X-Ray")
	bool bCutterEnabled = true;

	/** Rounds the cut's edges and corners, world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "X-Ray", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "100.0"))
	float CornerRadius = 0.0f;

	/** False when disabled or degenerate; otherwise fills OutShape. */
	bool GetCutterShape(FXRayCutterShape& OutShape) const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};
