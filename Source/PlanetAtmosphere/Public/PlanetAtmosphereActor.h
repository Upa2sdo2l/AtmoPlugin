// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetAtmosphereActor.generated.h"

class UPlanetAtmosphereComponent;

/**
 * Actor that represents a planet with volumetric atmosphere.
 * This is a thin wrapper around UPlanetAtmosphereComponent.
 */
UCLASS()
class PLANETATMOSPHERE_API APlanetAtmosphereActor : public AActor
{
	GENERATED_BODY()
	
public:	
	APlanetAtmosphereActor();

protected:
	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	TObjectPtr<UPlanetAtmosphereComponent> AtmosphereComponent;
};
