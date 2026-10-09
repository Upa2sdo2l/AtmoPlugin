// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereLutCache.h"
#include "AtmosphereShaders.h"
#include "PlanetAtmosphereTypes.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "GlobalShader.h"
#include "RHIStaticStates.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

// `stat gpu` -> "PlanetAtmosphere.TransmittanceLut" (Step 13) and "PlanetAtmosphere.MultipleScatteringLut" (Step 14).
// Since Step 16 they only appear on frames where a LUT is (re)built: first frame, a parameter edit, a new planet.
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereTransmittanceLutGPU, TEXT("PlanetAtmosphere.TransmittanceLut"));
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereMultipleScatteringLutGPU, TEXT("PlanetAtmosphere.MultipleScatteringLut"));

static_assert(PlanetAtmosphere::LutCache::NumSlots >= PLANET_ATMOSPHERE_MAX_VISIBLE,
	"Every visible atmosphere of one view needs its own slot");

namespace
{
	FRDGTextureDesc MakePoolDesc(int32 LutWidth, int32 LutHeight)
	{
		return FRDGTextureDesc::Create2D(
			FIntPoint(LutWidth, LutHeight * PlanetAtmosphere::LutCache::NumSlots),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
	}

	/** Copies every per-atmosphere array element Src[SrcIndex] -> Dst[DstIndex]. Must list every array of FAtmosphereInstanceParameters. */
	void CopyInstanceEntry(const FAtmosphereInstanceParameters& Src, int32 SrcIndex, FAtmosphereInstanceParameters& Dst, int32 DstIndex)
	{
		Dst.AtmosphereData0[DstIndex] = Src.AtmosphereData0[SrcIndex];
		Dst.AtmosphereData1[DstIndex] = Src.AtmosphereData1[SrcIndex];
		Dst.AtmosphereData2[DstIndex] = Src.AtmosphereData2[SrcIndex];
		Dst.AtmosphereData3[DstIndex] = Src.AtmosphereData3[SrcIndex];
		Dst.AtmosphereAxisX[DstIndex] = Src.AtmosphereAxisX[SrcIndex];
		Dst.AtmosphereAxisY[DstIndex] = Src.AtmosphereAxisY[SrcIndex];
		Dst.AtmosphereAxisZ[DstIndex] = Src.AtmosphereAxisZ[SrcIndex];
		Dst.AtmosphereRayleigh[DstIndex] = Src.AtmosphereRayleigh[SrcIndex];
		Dst.AtmosphereMieScattering[DstIndex] = Src.AtmosphereMieScattering[SrcIndex];
		Dst.AtmosphereMieAbsorption[DstIndex] = Src.AtmosphereMieAbsorption[SrcIndex];
		Dst.AtmosphereOzone[DstIndex] = Src.AtmosphereOzone[SrcIndex];
		Dst.AtmosphereSurface[DstIndex] = Src.AtmosphereSurface[SrcIndex];
		Dst.AtmosphereLutInfo[DstIndex] = Src.AtmosphereLutInfo[SrcIndex];
		Dst.AtmosphereWeatherInfo[DstIndex] = Src.AtmosphereWeatherInfo[SrcIndex];
		Dst.AtmosphereWeatherClimate[DstIndex] = Src.AtmosphereWeatherClimate[SrcIndex];
		Dst.AtmosphereCloudData[DstIndex] = Src.AtmosphereCloudData[SrcIndex];
	}

	/**
	 * Parameters holding only the listed atmospheres (entry j = Parameters entry Indices[j], with its pool slot), for the
	 * rebuild passes: the LUT shaders run NumAtmospheres row blocks and write each to block AtmosphereLutInfo[j].x.
	 */
	void BuildCompactParameters(
		const FAtmosphereInstanceParameters& Parameters,
		TConstArrayView<int32> Indices,
		FAtmosphereInstanceParameters& OutCompact)
	{
		OutCompact = Parameters;
		OutCompact.NumAtmospheres = Indices.Num();
		for (int32 Entry = 0; Entry < Indices.Num(); ++Entry)
		{
			CopyInstanceEntry(Parameters, Indices[Entry], OutCompact, Entry);
		}
	}

	/** Step 13: rebuilds the transmittance LUT of the NumAtmospheres entries of Parameters into their pool slots. */
	void AddTransmittanceLutPass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		const FAtmosphereInstanceParameters& Parameters,
		FRDGTextureRef TransmittancePool)
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereTransmittanceLutGPU, "PlanetAtmosphere.TransmittanceLut");

		const FIntPoint DispatchSize(
			FAtmosphereTransmittanceLutCS::LutWidth,
			FAtmosphereTransmittanceLutCS::LutHeight * Parameters.NumAtmospheres);

		FAtmosphereTransmittanceLutCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAtmosphereTransmittanceLutCS::FParameters>();
		PassParameters->AtmosphereParams = Parameters;
		PassParameters->OutTransmittanceLut = GraphBuilder.CreateUAV(TransmittancePool);

		TShaderMapRef<FAtmosphereTransmittanceLutCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PlanetAtmosphere.TransmittanceLut (%d rebuilt)", Parameters.NumAtmospheres),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(DispatchSize, FAtmosphereTransmittanceLutCS::ThreadGroupSize));
	}

	/** Step 14: rebuilds the multiple-scattering LUT of the NumAtmospheres entries of Parameters (reads the transmittance pool). */
	void AddMultipleScatteringLutPass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		const FAtmosphereInstanceParameters& Parameters,
		FRDGTextureRef TransmittancePool,
		FRDGTextureRef MultipleScatteringPool)
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereMultipleScatteringLutGPU, "PlanetAtmosphere.MultipleScatteringLut");

		FAtmosphereMultipleScatteringLutCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAtmosphereMultipleScatteringLutCS::FParameters>();
		PassParameters->AtmosphereParams = Parameters;
		PassParameters->TransmittanceLutAtlas = TransmittancePool;
		PassParameters->TransmittanceLutSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->OutMultipleScatteringLut = GraphBuilder.CreateUAV(MultipleScatteringPool);

		// One thread group per texel: [numthreads(1, 1, 64)], 64 = directions.
		TShaderMapRef<FAtmosphereMultipleScatteringLutCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PlanetAtmosphere.MultipleScatteringLut (%d rebuilt)", Parameters.NumAtmospheres),
			ComputeShader,
			PassParameters,
			FIntVector(
				FAtmosphereMultipleScatteringLutCS::LutWidth,
				FAtmosphereMultipleScatteringLutCS::LutHeight * Parameters.NumAtmospheres,
				1));
	}

	TGlobalResource<FPlanetAtmosphereLutCache> GPlanetAtmosphereLutCache;
}

