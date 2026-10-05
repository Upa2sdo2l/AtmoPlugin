// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetAtmosphereActor.generated.h"

class UPlanetAtmosphereComponent;

/**
 * Actor that represents a planet with volumetric atmosphere.
 * This is a thin wrapper around UPlanetAtmosphereComponent.
 * Registration in UAtmosphereWorldSubsystem is done by the component (OnRegister/OnUnregister),
 * so it works in Editor as well as in Game/PIE.
 */
UCLASS()
class PLANETATMOSPHERE_API APlanetAtmosphereActor : public AActor
{
	GENERATED_BODY()

public:
	APlanetAtmosphereActor();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	TObjectPtr<UPlanetAtmosphereComponent> AtmosphereComponent;
};
