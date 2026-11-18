// Copyright Matin. All Rights Reserved.

#include "XRayCube.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FXRayCubeModule"

DEFINE_LOG_CATEGORY(LogXRayCube);

void FXRayCubeModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("XRayCube"));
	if (!Plugin.IsValid())
	{
		return;
	}

	const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));

	if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/XRayCubeShaders")))
	{
		AddShaderSourceDirectoryMapping(TEXT("/XRayCubeShaders"), PluginShaderDir);
	}
}

void FXRayCubeModule::ShutdownModule()
{
	// Shader directory mappings are process-global; leave them in place.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FXRayCubeModule, XRayCube)
