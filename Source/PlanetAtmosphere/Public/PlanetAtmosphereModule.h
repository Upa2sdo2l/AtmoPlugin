// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "SceneViewExtension.h"

class FAtmosphereProxyRegistry;
class FPlanetAtmosphereViewExtension;
class UWorld;

class FPlanetAtmosphereModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void RegisterViewExtensionForWorld(UWorld* World, TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> Registry);
	void UnregisterViewExtensionForWorld(UWorld* World);

private:
	TSharedPtr<FSceneViewExtensionIsActiveFunctor> ViewExtensionFactory;
	TMap<UWorld*, TSharedPtr<FPlanetAtmosphereViewExtension, ESPMode::ThreadSafe>> WorldViewExtensions;
};
