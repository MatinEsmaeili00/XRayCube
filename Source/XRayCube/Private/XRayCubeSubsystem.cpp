// Copyright Matin. All Rights Reserved.

#include "XRayCubeSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/World.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "TextureResource.h"
#include "XRayCube.h"
#include "XRayCubeSettings.h"
#include "XRayCutterComponent.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#endif

void UXRayCubeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UXRayCubeSettings& Settings = UXRayCubeSettings::Get();

	FieldResolution = Settings.FieldResolution;
	FieldSizeWorld  = Settings.FieldSizeWorld;
	MaxDistance     = Settings.MaxDistance;
	BlendRadius     = Settings.BlendRadius;
	Response        = Settings.Response;
	bRunInEditor    = Settings.bRunInEditor;

	CenterParameterName       = Settings.CenterParameterName;
	SizeParameterName         = Settings.SizeParameterName;
	FieldTextureParameterName = Settings.FieldTextureParameterName;

	// Soft references, resolved once here so a mis-configured asset warns once
	// rather than every tick.
	if (!Settings.FieldRenderTarget.IsNull())
	{
		ConfiguredOutputAsset = Settings.FieldRenderTarget.LoadSynchronous();
#if WITH_EDITOR
		if (!ConfiguredOutputAsset)
		{
			ConfiguredOutputAsset = CreateOutputAsset(Settings.FieldRenderTarget.ToSoftObjectPath());
		}
#endif
		if (ConfiguredOutputAsset)
		{
			PrepareOutputAsset(ConfiguredOutputAsset);
		}
		else
		{
			UE_LOG(LogXRayCube, Warning,
				TEXT("X-ray field render target %s could not be loaded; falling back to a transient target. ")
				TEXT("Materials that sample the asset directly will see no cut."),
				*Settings.FieldRenderTarget.ToString());
		}
	}

	if (!Settings.ParameterCollection.IsNull())
	{
		ParameterCollection = Settings.ParameterCollection.LoadSynchronous();
		if (ParameterCollection)
		{
			bCenterParameterValid = ParameterCollection->GetVectorParameterByName(CenterParameterName) != nullptr;
			bSizeParameterValid   = ParameterCollection->GetVectorParameterByName(SizeParameterName) != nullptr;
			if (!bCenterParameterValid || !bSizeParameterValid)
			{
				UE_LOG(LogXRayCube, Warning,
					TEXT("Material Parameter Collection %s needs vector parameters named %s and %s."),
					*ParameterCollection->GetName(), *CenterParameterName.ToString(), *SizeParameterName.ToString());
			}
		}
	}
}

void UXRayCubeSubsystem::Deinitialize()
{
	RegisteredCutters.Reset();
	LastCutters.Reset();
	ReleaseRenderTargets();
	ConfiguredOutputAsset = nullptr;
	ParameterCollection = nullptr;

	Super::Deinitialize();
}

TStatId UXRayCubeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UXRayCubeSubsystem, STATGROUP_Tickables);
}

bool UXRayCubeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Editor included on purpose: the whole point is being able to drag a cube
	// around a building in the viewport. Preview worlds (asset editors) are not.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
}

bool UXRayCubeSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	if (World && World->WorldType == EWorldType::Editor)
	{
		return bRunInEditor;
	}
	return true;
}

bool UXRayCubeSubsystem::IsSuspendedForPIE() const
{
#if WITH_EDITOR
	// The editor world and a PIE world would both write into the same render
	// target asset, and whichever ran last would win. Let PIE have it.
	const UWorld* World = GetWorld();
	if (World && World->WorldType == EWorldType::Editor && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World() != nullptr)
			{
				return true;
			}
		}
	}
#endif
	return false;
}

void UXRayCubeSubsystem::RegisterCutter(UXRayCutterComponent* Cutter)
{
	if (Cutter)
	{
		RegisteredCutters.AddUnique(Cutter);
	}
}

