// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetAtmosphereActor.h"
#include "PlanetAtmosphereComponent.h"

APlanetAtmosphereActor::APlanetAtmosphereActor()
{
	PrimaryActorTick.bCanEverTick = false;

	AtmosphereComponent = CreateDefaultSubobject<UPlanetAtmosphereComponent>(TEXT("AtmosphereComponent"));
	RootComponent = AtmosphereComponent;
}
