// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereActor.h"
#include "PlanetAtmosphereComponent.h"
#include "AtmosphereWorldSubsystem.h"

APlanetAtmosphereActor::APlanetAtmosphereActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create atmosphere component
	AtmosphereComponent = CreateDefaultSubobject<UPlanetAtmosphereComponent>(TEXT("AtmosphereComponent"));
	RootComponent = AtmosphereComponent;
}

void APlanetAtmosphereActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Register with world subsystem
	if (UWorld* World = GetWorld())
	{
		if (UAtmosphereWorldSubsystem* Subsystem = World->GetSubsystem<UAtmosphereWorldSubsystem>())
		{
			Subsystem->RegisterAtmosphere(this);
		}
	}
}

void APlanetAtmosphereActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister from world subsystem
	if (UWorld* World = GetWorld())
	{
		if (UAtmosphereWorldSubsystem* Subsystem = World->GetSubsystem<UAtmosphereWorldSubsystem>())
		{
			Subsystem->UnregisterAtmosphere(this);
		}
	}
	
	Super::EndPlay(EndPlayReason);
}