void UXRayCubeSubsystem::UnregisterCutter(UXRayCutterComponent* Cutter)
{
	RegisteredCutters.RemoveAll([Cutter](const TWeakObjectPtr<UXRayCutterComponent>& Weak)
	{
		return Weak.Get() == Cutter;
	});
}

// ---------------------------------------------------------------------------
// Material wiring
// ---------------------------------------------------------------------------

FLinearColor UXRayCubeSubsystem::GetFieldCenterParameter() const
{
	return FLinearColor(
		static_cast<float>(FieldCenterWorld.X),
		static_cast<float>(FieldCenterWorld.Y),
		static_cast<float>(FieldCenterWorld.Z),
		bFieldValid ? 1.0f : 0.0f);
}

FLinearColor UXRayCubeSubsystem::GetFieldSizeParameter() const
{
	return FLinearColor(
		static_cast<float>(FieldSizeWorld.X),
		static_cast<float>(FieldSizeWorld.Y),
		static_cast<float>(FieldSizeWorld.Z),
		MaxDistance);
}

void UXRayCubeSubsystem::ApplyFieldParametersToMaterial(UMaterialInstanceDynamic* Material) const
{
	if (!Material)
	{
		return;
	}
	if (FieldDataTarget)
	{
		Material->SetTextureParameterValue(FieldTextureParameterName, FieldDataTarget);
	}
	Material->SetVectorParameterValue(CenterParameterName, GetFieldCenterParameter());
	Material->SetVectorParameterValue(SizeParameterName, GetFieldSizeParameter());
}

void UXRayCubeSubsystem::PushMaterialParameters()
{
	if (!ParameterCollection)
	{
		return;
	}
	if (bCenterParameterValid)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(this, ParameterCollection, CenterParameterName, GetFieldCenterParameter());
	}
	if (bSizeParameterValid)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(this, ParameterCollection, SizeParameterName, GetFieldSizeParameter());
	}
}

// ---------------------------------------------------------------------------
// Render targets
// ---------------------------------------------------------------------------

void UXRayCubeSubsystem::PrepareOutputAsset(UTextureRenderTargetVolume* Asset)
{
	check(Asset);

	if (Asset->GetFormat() != PF_FloatRGBA)
	{
		UE_LOG(LogXRayCube, Warning,
			TEXT("X-ray field render target %s is not RGBA16f. The field stores signed distances, so a ")
			TEXT("fixed-point format clamps away the inside of every cut."),
			*Asset->GetName());
	}

	// bCanCreateUAV isn't serialised; a saved volume target keeps the flag in
	// bSupportsUAV, which CreateResource() copies across.
	if (!Asset->bSupportsUAV || !Asset->bCanCreateUAV)
	{
		UE_LOG(LogXRayCube, Warning,
			TEXT("X-ray field render target %s did not have Supports UAV set; enabling it for this session."),
			*Asset->GetName());
		Asset->bSupportsUAV = true;
		Asset->bCanCreateUAV = true;
		Asset->UpdateResource();
	}
}

#if WITH_EDITOR
UTextureRenderTargetVolume* UXRayCubeSubsystem::CreateOutputAsset(const FSoftObjectPath& Path) const
{
	// Only in an interactive editor: a cooked game or a commandlet must never
	// invent content.
	if (!GIsEditor || IsRunningCommandlet() || Path.IsNull())
	{
		return nullptr;
	}

	const FString PackageName = Path.GetLongPackageName();
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, FPackageName::GetAssetPackageExtension())
		|| FPackageName::DoesPackageExist(PackageName))
	{
		// Unmounted root, or the package exists on disk and simply failed to
		// load: leave it alone and let the caller warn.
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackageName);
	UTextureRenderTargetVolume* Asset = NewObject<UTextureRenderTargetVolume>(
		Package, FName(*Path.GetAssetName()), RF_Public | RF_Standalone | RF_Transactional);

	Asset->ClearColor     = FLinearColor(MaxDistance, 0.0f, 0.0f, 0.0f);
	Asset->OverrideFormat = PF_FloatRGBA;
	Asset->bHDR           = true;
	Asset->bSupportsUAV   = true;
	Asset->bCanCreateUAV  = true;
	Asset->Init(FieldResolution.X, FieldResolution.Y, FieldResolution.Z, PF_FloatRGBA);
	Asset->UpdateResourceImmediate(true);

	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();

	UE_LOG(LogXRayCube, Log,
		TEXT("Created X-ray field render target %s (%dx%dx%d RGBA16F). Save it so materials can reference it."),
		*PackageName, FieldResolution.X, FieldResolution.Y, FieldResolution.Z);
	return Asset;
}
#endif

