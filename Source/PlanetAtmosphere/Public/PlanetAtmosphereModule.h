// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Plugin module: maps the shader directory.
 * View extensions are owned per world by UAtmosphereWorldSubsystem, not by the module.
 */
class FPlanetAtmosphereModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
