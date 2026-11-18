// Copyright Matin. All Rights Reserved.

using UnrealBuildTool;

public class XRayCube : ModuleRules
{
	public XRayCube(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				// UXRayCubeSettings derives from UDeveloperSettings, so anything
				// including our public headers needs this too.
				"DeveloperSettings",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",   // IPluginManager, to find our Shaders/ folder
				"RHI",
				"RenderCore", // FGlobalShader, RDG, FComputeShaderUtils
			}
		);
	}
}
