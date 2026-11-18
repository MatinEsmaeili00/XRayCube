// Copyright Matin. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

XRAYCUBE_API DECLARE_LOG_CATEGORY_EXTERN(LogXRayCube, Log, All);

/**
 * Module entry point. Maps the plugin's Shaders/ folder onto the virtual path
 * "/XRayCubeShaders" so that:
 *   - the compute shaders in XRayCube.usf can be found by IMPLEMENT_GLOBAL_SHADER, and
 *   - material Custom nodes can #include "/XRayCubeShaders/Private/XRayCubeMaterial.ush".
 *
 * The module loads at PostConfigInit precisely so this mapping exists before
 * the global shader map and any material referencing the include compile.
 */
class FXRayCubeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
