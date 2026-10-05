// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SharedPointer.h"
#include "AtmosphereWorldSubsystem.generated.h"

class APlanetAtmosphereActor;
class FAtmosphereProxyRegistry;

/**
 * World Subsystem that manages all planet atmospheres in the level.
 *
 * Game Thread only. The render side never touches this UObject:
 * it works exclusively with FAtmosphereProxyRegistry (plain C++, shared-ptr owned).
 */
UCLASS()
class PLANETATMOSPHERE_API UAtmosphereWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	/** Register an atmosphere actor - Game Thread */
	void RegisterAtmosphere(APlanetAtmosphereActor* Actor);

	/** Unregister an atmosphere actor - Game Thread */
	void UnregisterAtmosphere(APlanetAtmosphereActor* Actor);

	/** Get all registered atmosphere actors - Game Thread */
	const TArray<TWeakObjectPtr<APlanetAtmosphereActor>>& GetRegisteredAtmospheres() const
	{
		return RegisteredAtmospheres;
	}

	/** Shared render-side registry. Copied into each scene proxy on construction (Game Thread). */
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> GetProxyRegistry() const
	{
		return ProxyRegistry;
	}

private:
	/** All registered atmosphere actors - Game Thread ONLY */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<APlanetAtmosphereActor>> RegisteredAtmospheres;

	/** Render-side proxy registry (not a UObject, thread-safe ref-counted) */
	TSharedPtr<FAtmosphereProxyRegistry, ESPMode::ThreadSafe> ProxyRegistry;
};
