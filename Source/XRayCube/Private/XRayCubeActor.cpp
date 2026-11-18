// Copyright Matin. All Rights Reserved.

#include "XRayCubeActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "XRayCutterComponent.h"

namespace
{
	/** /Engine/BasicShapes/Cube is 100 uu across. */
	constexpr double EngineCubeHalfSize = 50.0;
}

AXRayCubeActor::AXRayCubeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Cutter = CreateDefaultSubobject<UXRayCutterComponent>(TEXT("Cutter"));
	Cutter->SetBoxExtent(FVector(150.0, 150.0, 150.0));
	SetRootComponent(Cutter);

	CubeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CubeMesh"));
	CubeMesh->SetupAttachment(Cutter);
	CubeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CubeMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	CubeMesh->SetGenerateOverlapEvents(false);
	CubeMesh->SetCanEverAffectNavigation(false);
	CubeMesh->CastShadow = false;
	CubeMesh->bAffectDistanceFieldLighting = false;
	CubeMesh->bAffectDynamicIndirectLighting = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeAsset.Succeeded())
	{
		CubeMesh->SetStaticMesh(CubeAsset.Object);
	}

	// Soft path rather than a ConstructorHelpers lookup, so a project that
	// deletes or moves the plugin content gets a warning-free plain cube.
	CubeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/XRayCube/Materials/M_XRayCube.M_XRayCube")));
}

void AXRayCubeActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SyncCubeMesh();
}

void AXRayCubeActor::SyncCubeMesh()
{
	if (!CubeMesh || !Cutter)
	{
		return;
	}

	// The mesh is a child of the cutter, so the actor's scale already applies
	// to both; only the unscaled box extent needs translating to mesh scale.
	CubeMesh->SetRelativeScale3D(Cutter->GetUnscaledBoxExtent() / EngineCubeHalfSize);
	CubeMesh->SetVisibility(bShowCube);

	if (UMaterialInterface* Material = CubeMaterial.LoadSynchronous())
	{
		CubeMesh->SetMaterial(0, Material);
	}
}

void AXRayCubeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bFollowPlayerCamera)
	{
		return;
	}

	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	FVector CamLocation;
	FRotator CamRotation;
	PC->GetPlayerViewPoint(CamLocation, CamRotation);

	// Yaw only: a cube that pitches with the camera would slice floors and
	// ceilings at an angle, which reads as a glitch rather than a cut-away.
	const FRotator Yaw(0.0, CamRotation.Yaw, 0.0);
	const FVector Target = CamLocation + CamRotation.Vector() * FollowDistance;

	const float Alpha = FollowSmoothing > KINDA_SMALL_NUMBER
		? 1.0f - FMath::Exp(-DeltaSeconds / FollowSmoothing)
		: 1.0f;

	SetActorLocationAndRotation(
		FMath::Lerp(GetActorLocation(), Target, Alpha),
		FQuat::Slerp(GetActorQuat(), Yaw.Quaternion(), Alpha));
}
