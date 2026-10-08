// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Validated planet weather parameters (Phase 5 / Step 27, decision AD-30, model C of prototype p26).
 * Built by UPlanetAtmosphereComponent::GetValidatedWeatherParameters(), copied by value into the scene proxy and the
 * per-view FAtmosphereVisibleInstance. Plain data only (no UObject, no engine types): the weather model
 * (AtmosphereWeatherModel.h) depends on nothing else.
 *
 * Units are the ones of the model: km, game hours, m/s, kelvin. "Days" are the planet's own rotation periods.
 * The weather is a pure function of (direction on the planet, weather time, these parameters, planet radius): the same
 * inputs give the same weather on every machine (future multiplayer).
 */
struct FPlanetWeatherParameters
{
	/** Seed of the deterministic weather sequence (storms, noise anchors). */
	int32 Seed = 1;

	/** Sidereal rotation period (one planet day), game hours. Drives the cell widths (Hadley edge) and storm activity. */
	double RotationPeriodHours = 24.0;

	/** Rotation direction: false = prograde (Earth), true = retrograde (winds and storm rotation flip). */
	bool bRetrograde = false;

	/** Axial tilt, degrees: seasonal shift of the thermal equator / ITCZ. */
	double AxialTiltDegrees = 23.44;

	/** Year length in planet days. */
	double YearLengthDays = 365.25;

	/** Position in the year at weather time 0 (0 = northern spring equinox, 0.25 = northern summer solstice). */
	double SeasonPhase = 0.0;

	/** Global mean temperature (K) and equator - pole difference (K). Zonal temperature only (Step 28 debug view). */
	double MeanTemperatureK = 288.15;
	double EquatorPoleDifferenceK = 40.0;

	/** Mean relative humidity at the cloud level (climatology baseline, 0..1). */
	double Humidity = 0.6;

	/** Mean lifetime of an extratropical cyclone, planet days (prototype default 5, test range 3..7). */
	double CycloneLifetimeDays = 5.0;

	/** Extratropical cyclones alive per hemisphere on average (scaled by the rotation rate). */
	double CyclonesPerHemisphere = 6.0;

	/** Radius of an extratropical cyclone, km (scaled down on planets smaller than Earth). */
	double CycloneRadiusKm = 1200.0;

	/** Tropical cyclones per hemisphere in its warm season. */
	double TropicalCyclonesPerHemisphere = 1.5;

	/** Multiplier of all wind speeds (zonal flow, storm drift and storm winds). */
	double WindScale = 1.0;
};
