// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereModule.h"
#include "PlanetAtmosphereTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "AtmosphereNoiseTextures.h"

DEFINE_LOG_CATEGORY(LogPlanetAtmosphere);

void FPlanetAtmosphereModule::StartupModule()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PlanetAtmosphere"));

	if (Plugin.IsValid())
	{
		const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/PlanetAtmosphere"), PluginShaderDir);
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("Shader directory mapped: %s"), *PluginShaderDir);
	}

	UE_LOG(LogPlanetAtmosphere, Log, TEXT("Module started"));
}

void FPlanetAtmosphereModule::ShutdownModule()
{
	// Shared GPU noise textures: released on the rendering thread before RHIExit() (see AtmosphereNoiseTextures.h).
	PlanetAtmosphere::ReleaseNoiseTextures_GameThread();
}

IMPLEMENT_MODULE(FPlanetAtmosphereModule, PlanetAtmosphere)
