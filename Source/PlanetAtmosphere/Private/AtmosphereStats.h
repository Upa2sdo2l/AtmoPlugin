// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/**
 * CPU stat group of the plugin: console `stat PlanetAtmosphere`.
 * Cycle stats themselves are declared (DECLARE_CYCLE_STAT) in the .cpp files that use them.
 * The GPU stat (`stat gpu` -> "PlanetAtmosphere") is declared in AtmosphereRenderer.cpp.
 */
DECLARE_STATS_GROUP(TEXT("PlanetAtmosphere"), STATGROUP_PlanetAtmosphere, STATCAT_Advanced);
