// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereWeather.h"
#include "AtmosphereWeatherModel.h"
#include "AtmosphereCVars.h"
#include "AtmosphereStats.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "GlobalShader.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

// `stat gpu` -> "PlanetAtmosphere.Weather" (Phase 5 / Step 27): weather snapshot generation. Empty on frames without
// work; normally one cube face per frame and planet (background build of the next snapshot).
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereWeatherGPU, TEXT("PlanetAtmosphere.Weather"));

// `stat PlanetAtmosphere`: render thread CPU cost (slots, storm sequence, pass setup).
DECLARE_CYCLE_STAT(TEXT("Weather Update (RT)"), STAT_PlanetAtmosphere_Weather, STATGROUP_PlanetAtmosphere);

static_assert(PlanetAtmosphere::Weather::MaxStorms == PLANET_ATMOSPHERE_WEATHER_MAX_STORMS, "Storm arrays must match the model");

namespace
{
	/** Slots of planets not rendered for this many frames are freed; the atlas too when nothing uses it. */
	constexpr uint32 WeatherMaxAgeFrames = 120;

	int64 WeatherPositiveMod(int64 Value, int64 Divisor)
	{
		const int64 Mod = Value % Divisor;
		return Mod < 0 ? Mod + Divisor : Mod;
	}

	FVector4f WeatherToVector4f(const float (&Values)[4])
	{
		return FVector4f(Values[0], Values[1], Values[2], Values[3]);
	}

	/** One snapshot (time TimeSeconds) of one planet: the faces in FaceMask, written at rows [RowBase, RowBase + Res). */
	void AddWeatherSnapshotPass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		FRDGTextureRef AtlasTexture,
		int32 Resolution,
		int32 RowBase,
		const FPlanetWeatherParameters& WeatherParameters,
		double RadiusKm,
		double TimeSeconds,
		uint8 FaceMask,
		bool bSynchronous)
	{
		using namespace PlanetAtmosphere::Weather;

		FSnapshotInputs In;
		BuildSnapshotInputs(WeatherParameters, RadiusKm, TimeSeconds / 3600.0, In);

		FAtmosphereWeatherGenerateCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereWeatherGenerateCS::FParameters>();
		Parameters->OutWeatherAtlas = GraphBuilder.CreateUAV(AtlasTexture);
		Parameters->WeatherResolution = Resolution;
		Parameters->WeatherRowBase = RowBase;
		int32 NumFaceJobs = 0;
		for (int32 Face = 0; Face < NumFaces; ++Face)
		{
			Parameters->WeatherFaces[Face] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		}
		for (int32 Face = 0; Face < NumFaces; ++Face)
		{
			if (FaceMask & (1u << Face))
			{
				Parameters->WeatherFaces[NumFaceJobs++] = FVector4f(static_cast<float>(Face), 0.0f, 0.0f, 0.0f);
			}
		}
		Parameters->WeatherPlanet = FVector4f(In.RadiusKm, In.Sign, In.WindScale, In.Humidity);
		Parameters->WeatherCells = FVector4f(In.HadleyDegrees, In.FerrelDegrees, In.ThermalEquatorRadians, 0.0f);
		Parameters->WeatherNoise = FVector4f(In.NoiseAgeHours[0], In.NoiseAgeHours[1], In.NoiseWeight[0], In.NoiseWeight[1]);
		Parameters->WeatherItcz = FVector4f(In.ItczWeight[0], In.ItczWeight[1], In.ItczDriftAngle, 0.0f);
		Parameters->WeatherPhases = FVector4f(In.ItczWavePhase7, In.ItczWavePhase11, In.StormTrackWavePhase, In.MeanderPhase);
		// uint32 -> int32 bit pattern (modular conversion); the shader reads them back with asuint.
		Parameters->WeatherNoiseSeed0 = static_cast<int32>(In.NoiseSeed[0]);
		Parameters->WeatherNoiseSeed1 = static_cast<int32>(In.NoiseSeed[1]);
		Parameters->WeatherItczSeed0 = static_cast<int32>(In.ItczSeed[0]);
		Parameters->WeatherItczSeed1 = static_cast<int32>(In.ItczSeed[1]);
		Parameters->NumWeatherStorms = In.NumStorms;
		const FVector4f Zero(0.0f, 0.0f, 0.0f, 0.0f);
		for (int32 Index = 0; Index < MaxStorms; ++Index)
		{
			const bool bStorm = Index < In.NumStorms;
			Parameters->WeatherStorm0[Index] = bStorm ? WeatherToVector4f(In.Storms[Index].Center) : Zero;
			Parameters->WeatherStorm1[Index] = bStorm ? WeatherToVector4f(In.Storms[Index].East) : Zero;
			Parameters->WeatherStorm2[Index] = bStorm ? WeatherToVector4f(In.Storms[Index].Shape) : Zero;
			Parameters->WeatherStorm3[Index] = bStorm ? WeatherToVector4f(In.Storms[Index].Extra) : Zero;
		}

		const int32 Groups = FMath::DivideAndRoundUp(Resolution, FAtmosphereWeatherGenerateCS::ThreadGroupSize);
		TShaderMapRef<FAtmosphereWeatherGenerateCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PlanetAtmosphere.Weather %s t=%.2f h, %d face(s), %d storms",
				bSynchronous ? TEXT("sync") : TEXT("background"), TimeSeconds / 3600.0, NumFaceJobs, In.NumStorms),
			ComputeShader,
			Parameters,
			FIntVector(Groups, Groups, NumFaceJobs));

		UE_LOG(LogPlanetAtmosphere, Verbose, TEXT("Weather: %s build of snapshot t = %.2f h at rows %d (%d face(s), %d storms, %d active)"),
			bSynchronous ? TEXT("synchronous") : TEXT("background"), TimeSeconds / 3600.0, RowBase, NumFaceJobs, In.NumStorms, In.NumActiveStorms);
	}

	TGlobalResource<FPlanetAtmosphereWeather> GPlanetAtmosphereWeather;
}