void UXRayCubeSubsystem::ReleaseRenderTargets()
{
	FieldTargets[0] = nullptr;
	FieldTargets[1] = nullptr;
	FieldDataTarget = nullptr;
	bUsingExternalOutput = false;
	bTargetsInitialised = false;
	bFirstFrame = true;
	bFieldValid = false;
}

void UXRayCubeSubsystem::EnsureRenderTargets()
{
	// A user-supplied output asset decides the resolution: the ping-pong
	// volumes have to match it voxel for voxel.
	UTextureRenderTargetVolume* OutputAsset = ConfiguredOutputAsset;
	if (OutputAsset)
	{
		FieldResolution = FIntVector(OutputAsset->SizeX, OutputAsset->SizeY, OutputAsset->SizeZ);
	}

	// Any size works - the shader bounds-checks partial thread groups - but
	// multiples of ThreadGroupSize (4) keep every group full.
	FieldResolution = FieldResolution.ComponentMax(FIntVector(1));

	FieldSizeWorld = FieldSizeWorld.ComponentMax(FVector(1.0));
	if (!FieldSizeWorld.Equals(AllocatedFieldSize))
	{
		// Same voxel count over a different volume: every stored distance now
		// sits at the wrong place, so start over.
		AllocatedFieldSize = FieldSizeWorld;
		bFirstFrame = true;
	}

	const bool bMatchesRequest =
		bTargetsInitialised
		&& FieldTargets[0] && FieldTargets[1] && FieldDataTarget
		&& bUsingExternalOutput == (OutputAsset != nullptr)
		&& (!OutputAsset || FieldDataTarget == OutputAsset)
		&& FieldTargets[0]->SizeX == FieldResolution.X
		&& FieldTargets[0]->SizeY == FieldResolution.Y
		&& FieldTargets[0]->SizeZ == FieldResolution.Z;

	if (bMatchesRequest)
	{
		return;
	}

	auto MakeVolume = [this](EPixelFormat Format) -> UTextureRenderTargetVolume*
	{
		UTextureRenderTargetVolume* RT = NewObject<UTextureRenderTargetVolume>(this);
		RT->ClearColor = FLinearColor(MaxDistance, 0.0f, 0.0f, 0.0f);
		RT->bCanCreateUAV = true;
		RT->Init(FieldResolution.X, FieldResolution.Y, FieldResolution.Z, Format);
		RT->UpdateResourceImmediate(true);
		return RT;
	};

	// R32F for the persistent state: the ease is an exponential approach, and
	// in half precision small steps would round to zero and stall short of
	// the target.
	FieldTargets[0] = MakeVolume(PF_R32_FLOAT);
	FieldTargets[1] = MakeVolume(PF_R32_FLOAT);

	if (OutputAsset)
	{
		FieldDataTarget = OutputAsset;
		bUsingExternalOutput = true;
	}
	else
	{
		FieldDataTarget = MakeVolume(PF_FloatRGBA);
		bUsingExternalOutput = false;
	}

	CurrentFieldIndex = 0;
	bFirstFrame = true;
	bTargetsInitialised = true;

	const FVector VoxelSize = FieldSizeWorld / FVector(FieldResolution);
	UE_LOG(LogXRayCube, Log,
		TEXT("X-ray field allocated: %dx%dx%d voxels over %.0fx%.0fx%.0f uu (voxel %.1fx%.1fx%.1f), output: %s."),
		FieldResolution.X, FieldResolution.Y, FieldResolution.Z,
		FieldSizeWorld.X, FieldSizeWorld.Y, FieldSizeWorld.Z,
		VoxelSize.X, VoxelSize.Y, VoxelSize.Z,
		bUsingExternalOutput ? *FieldDataTarget->GetName() : TEXT("transient"));
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void UXRayCubeSubsystem::Tick(float DeltaTime)
{
	if (!IsInitialized())
	{
		return;
	}

	if (IsSuspendedForPIE())
	{
		bWasSuspended = true;
		return;
	}
	if (bWasSuspended)
	{
		// PIE has been writing our shared render target; rebuild from scratch.
		bWasSuspended = false;
		bFirstFrame = true;
	}

	EnsureRenderTargets();
	if (!bTargetsInitialised)
	{
		return;
	}

	// --- Gather cutters (pull model: one consistent snapshot per frame) ----
	TArray<FXRayCutterShape, TInlineAllocator<8>> Shapes;
	FBox CutterBounds(ForceInit);
	for (auto It = RegisteredCutters.CreateIterator(); It; ++It)
	{
		const UXRayCutterComponent* Cutter = It->Get();
		if (!Cutter)
		{
			It.RemoveCurrent();
			continue;
		}

		FXRayCutterShape Shape;
		if (Cutter->GetCutterShape(Shape))
		{
			CutterBounds += FBox(-Shape.HalfExtent, Shape.HalfExtent).TransformBy(FTransform(Shape.Rotation, Shape.Center));
			Shapes.Add(Shape);
		}
	}

	// --- Place the field: centred on the cutters, snapped to whole voxels --
	const FVector VoxelSize = FieldSizeWorld / FVector(FieldResolution);
	const FVector DesiredCenter = CutterBounds.IsValid ? CutterBounds.GetCenter() : FieldCenterWorld;
	const FIntVector NewCenterVoxel(
		FMath::RoundToInt32(DesiredCenter.X / VoxelSize.X),
		FMath::RoundToInt32(DesiredCenter.Y / VoxelSize.Y),
		FMath::RoundToInt32(DesiredCenter.Z / VoxelSize.Z));

	const FIntVector VoxelOffset = bFirstFrame ? FIntVector::ZeroValue : (NewCenterVoxel - CenterVoxel);
	const FVector NewCenterWorld = FVector(NewCenterVoxel) * VoxelSize;

	if (CutterBounds.IsValid && !bWarnedOutOfBounds)
	{
		// Only the cutters themselves have to fit (plus a voxel each side so
		// trilinear sampling sees "outside" past every face). Beyond the field
		// the material reads MaxDistance, i.e. "not cut", which is exactly
		// right everywhere except inside a cutter.
		const FVector Needed = CutterBounds.GetSize() + 2.0 * VoxelSize;
		if (Needed.X > FieldSizeWorld.X || Needed.Y > FieldSizeWorld.Y || Needed.Z > FieldSizeWorld.Z)
		{
			bWarnedOutOfBounds = true;
			UE_LOG(LogXRayCube, Warning,
				TEXT("X-ray cutters span %.0fx%.0fx%.0f uu but the field only covers %.0fx%.0fx%.0f; ")
				TEXT("cuts will stop at the field's edge. Raise FieldSizeWorld in Project Settings."),
				CutterBounds.GetSize().X, CutterBounds.GetSize().Y, CutterBounds.GetSize().Z,
				FieldSizeWorld.X, FieldSizeWorld.Y, FieldSizeWorld.Z);
		}
	}

	// --- Convert to field-local GPU structs ---------------------------------
	TArray<FXRayCutterGPU> GPUCutters;
	GPUCutters.Reserve(Shapes.Num());
	for (const FXRayCutterShape& Shape : Shapes)
	{
		FXRayCutterGPU& C = GPUCutters.AddDefaulted_GetRef();
		C.Center = FVector3f(Shape.Center - NewCenterWorld);
		C.CornerRadius = Shape.CornerRadius;
		C.AxisX = FVector3f(Shape.Rotation.GetAxisX());
		C.AxisY = FVector3f(Shape.Rotation.GetAxisY());
		C.AxisZ = FVector3f(Shape.Rotation.GetAxisZ());
		C.HalfX = static_cast<float>(Shape.HalfExtent.X);
		C.HalfY = static_cast<float>(Shape.HalfExtent.Y);
		C.HalfZ = static_cast<float>(Shape.HalfExtent.Z);
	}

	// --- Decide whether there is any work -------------------------------------
	// Exact comparison is intended: a cutter that hasn't moved produces
	// bit-identical floats, and any real change must trigger an update.
	const bool bCuttersChanged =
		GPUCutters.Num() != LastCutters.Num()
		|| (GPUCutters.Num() > 0 && FMemory::Memcmp(GPUCutters.GetData(), LastCutters.GetData(), GPUCutters.Num() * sizeof(FXRayCutterGPU)) != 0);

	const bool bChanged =
		bFirstFrame
		|| bCuttersChanged
		|| VoxelOffset != FIntVector::ZeroValue
		|| MaxDistance != LastMaxDistance
		|| BlendRadius != LastBlendRadius;

	if (bChanged)
	{
		// exp(-7) < 0.001: after 7 / Response seconds the ease is visually done.
		SettleTimeRemaining = Response > KINDA_SMALL_NUMBER ? (7.0f / Response) : 0.0f;
	}
	else
	{
		SettleTimeRemaining = FMath::Max(SettleTimeRemaining - DeltaTime, 0.0f);
	}

	if (!bChanged && SettleTimeRemaining <= 0.0f)
	{
		// Idle: nothing moved and the field has caught up. Zero GPU cost.
		PushMaterialParameters();
		return;
	}

	FTextureRenderTargetResource* PrevResource = FieldTargets[CurrentFieldIndex]->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* NextResource = FieldTargets[1 - CurrentFieldIndex]->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* DataResource = FieldDataTarget->GameThread_GetRenderTargetResource();
	if (!PrevResource || !NextResource || !DataResource)
	{
		// Resources are created on the render thread; try again next tick.
		// Checked before any state advances so no change is lost.
		return;
	}

	FXRayFieldDispatchParams Params;
	Params.Resolution = FieldResolution;
	Params.FieldSize = FVector3f(FieldSizeWorld);
	Params.PrevVoxelOffset = VoxelOffset;
	Params.MaxDistance = MaxDistance;
	Params.BlendRadius = BlendRadius;
	Params.BlendAlpha = Response > KINDA_SMALL_NUMBER ? (1.0f - FMath::Exp(-Response * DeltaTime)) : 1.0f;
	Params.ClearMask = bFirstFrame ? 0.0f : 1.0f;
	Params.Cutters = GPUCutters;

	// RHI textures are resolved on the render thread: a resource can swap its
	// texture (resize, device reset) between now and when the command runs.
	ENQUEUE_RENDER_COMMAND(XRayFieldDispatch)(
		[Params, PrevResource, NextResource, DataResource](FRHICommandListImmediate& RHICmdList) mutable
		{
			Params.PrevFieldTexture = PrevResource->GetRenderTargetTexture();
			Params.NextFieldTexture = NextResource->GetRenderTargetTexture();
			Params.FieldDataTexture = DataResource->GetRenderTargetTexture();

			XRayCube::Dispatch_RenderThread(RHICmdList, Params);
		});

	LastCutters = MoveTemp(GPUCutters);
	LastMaxDistance = MaxDistance;
	LastBlendRadius = BlendRadius;
	CenterVoxel = NewCenterVoxel;
	FieldCenterWorld = NewCenterWorld;
	CurrentFieldIndex = 1 - CurrentFieldIndex;
	bFirstFrame = false;
	bFieldValid = true;

	PushMaterialParameters();
}
