// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlanetWeatherTypes.h"

/**
 * CPU half of the planetary weather model C (Phase 5 / Step 27, AD-30; prototype p26.py).
 *
 * The weather is a pure function of (direction on the planet, weather time, FPlanetWeatherParameters, planet radius).
 * Everything that depends only on the time is computed here in DOUBLE precision once per snapshot and handed to the GPU
 * (WeatherGenerate.usf -> WeatherModel.ush) as uniforms:
 *  - the active storms (deterministic integer-hash sequence from the seed; extratropical and tropical cyclones with
 *    their position, intensity, size, rotation phase), at most MaxStorms (the strongest are kept);
 *  - the seasonal thermal equator, the cell edges, the noise anchors (seeds, ages, crossfade weights) and the phases of
 *    the travelling waves, all reduced modulo 2 pi here so that the shader never sees a large time value.
 * Hash: identical to p26 hash_u32 (and to PA_WeatherHash in WeatherModel.ush): uint32 arithmetic, arguments cast from
 * int64 modulo 2^32.
 *
 * No engine dependencies beyond CoreMinimal (FMath): the file is also compiled stand-alone by the CPU verification
 * harness of Step 27 against the numpy prototype.
 */
namespace PlanetAtmosphere::Weather
{
	/** Storms uploaded per snapshot. Must match PA_WEATHER_MAX_STORMS (WeatherModel.ush). */
	constexpr int32 MaxStorms = 64;

	/** One storm as uploaded to the GPU (4 x float4, see WeatherStorm0..3 in WeatherModel.ush). */
	struct FStormGPU
	{
		float Center[4];   // xyz = unit vector of the storm centre (planet local), w = radius (km)
		float East[4];     // xyz = local east unit vector at the centre, w = intensity (0..1)
		float Shape[4];    // x = rotation sense (+1 / -1), y = kind (0 = extratropical, 1 = tropical), z = hemisphere of the
		                   // fronts (+1 / -1), w = front maturity (0..1)
		float Extra[4];    // x = max wind (m/s), y = spiral rotation phase (rad, mod 2 pi), z = cos of the influence radius
		                   // (4 R) seen from the planet centre, w unused
	};

	/** Uniforms of one weather snapshot (one time). Mirrors FPAWeatherInputs in WeatherModel.ush. */
	struct FSnapshotInputs
	{
		// Per planet
		float RadiusKm = 0.0f;
		float Sign = 1.0f;                 // +1 prograde, -1 retrograde
		float WindScale = 1.0f;
		float Humidity = 0.6f;
		float HadleyDegrees = 30.0f;
		float FerrelDegrees = 60.0f;
		float MeanTemperatureK = 288.15f;
		float EquatorPoleDifferenceK = 40.0f;

		// Per time
		float ThermalEquatorRadians = 0.0f;  // 0.6 x subsolar latitude
		uint32 NoiseSeed[2] = {0u, 0u};      // transported synoptic noise anchors base - 1, base (seed x 7919 + k x 3)
		float NoiseAgeHours[2] = {0.0f, 0.0f};
		float NoiseWeight[2] = {0.0f, 0.0f};
		uint32 ItczSeed[2] = {0u, 0u};       // ITCZ cluster anchors (seed x 3301 + k)
		float ItczWeight[2] = {0.0f, 0.0f};
		float ItczDriftAngle = 0.0f;         // westward drift of the clusters / easterly waves (rad, mod 2 pi)
		float ItczWavePhase7 = 0.0f;
		float ItczWavePhase11 = 0.0f;
		float StormTrackWavePhase = 0.0f;    // planetary waves on the storm tracks (rad, mod 2 pi)
		float MeanderPhase = 0.0f;           // ITCZ meander (rad, mod 2 pi)

		int32 NumStorms = 0;
		int32 NumActiveStorms = 0;           // before the MaxStorms cap (diagnostics)
		FStormGPU Storms[MaxStorms];
	};

	/** Hadley and Ferrel cell edges in degrees (Held-Hou scaling, Earth = 30 deg), as p26 cell_edges. */
	void ComputeCellEdges(const FPlanetWeatherParameters& Params, double PlanetRadiusKm, double& OutHadleyDegrees, double& OutFerrelDegrees);

	/** Latitude of the subsolar point (radians) at TimeHours, as p26 subsolar_lat. */
	double SubsolarLatitude(const FPlanetWeatherParameters& Params, double TimeHours);

	/** Fills every uniform of the snapshot at TimeHours (game hours). Deterministic. */
	void BuildSnapshotInputs(const FPlanetWeatherParameters& Params, double PlanetRadiusKm, double TimeHours, FSnapshotInputs& Out);

	/** p26 hash_u32 of 2 / 4 integers; [0, 1) versions. Exposed for the verification harness. */
	uint32 Hash(int64 A, int64 B);
	uint32 Hash(int64 A, int64 B, int64 C, int64 D);
	double Hash01(int64 A, int64 B);
	double Hash01(int64 A, int64 B, int64 C, int64 D);
}