bool FPlanetAtmosphereWeather::SameWeather(const FSlot& Slot, const FAtmosphereVisibleInstance& Planet)
{
	return Slot.RadiusKm == Planet.RadiiUU.Planet / 1.0e5 && PlanetAtmosphere::Weather::SameParameters(Slot.Parameters, Planet.Weather);
}

void FPlanetAtmosphereWeather::ResetAtlas(int32 NewResolution, int32 NewCapacity)
{
	Atlas.SafeRelease();
	Resolution = NewResolution;
	Capacity = NewCapacity;
	Slots.Init(FSlot(), NewCapacity);
}

FRDGTextureRef FPlanetAtmosphereWeather::Update(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* GlobalShaderMap,
	uint32 FrameNumber,
	const FAtmosphereWeatherInputs& Inputs,
	FAtmosphereInstanceParameters& InOutParameters,
	int32& OutResolution)
{
	using namespace PlanetAtmosphere::Weather;
	SCOPE_CYCLE_COUNTER(STAT_PlanetAtmosphere_Weather);
	FScopeLock Lock(&Mutex);

	OutResolution = 0;
	for (int32 Index = 0; Index < PLANET_ATMOSPHERE_MAX_VISIBLE; ++Index)
	{
		InOutParameters.AtmosphereWeatherInfo[Index] = FVector4f(-1.0f, 0.0f, 0.0f, 0.0f);
		InOutParameters.AtmosphereWeatherClimate[Index] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	}

	const PlanetAtmosphere::CVars::FWeatherSettings Settings = PlanetAtmosphere::CVars::GetWeatherSettings();
	const int32 NumAtmospheres = FMath::Min(FMath::Min(InOutParameters.NumAtmospheres, PLANET_ATMOSPHERE_MAX_VISIBLE), Inputs.Planets.Num());
	if (!IsInitialized() || !Settings.bEnabled || !Inputs.Time.bValid || NumAtmospheres <= 0)
	{
		return nullptr;
	}
	// The nearest planets of the view get weather.
	const int32 NumWithWeather = FMath::Min(NumAtmospheres, Settings.MaxPlanets);

	// ---- Slots. Pass 1: planets that already have one keep it. ----
	int32 SlotOfPlanet[PLANET_ATMOSPHERE_MAX_VISIBLE];
	for (int32& PlanetSlot : SlotOfPlanet)
	{
		PlanetSlot = INDEX_NONE;
	}
	TArray<bool, TInlineAllocator<8>> TakenByThisView;
	auto MatchSlots = [&]() -> int32
	{
		TakenByThisView.Init(false, Slots.Num());
		int32 Unmatched = 0;
		for (int32 Index = 0; Index < NumWithWeather; ++Index)
		{
			SlotOfPlanet[Index] = INDEX_NONE;
			for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
			{
				if (Slots[SlotIndex].bUsed && Slots[SlotIndex].PlanetId == Inputs.Planets[Index].PlanetId)
				{
					SlotOfPlanet[Index] = SlotIndex;
					TakenByThisView[SlotIndex] = true;
					break;
				}
			}
			Unmatched += SlotOfPlanet[Index] == INDEX_NONE ? 1 : 0;
		}
		return Unmatched;
	};

	// ---- The atlas: (re)created for a new resolution / a lower MaxPlanets, grown when this frame needs more slots than are
	// free (a slot used in this frame by another view is never evicted: editor + PIE would thrash). Everything is rebuilt.
	// MaxPlanets is per view; the shared atlas holds up to 2 x MaxPlanets slots (all views / worlds of a frame), within the
	// texture height limit (3 Res rows per slot, at most 16384 rows).
	const int32 MaxCapacity = FMath::Max(1, FMath::Min(2 * Settings.MaxPlanets, 16384 / (NumSnapshots * Settings.Resolution)));
	const bool bCompatible = Atlas.IsValid() && Resolution == Settings.Resolution && Capacity <= MaxCapacity;
	int32 TargetCapacity = FMath::Max(NumWithWeather, Atlas.IsValid() ? Capacity : 0);
	if (bCompatible)
	{
		const int32 Unmatched = MatchSlots();
		int32 Available = 0;
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			Available += (!TakenByThisView[SlotIndex] && (!Slots[SlotIndex].bUsed || Slots[SlotIndex].LastUsedFrame != FrameNumber)) ? 1 : 0;
		}
		TargetCapacity = Capacity + FMath::Max(0, Unmatched - Available);
	}
	TargetCapacity = FMath::Clamp(TargetCapacity, 1, MaxCapacity);

	const bool bNewAtlas = !bCompatible || TargetCapacity != Capacity;
	if (bNewAtlas)
	{
		ResetAtlas(Settings.Resolution, TargetCapacity);
		MatchSlots();
	}
	const int32 Res = Resolution;

	// Pass 2: new planets take a free slot, else the least recently used one not used in this frame (else: no weather).
	for (int32 Index = 0; Index < NumWithWeather; ++Index)
	{
		const FAtmosphereVisibleInstance& Planet = Inputs.Planets[Index];
		if (SlotOfPlanet[Index] != INDEX_NONE)
		{
			FSlot& Slot = Slots[SlotOfPlanet[Index]];
			if (!SameWeather(Slot, Planet))
			{
				// Parameters edited (proxy re-created) or a re-used component id: same slot, new weather.
				Slot.Parameters = Planet.Weather;
				Slot.RadiusKm = Planet.RadiiUU.Planet / 1.0e5;
				for (FSnapshot& Snapshot : Slot.Snapshots)
				{
					Snapshot = FSnapshot();
				}
				UE_LOG(LogPlanetAtmosphere, Log, TEXT("Weather: planet %u parameters changed, slot %d rebuilt"), Planet.PlanetId, SlotOfPlanet[Index]);
			}
			continue;
		}

		int32 Chosen = INDEX_NONE;
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num() && Chosen == INDEX_NONE; ++SlotIndex)
		{
			if (!TakenByThisView[SlotIndex] && !Slots[SlotIndex].bUsed)
			{
				Chosen = SlotIndex;
			}
		}
		if (Chosen == INDEX_NONE)
		{
			uint32 OldestAge = 0;
			for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
			{
				const FSlot& Candidate = Slots[SlotIndex];
				const uint32 Age = FrameNumber - Candidate.LastUsedFrame;
				if (!TakenByThisView[SlotIndex] && Candidate.LastUsedFrame != FrameNumber && (Chosen == INDEX_NONE || Age > OldestAge))
				{
					Chosen = SlotIndex;
					OldestAge = Age;
				}
			}
		}
		if (Chosen == INDEX_NONE)
		{
			UE_LOG(LogPlanetAtmosphere, VeryVerbose, TEXT("Weather: no free slot for planet %u (%d slots, r.PlanetAtmosphere.Weather.MaxPlanets %d)"),
				Planet.PlanetId, Capacity, Settings.MaxPlanets);
			continue;
		}

		FSlot& Slot = Slots[Chosen];
		Slot = FSlot();
		Slot.bUsed = true;
		Slot.PlanetId = Planet.PlanetId;
		Slot.Parameters = Planet.Weather;
		Slot.RadiusKm = Planet.RadiiUU.Planet / 1.0e5;
		TakenByThisView[Chosen] = true;
		SlotOfPlanet[Index] = Chosen;
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("Weather: planet %u -> slot %d (radius %.0f km, seed %d)"), Planet.PlanetId, Chosen, Slot.RadiusKm, Slot.Parameters.Seed);
	}

	// ---- Atlas texture of this graph. ----
	FRDGTextureRef AtlasTexture = nullptr;
	if (bNewAtlas)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			FIntPoint(NumFaces * Res, NumSnapshots * Res * Capacity),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
		AtlasTexture = GraphBuilder.CreateTexture(Desc, TEXT("PlanetAtmosphere.WeatherAtlas"));
	}
	else
	{
		AtlasTexture = GraphBuilder.RegisterExternalTexture(Atlas);
	}

	// ---- Snapshots on the global time grid t_k = k x interval: k and k + 1 displayed (built now if missing), k + 2 ahead. ----
	const double Interval = Settings.SnapshotIntervalSeconds;
	const double GridPosition = Inputs.Time.Seconds / Interval;
	const double GridFloor = FMath::FloorToDouble(GridPosition);
	const int64 K = static_cast<int64>(GridFloor);
	const float Blend = static_cast<float>(FMath::Clamp(GridPosition - GridFloor, 0.0, 1.0));
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereWeatherGPU, "PlanetAtmosphere.Weather");
		for (int32 Index = 0; Index < NumWithWeather; ++Index)
		{
			const int32 SlotIndex = SlotOfPlanet[Index];
			if (SlotIndex == INDEX_NONE)
			{
				continue;
			}
			FSlot& Slot = Slots[SlotIndex];
			Slot.LastUsedFrame = FrameNumber;

			// Step 28: weather seconds per frame, measured once per frame (the first view of the frame) against the previous
			// frame this planet was rendered in; unknown (0) after a gap of more than 4 frames or when the time went back.
			if (!Slot.bHasRateSample || Slot.RateSampleFrame != FrameNumber)
			{
				const uint32 FramesSince = FrameNumber - Slot.RateSampleFrame;
				Slot.TimePerFrame = (Slot.bHasRateSample && FramesSince > 0 && FramesSince <= 4 && Inputs.Time.Seconds > Slot.RateSampleTime)
					? (Inputs.Time.Seconds - Slot.RateSampleTime) / static_cast<double>(FramesSince)
					: 0.0;
				Slot.bHasRateSample = true;
				Slot.RateSampleTime = Inputs.Time.Seconds;
				Slot.RateSampleFrame = FrameNumber;
			}

			int32 SnapshotRow[2] = {0, 0};
			for (int32 Step = 0; Step < NumSnapshots; ++Step)
			{
				const int64 SnapshotIndex = K + Step;
				const double SnapshotTime = static_cast<double>(SnapshotIndex) * Interval;
				const int32 Ring = static_cast<int32>(WeatherPositiveMod(SnapshotIndex, NumSnapshots));
				const int32 RowBase = (SlotIndex * NumSnapshots + Ring) * Res;
				FSnapshot& Snapshot = Slot.Snapshots[Ring];
				if (!Snapshot.bAssigned || Snapshot.TimeSeconds != SnapshotTime)
				{
					Snapshot = FSnapshot();
					Snapshot.bAssigned = true;
					Snapshot.TimeSeconds = SnapshotTime;
				}

				uint8 Missing = static_cast<uint8>(AllFacesMask & ~Snapshot.FacesDone);
				const bool bDisplayed = Step < 2;
				if (bDisplayed)
				{
					SnapshotRow[Step] = RowBase;
				}
				else if (Missing != 0)
				{
					// Background: FacesPerFrame faces per frame and planet (once per frame, whatever the number of views).
					// Step 28: when the weather time moves fast, the faces still missing are spread over the frames left until
					// k + 2 is displayed (one frame of margin for frame-time jitter), so it is complete in time and never built
					// synchronously; nothing is built ahead when a new snapshot is needed every frame (it would be wasted).
					if (Slot.bBackgroundDone && Slot.LastBackgroundFrame == FrameNumber)
					{
						Missing = 0;
					}
					else
					{
						int32 Budget = Settings.FacesPerFrame;
						if (Slot.TimePerFrame > Interval)
						{
							Budget = 0;
						}
						else if (Slot.TimePerFrame > 0.0)
						{
							int32 MissingCount = 0;
							for (int32 Face = 0; Face < NumFaces; ++Face)
							{
								MissingCount += (Missing & (1u << Face)) ? 1 : 0;
							}
							const double Remaining = static_cast<double>(K + 1) * Interval - Inputs.Time.Seconds;
							const double FramesToDisplay = FMath::Min(FMath::CeilToDouble(Remaining / Slot.TimePerFrame), 1.0e6);
							const int32 FramesLeft = FMath::Max(1, static_cast<int32>(FramesToDisplay) - 1);
							Budget = FMath::Max(Budget, FMath::DivideAndRoundUp(MissingCount, FramesLeft));
						}
						uint8 Limited = 0;
						int32 Count = 0;
						for (int32 Face = 0; Face < NumFaces && Count < Budget; ++Face)
						{
							if (Missing & (1u << Face))
							{
								Limited = static_cast<uint8>(Limited | (1u << Face));
								++Count;
							}
						}
						Missing = Limited;
						Slot.bBackgroundDone = true;
						Slot.LastBackgroundFrame = FrameNumber;
					}
				}

				if (Missing != 0)
				{
					AddWeatherSnapshotPass(GraphBuilder, GlobalShaderMap, AtlasTexture, Res, RowBase, Slot.Parameters, Slot.RadiusKm,
						SnapshotTime, Missing, bDisplayed);
					Snapshot.FacesDone = static_cast<uint8>(Snapshot.FacesDone | Missing);
				}
			}

			InOutParameters.AtmosphereWeatherInfo[Index] = FVector4f(
				static_cast<float>(SlotIndex), static_cast<float>(SnapshotRow[0]), static_cast<float>(SnapshotRow[1]), Blend);
			// Step 28: zonal temperature inputs for DebugMode 17 (thermal equator at the current weather time, double on the CPU).
			InOutParameters.AtmosphereWeatherClimate[Index] = FVector4f(
				static_cast<float>(Slot.Parameters.MeanTemperatureK),
				static_cast<float>(Slot.Parameters.EquatorPoleDifferenceK),
				static_cast<float>(0.6 * SubsolarLatitude(Slot.Parameters, Inputs.Time.Seconds / 3600.0)),
				0.0f);
		}
	}

	// Immediate allocation + external: the atlas outlives this graph and its first passes are cull roots.
	if (bNewAtlas)
	{
		Atlas = GraphBuilder.ConvertToExternalTexture(AtlasTexture);
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("Weather: atlas %d x %d RGBA16F, %d planet slot(s) x %d snapshots x 6 faces of %d^2 (%u bytes) [ComputeMemorySize]"),
			NumFaces * Res, NumSnapshots * Res * Capacity, Capacity, NumSnapshots, Res, Atlas.IsValid() ? Atlas->ComputeMemorySize() : 0u);
	}
	AtlasLastUsedFrame = FrameNumber;
	OutResolution = Res;
	return AtlasTexture;
}

