// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereShaders.h"

// Global shaders must live in a module loaded at PostConfigInit — PlanetAtmosphere.uplugin already does that.
// "/Plugin/PlanetAtmosphere" is mapped to <Plugin>/Shaders in FPlanetAtmosphereModule::StartupModule().
IMPLEMENT_GLOBAL_SHADER(FAtmosphereBoundsDebugCS, "/Plugin/PlanetAtmosphere/Private/AtmosphereBoundsDebug.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAtmosphereCloudRaymarchCS, "/Plugin/PlanetAtmosphere/Private/CloudRaymarch.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAtmosphereNoiseBakeCS, "/Plugin/PlanetAtmosphere/Private/NoiseBake.usf", "MainCS", SF_Compute);