FPlanetAtmosphereLutCache::FKey FPlanetAtmosphereLutCache::MakeKey(const FAtmosphereInstanceParameters& Parameters, int32 Index)
{
	// Exactly what PA_LoadScattering (AtmosphereScattering.ush) reads; nothing else enters the LUT shaders.
	FKey Key;
	Key.Values[0] = FVector4f(
		Parameters.AtmosphereData0[Index].W,    // planet radius
		Parameters.AtmosphereData1[Index].Y,    // atmosphere bottom altitude
		Parameters.AtmosphereData1[Index].Z,    // atmosphere top altitude
		0.0f);
	Key.Values[1] = Parameters.AtmosphereRayleigh[Index];
	Key.Values[2] = Parameters.AtmosphereMieScattering[Index];
	Key.Values[3] = Parameters.AtmosphereMieAbsorption[Index];
	Key.Values[4] = Parameters.AtmosphereOzone[Index];
	Key.Values[5] = Parameters.AtmosphereSurface[Index];
	return Key;
}

void FPlanetAtmosphereLutCache::ResetSlots()
{
	for (FSlot& Slot : Slots)
	{
		Slot = FSlot();
	}
}

bool FPlanetAtmosphereLutCache::Prepare(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* GlobalShaderMap,
	uint32 FrameNumber,
	bool bMultipleScattering,
	bool bForceRebuild,
	FAtmosphereInstanceParameters& InOutParameters,
	FPlanetAtmosphereLutsRDG& OutLuts)
{
	using PlanetAtmosphere::LutCache::NumSlots;
	FScopeLock Lock(&Mutex);

	// Released at module shutdown (or never initialized): never re-create.
	const int32 NumAtmospheres = FMath::Min(InOutParameters.NumAtmospheres, PLANET_ATMOSPHERE_MAX_VISIBLE);
	if (!IsInitialized() || NumAtmospheres <= 0)
	{
		return false;
	}

	// ---- Pools. A newly created pool holds nothing: forget every slot it backs. ----
	const bool bNewTransmittancePool = !TransmittancePool.IsValid();
	if (bNewTransmittancePool)
	{
		ResetSlots();
		OutLuts.Transmittance = GraphBuilder.CreateTexture(
			MakePoolDesc(FAtmosphereTransmittanceLutCS::LutWidth, FAtmosphereTransmittanceLutCS::LutHeight),
			TEXT("PlanetAtmosphere.TransmittanceLutPool"));
	}
	else
	{
		OutLuts.Transmittance = GraphBuilder.RegisterExternalTexture(TransmittancePool);
	}

	const bool bNewMultipleScatteringPool = bMultipleScattering && !MultipleScatteringPool.IsValid();
	if (bNewMultipleScatteringPool)
	{
		for (FSlot& Slot : Slots)
		{
			Slot.bMultipleScatteringValid = false;
		}
		OutLuts.MultipleScattering = GraphBuilder.CreateTexture(
			MakePoolDesc(FAtmosphereMultipleScatteringLutCS::LutWidth, FAtmosphereMultipleScatteringLutCS::LutHeight),
			TEXT("PlanetAtmosphere.MultipleScatteringLutPool"));
	}
	else if (bMultipleScattering)
	{
		OutLuts.MultipleScattering = GraphBuilder.RegisterExternalTexture(MultipleScatteringPool);
	}
	else
	{
		OutLuts.MultipleScattering = OutLuts.Transmittance;
	}

	// ---- Slot of every atmosphere of this view. ----
	// Pass 1: cached keys keep their slot (so pass 2 never evicts a slot another atmosphere of this view still needs).
	bool bTakenByThisView[NumSlots] = {};
	int32 SlotOfAtmosphere[PLANET_ATMOSPHERE_MAX_VISIBLE];
	FKey Keys[PLANET_ATMOSPHERE_MAX_VISIBLE];
	for (int32 Index = 0; Index < NumAtmospheres; ++Index)
	{
		Keys[Index] = MakeKey(InOutParameters, Index);
		SlotOfAtmosphere[Index] = INDEX_NONE;
		for (int32 Candidate = 0; Candidate < NumSlots; ++Candidate)
		{
			if (Slots[Candidate].bUsed && FMemory::Memcmp(&Slots[Candidate].Key, &Keys[Index], sizeof(FKey)) == 0)
			{
				SlotOfAtmosphere[Index] = Candidate;
				bTakenByThisView[Candidate] = true;
				break;
			}
		}
	}

	// Pass 2: new keys. Two atmospheres with identical parameters share one slot (the second finds the first's key).
	for (int32 Index = 0; Index < NumAtmospheres; ++Index)
	{
		if (SlotOfAtmosphere[Index] != INDEX_NONE)
		{
			continue;
		}

		int32 SlotIndex = INDEX_NONE;
		for (int32 Candidate = 0; Candidate < NumSlots; ++Candidate)
		{
			if (bTakenByThisView[Candidate] && FMemory::Memcmp(&Slots[Candidate].Key, &Keys[Index], sizeof(FKey)) == 0)
			{
				SlotIndex = Candidate;
				break;
			}
		}

		if (SlotIndex == INDEX_NONE)
		{
			// A never-used slot, else the least recently used one not taken by this view (always exists: NumSlots >= 16).
			for (int32 Candidate = 0; Candidate < NumSlots; ++Candidate)
			{
				if (!Slots[Candidate].bUsed)
				{
					SlotIndex = Candidate;
					break;
				}
			}
			if (SlotIndex == INDEX_NONE)
			{
				for (int32 Candidate = 0; Candidate < NumSlots; ++Candidate)
				{
					if (!bTakenByThisView[Candidate]
						&& (SlotIndex == INDEX_NONE || Slots[Candidate].LastUsedFrame < Slots[SlotIndex].LastUsedFrame))
					{
						SlotIndex = Candidate;
					}
				}
			}
			check(SlotIndex != INDEX_NONE);

			FSlot& NewSlot = Slots[SlotIndex];
			NewSlot = FSlot();
			NewSlot.Key = Keys[Index];
			NewSlot.bUsed = true;
			bTakenByThisView[SlotIndex] = true;
		}
		SlotOfAtmosphere[Index] = SlotIndex;
	}

	// r.PlanetAtmosphere.Atmosphere.LutCache 0: every cached LUT is stale (keys and slots are kept). This view rebuilds
	// the ones it uses; the others are rebuilt when next used, so none built by older shaders survives the toggle.
	if (bForceRebuild)
	{
		for (FSlot& Slot : Slots)
		{
			Slot.bTransmittanceValid = false;
			Slot.bMultipleScatteringValid = false;
		}
	}

	// ---- What must be rebuilt. The flags are set when the rebuild is queued, so a shared slot is queued once. ----
	TArray<int32, TInlineAllocator<PLANET_ATMOSPHERE_MAX_VISIBLE>> RebuildTransmittance;
	TArray<int32, TInlineAllocator<PLANET_ATMOSPHERE_MAX_VISIBLE>> RebuildMultipleScattering;
	for (int32 Index = 0; Index < NumAtmospheres; ++Index)
	{
		FSlot& Slot = Slots[SlotOfAtmosphere[Index]];
		Slot.LastUsedFrame = FrameNumber;
		InOutParameters.AtmosphereLutInfo[Index] = FVector4f(static_cast<float>(SlotOfAtmosphere[Index]), 0.0f, 0.0f, 0.0f);

		if (!Slot.bTransmittanceValid)
		{
			RebuildTransmittance.Add(Index);
			Slot.bTransmittanceValid = true;
			Slot.bMultipleScatteringValid = false;   // computed from the transmittance LUT
		}
		if (bMultipleScattering && !Slot.bMultipleScatteringValid)
		{
			RebuildMultipleScattering.Add(Index);
			Slot.bMultipleScatteringValid = true;
		}
	}

	// ---- Rebuild passes (transmittance first: the multiple-scattering pass reads it). ----
	if (RebuildTransmittance.Num() > 0 || RebuildMultipleScattering.Num() > 0)
	{
		FAtmosphereInstanceParameters Compact;
		if (RebuildTransmittance.Num() > 0)
		{
			BuildCompactParameters(InOutParameters, RebuildTransmittance, Compact);
			AddTransmittanceLutPass(GraphBuilder, GlobalShaderMap, Compact, OutLuts.Transmittance);
		}
		if (RebuildMultipleScattering.Num() > 0)
		{
			BuildCompactParameters(InOutParameters, RebuildMultipleScattering, Compact);
			AddMultipleScatteringLutPass(GraphBuilder, GlobalShaderMap, Compact, OutLuts.Transmittance, OutLuts.MultipleScattering);
		}

		UE_LOG(LogPlanetAtmosphere, Verbose, TEXT("LUT cache, frame %u: rebuilt transmittance x%d, multiple scattering x%d (%d atmospheres in view)"),
			FrameNumber, RebuildTransmittance.Num(), RebuildMultipleScattering.Num(), NumAtmospheres);
	}

	// Immediate allocation + external: the pools outlive this graph and their first passes are cull roots.
	if (bNewTransmittancePool)
	{
		TransmittancePool = GraphBuilder.ConvertToExternalTexture(OutLuts.Transmittance);
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("LUT cache: transmittance pool created, %d slots (%u bytes) [ComputeMemorySize]"),
			NumSlots, TransmittancePool.IsValid() ? TransmittancePool->ComputeMemorySize() : 0u);
	}
	if (bNewMultipleScatteringPool)
	{
		MultipleScatteringPool = GraphBuilder.ConvertToExternalTexture(OutLuts.MultipleScattering);
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("LUT cache: multiple-scattering pool created, %d slots (%u bytes) [ComputeMemorySize]"),
			NumSlots, MultipleScatteringPool.IsValid() ? MultipleScatteringPool->ComputeMemorySize() : 0u);
	}
	return true;
}

void FPlanetAtmosphereLutCache::ReleaseRHI()
{
	FScopeLock Lock(&Mutex);
	TransmittancePool.SafeRelease();
	MultipleScatteringPool.SafeRelease();
	ResetSlots();
}

namespace PlanetAtmosphere
{
	FPlanetAtmosphereLutCache& GetLutCache()
	{
		return GPlanetAtmosphereLutCache;
	}

	void ReleaseLutCache_GameThread()
	{
		BeginReleaseResource(&GPlanetAtmosphereLutCache);
		FlushRenderingCommands();
	}
}