void FPlanetAtmosphereWeather::CollectGarbage(uint32 FrameNumber)
{
	FScopeLock Lock(&Mutex);
	// Only strictly older frames: a slot used in this frame is referenced by passes of the current graph.
	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		FSlot& Slot = Slots[SlotIndex];
		if (Slot.bUsed && FrameNumber > Slot.LastUsedFrame && FrameNumber - Slot.LastUsedFrame > WeatherMaxAgeFrames)
		{
			UE_LOG(LogPlanetAtmosphere, Log, TEXT("Weather: slot %d of planet %u released (unused for %u frames)"),
				SlotIndex, Slot.PlanetId, FrameNumber - Slot.LastUsedFrame);
			Slot = FSlot();
		}
	}
	if (Atlas.IsValid() && FrameNumber > AtlasLastUsedFrame && FrameNumber - AtlasLastUsedFrame > WeatherMaxAgeFrames)
	{
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("Weather: atlas released (unused for %u frames)"), FrameNumber - AtlasLastUsedFrame);
		ResetAtlas(0, 0);
	}
}

void FPlanetAtmosphereWeather::ReleaseRHI()
{
	FScopeLock Lock(&Mutex);
	ResetAtlas(0, 0);
}

namespace PlanetAtmosphere::Weather
{
	FPlanetAtmosphereWeather& Get()
	{
		return GPlanetAtmosphereWeather;
	}

	void Release_GameThread()
	{
		BeginReleaseResource(&GPlanetAtmosphereWeather);
		FlushRenderingCommands();
	}
}
