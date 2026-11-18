// Copyright Matin. All Rights Reserved.

#include "XRayCutterComponent.h"

#include "Engine/World.h"
#include "XRayCubeSubsystem.h"

UXRayCutterComponent::UXRayCutterComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	InitBoxExtent(FVector(100.0));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);

	ShapeColor = FColor(0, 220, 255);
	LineThickness = 1.0f;
	bHiddenInGame = true;
}

void UXRayCutterComponent::OnRegister()
{
	Super::OnRegister();

	if (UWorld* World = GetWorld())
	{
		if (UXRayCubeSubsystem* Subsystem = World->GetSubsystem<UXRayCubeSubsystem>())
		{
			Subsystem->RegisterCutter(this);
		}
	}
}

void UXRayCutterComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UXRayCubeSubsystem* Subsystem = World->GetSubsystem<UXRayCubeSubsystem>())
		{
			Subsystem->UnregisterCutter(this);
		}
	}

	Super::OnUnregister();
}

bool UXRayCutterComponent::GetCutterShape(FXRayCutterShape& OutShape) const
{
	if (!bCutterEnabled || !IsRegistered())
	{
		return false;
	}

	const FVector HalfExtent = GetScaledBoxExtent().GetAbs();
	if (HalfExtent.GetMin() <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FTransform& Xf = GetComponentTransform();
	OutShape.Center = Xf.GetLocation();
	OutShape.Rotation = Xf.GetRotation();
	OutShape.HalfExtent = HalfExtent;
	OutShape.CornerRadius = FMath::Clamp(CornerRadius, 0.0f, static_cast<float>(HalfExtent.GetMin()));
	return true;
}
