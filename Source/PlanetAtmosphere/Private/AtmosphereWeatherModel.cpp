// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereWeatherModel.h"

// Line-by-line port of storms_at / cell_edges / subsolar_lat and the time-dependent parts of weather() of the Step 26
// prototype (p26.py). Units: km, game hours, m/s. All in double.

namespace PlanetAtmosphere::Weather
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr double TwoPi = 2.0 * Pi;
		constexpr double KmhPerMps = 3.6;
		constexpr double EarthRadiusKm = 6371.0;
		constexpr double EarthOmega = TwoPi / (24.0 * 3600.0);

		/** Transported noise: anchors every 24 h, each lives 48 h (sin^2 crossfade). = p26 ANCHOR_H. */
		constexpr double NoiseAnchorHours = 48.0;
		/** ITCZ convective cluster populations live 60 h, new anchor every 30 h. */
		constexpr double ItczLifeHours = 60.0;

		/** Cap of the storms considered before the GPU limit (clamped parameters give at most ~45 at once). */
		constexpr int32 MaxCandidateStorms = 128;

		uint32 HashMix(uint32 H, int64 Value)
		{
			// numpy: np.asarray(x).astype(np.int64).astype(np.uint32) -> modulo 2^32.
			const uint32 V = static_cast<uint32>(static_cast<uint64>(Value));
			H = (H ^ V) * 0x85EBCA6Bu;
			H = H ^ (H >> 13);
			H = H * 0xC2B2AE35u;
			H = H ^ (H >> 16);
			return H;
		}

		double Wrap2Pi(double Angle)
		{
			return Angle - TwoPi * FMath::FloorToDouble(Angle / TwoPi);
		}

		double SignOf(double Value)
		{
			return Value > 0.0 ? 1.0 : (Value < 0.0 ? -1.0 : 0.0);
		}

		double Omega(const FPlanetWeatherParameters& Params)
		{
			return TwoPi / (Params.RotationPeriodHours * 3600.0);
		}

		struct FStorm
		{
			double Lat = 0.0;
			double Lon = 0.0;
			double Intensity = 0.0;
			double RadiusKm = 0.0;
			double MaxWind = 0.0;
			double Sense = 1.0;
			bool bTropical = false;
			double Age = 0.0;
			double Life = 1.0;
		};

		/** p26 storms_at. Order: hemisphere +1 (extratropical, tropical), then -1. */
		int32 GatherStorms(const FPlanetWeatherParameters& Params, double PlanetRadiusKm, double T, FStorm* OutStorms, int32 MaxOut)
		{
			int32 Count = 0;
			double Hadley = 0.0;
			double Ferrel = 0.0;
			ComputeCellEdges(Params, PlanetRadiusKm, Hadley, Ferrel);
			const double Therm = 0.6 * SubsolarLatitude(Params, T);
			const double Sign = Params.bRetrograde ? -1.0 : 1.0;
			const double L = Params.CycloneLifetimeDays * Params.RotationPeriodHours;
			const double SizeFactor = FMath::Pow(FMath::Min(1.0, PlanetRadiusKm / EarthRadiusKm), 0.5);
			const int64 Seed = Params.Seed;

			for (int32 HemiIndex = 0; HemiIndex < 2; ++HemiIndex)
			{
				const int64 Hemi = HemiIndex == 0 ? 1 : -1;
				const double HemiD = static_cast<double>(Hemi);

				// Extratropical: baroclinic storms weaken / thin out on slowly rotating planets, more on fast ones.
				const double RotFactor = FMath::Clamp(Omega(Params) / EarthOmega, 0.3, 1.5);
				double N = Params.CyclonesPerHemisphere * RotFactor;
				if (N > 0.0 && L > 0.0)
				{
					const double Dt = L / N;
					const int64 K0 = static_cast<int64>(FMath::FloorToDouble((T - 1.3 * L) / Dt)) - 1;
					const int64 K1 = static_cast<int64>(FMath::FloorToDouble(T / Dt)) + 1;
					for (int64 K = K0; K <= K1; ++K)
					{
						const int64 H2 = Hemi + 2;
						const double T0 = (static_cast<double>(K) + 0.8 * Hash01(Seed, H2, K, 1)) * Dt;
						const double Life = L * (0.8 + 0.4 * Hash01(Seed, H2, K, 2));
						const double Age = T - T0;
						if (Age < 0.0 || Age >= Life)
						{
							continue;
						}
						const double Lat0Deg = FMath::Min(0.5 * (Hadley + Ferrel) + 3.0, 62.0) + (Hash01(Seed, H2, K, 3) - 0.5) * 16.0;
						const double Lat0 = FMath::DegreesToRadians(Lat0Deg) * HemiD + 0.5 * Therm;
						const double Lon0 = Hash01(Seed, H2, K, 4) * TwoPi;
						const double EastDrift = Sign * Params.WindScale * 12.0 * (0.8 + 0.4 * Hash01(Seed, H2, K, 5));   // m/s
						const double PolewardDrift = 2.0 * HemiD;                                                          // m/s
						double Lat = Lat0 + PolewardDrift * KmhPerMps * Age / PlanetRadiusKm;
						const double LatLimit = FMath::DegreesToRadians(75.0);
						Lat = FMath::Clamp(Lat, -LatLimit, LatLimit);
						const double Lon = Lon0 + EastDrift * KmhPerMps * Age / (PlanetRadiusKm * FMath::Cos(0.5 * (Lat0 + Lat)));
						const double Phase = Age / Life;
						if (Count < MaxOut)
						{
							FStorm& S = OutStorms[Count++];
							S.Lat = Lat;
							S.Lon = Lon;
							S.Intensity = FMath::Pow(FMath::Sin(Pi * Phase), 1.5) * (0.7 + 0.3 * Hash01(Seed, H2, K, 6));
							S.RadiusKm = Params.CycloneRadiusKm * (0.8 + 0.4 * Hash01(Seed, H2, K, 7)) * SizeFactor;
							S.MaxWind = 25.0 * Params.WindScale * FMath::Min(1.0, RotFactor);
							S.Sense = Sign * HemiD;
							S.bTropical = false;
							S.Age = Age;
							S.Life = Life;
						}
					}
				}

				// Tropical (warm season of this hemisphere).
				N = Params.TropicalCyclonesPerHemisphere;
				const double Lt = 0.7 * L;
				if (N > 0.0 && Lt > 0.0)
				{
					const double Dt = Lt / N;
					const int64 K0 = static_cast<int64>(FMath::FloorToDouble((T - 1.3 * Lt) / Dt)) - 1;
					const int64 K1 = static_cast<int64>(FMath::FloorToDouble(T / Dt)) + 1;
					for (int64 K = K0; K <= K1; ++K)
					{
						const int64 H7 = Hemi + 7;
						const double T0 = (static_cast<double>(K) + 0.8 * Hash01(Seed, H7, K, 1)) * Dt;
						const double Life = Lt * (0.7 + 0.6 * Hash01(Seed, H7, K, 2));
						const double Age = T - T0;
						if (Age < 0.0 || Age >= Life)
						{
							continue;
						}
						double Season = 0.6;
						if (Params.AxialTiltDegrees > 1.0)
						{
							const double Subsolar = SubsolarLatitude(Params, T0);
							Season = 0.5 + 0.5 * SignOf(HemiD * Subsolar) * FMath::Min(1.0, FMath::Abs(FMath::RadiansToDegrees(Subsolar)) / 10.0);
						}
						if (Hash01(Seed, H7, K, 8) > Season)
						{
							continue;
						}
						const double Lat0 = FMath::DegreesToRadians(10.0 + Hash01(Seed, H7, K, 3) * 10.0) * HemiD;
						const double Lon0 = Hash01(Seed, H7, K, 4) * TwoPi;
						const double Phase = Age / Life;
						// Westward with the trades, then poleward and recurving eastward.
						const double ZonalDrift = -Sign * 5.0 * (1.0 - Phase) + Sign * 6.0 * Phase * Phase;
						const double Lat = Lat0 + HemiD * (1.0 + 4.0 * Phase) * KmhPerMps * Age / PlanetRadiusKm;
						const double Lon = Lon0 + ZonalDrift * KmhPerMps * Age / (PlanetRadiusKm * FMath::Cos(Lat));
						if (Count < MaxOut)
						{
							FStorm& S = OutStorms[Count++];
							S.Lat = Lat;
							S.Lon = Lon;
							S.Intensity = FMath::Pow(FMath::Sin(Pi * Phase), 1.2);
							S.RadiusKm = 450.0 * SizeFactor;
							S.MaxWind = 45.0 * Params.WindScale;
							S.Sense = Sign * HemiD;
							S.bTropical = true;
							S.Age = Age;
							S.Life = Life;
						}
					}
				}
			}
			return Count;
		}

		void PackStorm(const FStorm& S, double PlanetRadiusKm, FStormGPU& Out)
		{
			const double CosLat = FMath::Cos(S.Lat);
			Out.Center[0] = static_cast<float>(CosLat * FMath::Cos(S.Lon));
			Out.Center[1] = static_cast<float>(CosLat * FMath::Sin(S.Lon));
			Out.Center[2] = static_cast<float>(FMath::Sin(S.Lat));
			Out.Center[3] = static_cast<float>(S.RadiusKm);
			Out.East[0] = static_cast<float>(-FMath::Sin(S.Lon));
			Out.East[1] = static_cast<float>(FMath::Cos(S.Lon));
			Out.East[2] = 0.0f;
			Out.East[3] = static_cast<float>(S.Intensity);
			// Fronts (extratropical only): hemisphere of the storm, maturity grows over the first 30 % of its life.
			Out.Shape[0] = static_cast<float>(S.Sense);
			Out.Shape[1] = S.bTropical ? 1.0f : 0.0f;
			Out.Shape[2] = S.Lat != 0.0 ? static_cast<float>(SignOf(S.Lat)) : 1.0f;
			Out.Shape[3] = static_cast<float>(FMath::Min(1.0, S.Age / (0.3 * S.Life)));
			const double Spin = S.bTropical ? 0.6 : 0.12;
			Out.Extra[0] = static_cast<float>(S.MaxWind);
			Out.Extra[1] = static_cast<float>(Wrap2Pi(S.Sense * Spin * S.Age));
			Out.Extra[2] = static_cast<float>(FMath::Cos(4.0 * S.RadiusKm / PlanetRadiusKm));
			Out.Extra[3] = 0.0f;
		}
	}

	uint32 Hash(int64 A, int64 B)
	{
		uint32 H = 0x9E3779B9u;
		H = HashMix(H, A);
		H = HashMix(H, B);
		return H;
	}

	uint32 Hash(int64 A, int64 B, int64 C, int64 D)
	{
		uint32 H = 0x9E3779B9u;
		H = HashMix(H, A);
		H = HashMix(H, B);
		H = HashMix(H, C);
		H = HashMix(H, D);
		return H;
	}

	double Hash01(int64 A, int64 B)
	{
		return static_cast<double>(Hash(A, B)) / 4294967296.0;
	}

	double Hash01(int64 A, int64 B, int64 C, int64 D)
	{
		return static_cast<double>(Hash(A, B, C, D)) / 4294967296.0;
	}

	void ComputeCellEdges(const FPlanetWeatherParameters& Params, double PlanetRadiusKm, double& OutHadleyDegrees, double& OutFerrelDegrees)
	{
		const double Earth = EarthOmega * EarthRadiusKm;
		OutHadleyDegrees = FMath::Clamp(30.0 * Earth / (Omega(Params) * PlanetRadiusKm), 12.0, 60.0);
		OutFerrelDegrees = OutHadleyDegrees + (90.0 - OutHadleyDegrees) * 0.5;
	}

	double SubsolarLatitude(const FPlanetWeatherParameters& Params, double TimeHours)
	{
		const double YearHours = Params.YearLengthDays * Params.RotationPeriodHours;
		return FMath::DegreesToRadians(Params.AxialTiltDegrees) * FMath::Sin(TwoPi * (TimeHours / YearHours + Params.SeasonPhase));
	}

	void BuildSnapshotInputs(const FPlanetWeatherParameters& Params, double PlanetRadiusKm, double TimeHours, FSnapshotInputs& Out)
	{
		const double T = TimeHours;
		const double Sign = Params.bRetrograde ? -1.0 : 1.0;
		const int64 Seed = Params.Seed;
		double Hadley = 0.0;
		double Ferrel = 0.0;
		ComputeCellEdges(Params, PlanetRadiusKm, Hadley, Ferrel);

		Out.RadiusKm = static_cast<float>(PlanetRadiusKm);
		Out.Sign = static_cast<float>(Sign);
		Out.WindScale = static_cast<float>(Params.WindScale);
		Out.Humidity = static_cast<float>(Params.Humidity);
		Out.HadleyDegrees = static_cast<float>(Hadley);
		Out.FerrelDegrees = static_cast<float>(Ferrel);
		Out.MeanTemperatureK = static_cast<float>(Params.MeanTemperatureK);
		Out.EquatorPoleDifferenceK = static_cast<float>(Params.EquatorPoleDifferenceK);
		Out.ThermalEquatorRadians = static_cast<float>(0.6 * SubsolarLatitude(Params, T));

		// Transported noise (p26 transported_noise): anchors k = base - 1, base every 24 h, each crossfaded over 48 h.
		{
			const double Spacing = 0.5 * NoiseAnchorHours;
			const int64 Base = static_cast<int64>(FMath::FloorToDouble(T / Spacing));
			for (int32 J = 0; J < 2; ++J)
			{
				const int64 K = Base - 1 + J;
				const double Age = T - static_cast<double>(K) * Spacing;
				const double S = FMath::Sin(Pi * FMath::Clamp(Age / NoiseAnchorHours, 0.0, 1.0));
				Out.NoiseSeed[J] = static_cast<uint32>(static_cast<uint64>(Seed * 7919 + K * 3));
				Out.NoiseAgeHours[J] = static_cast<float>(Age);
				Out.NoiseWeight[J] = static_cast<float>(S * S);
			}
		}

		// ITCZ convective clusters (p26 itcz_clusters): populations every 30 h, life 60 h, drifting westward at 7 m/s.
		{
			const double Spacing = 0.5 * ItczLifeHours;
			const int64 Base = static_cast<int64>(FMath::FloorToDouble(T / Spacing));
			for (int32 J = 0; J < 2; ++J)
			{
				const int64 K = Base - 1 + J;
				const double Age = T - static_cast<double>(K) * Spacing;
				const double S = FMath::Sin(Pi * FMath::Clamp(Age / ItczLifeHours, 0.0, 1.0));
				Out.ItczSeed[J] = static_cast<uint32>(static_cast<uint64>(Seed * 3301 + K));
				Out.ItczWeight[J] = static_cast<float>(S * S);
			}
			Out.ItczDriftAngle = static_cast<float>(Wrap2Pi(Sign * 7.0 * KmhPerMps * T / PlanetRadiusKm));
			Out.ItczWavePhase7 = static_cast<float>(6.28 * Hash01(Seed, 15));
			Out.ItczWavePhase11 = static_cast<float>(6.28 * Hash01(Seed, 16));
		}

		// Planetary waves on the storm tracks: 0.10 cos(5 (lon - sign 0.3 x 15 m/s x t / (0.7 R)) + 6.28 h) = cos(5 lon + phase).
		Out.StormTrackWavePhase = static_cast<float>(Wrap2Pi(
			-5.0 * Sign * 0.3 * 15.0 * KmhPerMps * T / (PlanetRadiusKm * 0.7) + 6.28 * Hash01(Seed, 11)));
		// ITCZ meander: 4 sin(2 lon + 6.28 h + sign 2 m/s x t / R).
		Out.MeanderPhase = static_cast<float>(Wrap2Pi(6.28 * Hash01(Seed, 14) + Sign * 2.0 * KmhPerMps * T / PlanetRadiusKm));

		// Storms. Over the GPU limit (never with the clamped component parameters): the strongest are kept.
		FStorm Candidates[MaxCandidateStorms];
		const int32 NumCandidates = GatherStorms(Params, PlanetRadiusKm, T, Candidates, MaxCandidateStorms);
		Out.NumActiveStorms = NumCandidates;
		int32 Order[MaxCandidateStorms];
		for (int32 Index = 0; Index < NumCandidates; ++Index)
		{
			Order[Index] = Index;
		}
		int32 NumKept = NumCandidates;
		if (NumCandidates > MaxStorms)
		{
			// Partial selection sort by intensity (descending), then restore the generation order of the kept ones.
			for (int32 I = 0; I < MaxStorms; ++I)
			{
				int32 Best = I;
				for (int32 J = I + 1; J < NumCandidates; ++J)
				{
					if (Candidates[Order[J]].Intensity > Candidates[Order[Best]].Intensity)
					{
						Best = J;
					}
				}
				const int32 Tmp = Order[I];
				Order[I] = Order[Best];
				Order[Best] = Tmp;
			}
			NumKept = MaxStorms;
			for (int32 I = 1; I < NumKept; ++I)
			{
				for (int32 J = I; J > 0 && Order[J - 1] > Order[J]; --J)
				{
					const int32 Tmp = Order[J];
					Order[J] = Order[J - 1];
					Order[J - 1] = Tmp;
				}
			}
		}
		Out.NumStorms = NumKept;
		for (int32 Index = 0; Index < NumKept; ++Index)
		{
			PackStorm(Candidates[Order[Index]], PlanetRadiusKm, Out.Storms[Index]);
		}
	}
}
