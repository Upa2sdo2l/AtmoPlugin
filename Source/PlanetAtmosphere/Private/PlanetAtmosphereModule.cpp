// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereModule.h"
#include "PlanetAtmosphereViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FPlanetAtmosphereModule"

void FPlanetAtmosphereModule::StartupModule()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PlanetAtmosphere"));

	if (Plugin.IsValid())
	{
		FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/PlanetAtmosphere"), PluginShaderDir);
		UE_LOG(LogTemp, Log, TEXT("PlanetAtmosphere: Shader directory mapped"));
	}

	ViewExtensionFactory = MakeShared<FSceneViewExtensionIsActiveFunctor>();
	ViewExtensionFactory->IsActiveFunction = [](const ISceneViewExtension*, const FSceneViewExtensionContext&)
	{
		return TOptional<bool>(true);
	};

	UE_LOG(LogTemp, Log, TEXT("PlanetAtmosphere: Module started"));
}

void FPlanetAtmosphereModule::ShutdownModule()
{
	WorldViewExtensions.Empty();
	ViewExtensionFactory.Reset();
}

void FPlanetAtmosphereModule::RegisterViewExtensionForWorld(UWorld* World, TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> Registry)
{
	if (!World || !Registry.IsValid())
	{
		return;
	}

	TSharedPtr<FPlanetAtmosphereViewExtension, ESPMode::ThreadSafe> ViewExtension =
		FSceneViewExtensions::NewExtension<FPlanetAtmosphereViewExtension>(Registry);

	WorldViewExtensions.Add(World, ViewExtension);
	UE_LOG(LogTemp, Log, TEXT("PlanetAtmosphere: ViewExtension registered for world"));
}

void FPlanetAtmosphereModule::UnregisterViewExtensionForWorld(UWorld* World)
{
	if (World && WorldViewExtensions.Contains(World))
	{
		WorldViewExtensions.Remove(World);
		UE_LOG(LogTemp, Log, TEXT("PlanetAtmosphere: ViewExtension unregistered"));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPlanetAtmosphereModule, PlanetAtmosphere)
