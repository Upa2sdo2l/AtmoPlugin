// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "RenderGraphDefinitions.h"
#include "AtmosphereShaders.h"

class FRDGBuilder;
class FGlobalShaderMap;

namespace PlanetAtmosphere::LutCache
{
	/** Row blocks per pool = distinct atmospheres whose LUTs stay cached. Must match PA_LUT_POOL_SLOTS (AtmosphereScattering.ush). */
	constexpr int32 NumSlots = 32;
}

/** RDG handles of the two LUT pools for the current graph. */
struct FPlanetAtmosphereLutsRDG
{
	FRDGTextureRef Transmittance = nullptr;
	FRDGTextureRef MultipleScattering = nullptr;   // = Transmittance when the multiple-scattering LUT is not requested
};

/**
 * Cache of the atmosphere LUTs across frames (Phase 2.5 / Step 16). One instance per process / RHI, shared by all
 * worlds, views and planets.
 *
 *  - Two persistent pools: transmittance 256 x (64 x NumSlots) and multiple scattering 64 x (32 x NumSlots), RGBA16F.
 *    Slot s = row block s of both pools. Created on first use like the noise textures (Step 10 pattern:
 *    CreateTexture + ConvertToExternalTexture, later graphs RegisterExternalTexture).
 *  - Each slot remembers the key of the atmosphere it holds: exactly the values the LUT shaders read
 *    (planet radius, atmosphere bottom / top altitudes, the five scattering vectors, see PA_LoadScattering), compared
 *    bit for bit. A slot is (re)built only when it is assigned a new key; the camera, clouds, sun and LOD do not
 *    affect the LUTs and do not invalidate them.
 *  - Slot choice: first every atmosphere of the view whose key is cached takes its slot; then each of the others takes
 *    a never-used slot, otherwise the least recently used slot not taken by this view. Evicting a slot read earlier in the same frame is still correct: RDG orders the
 *    later write after the earlier read of the same pool texture.
 *  - Validity flags are set when the rebuild passes are added. They hold because the passes are never culled: on
 *    creation the pools are cull roots (ConvertToExternalTexture), afterwards the raymarch pass of the same graph,
 *    which always reads both bound pools, consumes them (Prepare is called only once that pass is certain to be added).
 *  - ReleaseRHI drops both pools (module shutdown, ReleaseLutCache_GameThread); after that Prepare returns false.
 */
class FPlanetAtmosphereLutCache : public FRenderResource
{
public:
	/**
	 * Render thread (RDG setup). Assigns a pool slot to each of InOutParameters.NumAtmospheres atmospheres, writes it
	 * to InOutParameters.AtmosphereLutInfo[i].x, adds the rebuild passes for slots whose LUT is missing and returns the
	 * pools. The transmittance LUT is always prepared, the multiple-scattering LUT only if bMultipleScattering
	 * (otherwise OutLuts.MultipleScattering = OutLuts.Transmittance, never sampled by the shader then).
	 * bForceRebuild (r.PlanetAtmosphere.Atmosphere.LutCache 0): invalidate every cached LUT (this view rebuilds its own).
	 * Returns false if the cache was released (module shutdown): the caller draws nothing.
	 */
	bool Prepare(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		uint32 FrameNumber,
		bool bMultipleScattering,
		bool bForceRebuild,
		FAtmosphereInstanceParameters& InOutParameters,
		FPlanetAtmosphereLutsRDG& OutLuts);

	// FRenderResource
	virtual void ReleaseRHI() override;

private:
	/** The values PA_LoadScattering reads, as packed for the shader (bitwise comparison). */
	struct FKey
	{
		FVector4f Values[6];
	};

	struct FSlot
	{
		FKey Key;
		uint32 LastUsedFrame = 0;
		bool bUsed = false;
		bool bTransmittanceValid = false;
		bool bMultipleScatteringValid = false;
	};

	static FKey MakeKey(const FAtmosphereInstanceParameters& Parameters, int32 Index);
	void ResetSlots();

	/** Guards the pools and slots in case views are set up from more than one thread. */
	FCriticalSection Mutex;
	TRefCountPtr<IPooledRenderTarget> TransmittancePool;
	TRefCountPtr<IPooledRenderTarget> MultipleScatteringPool;
	FSlot Slots[PlanetAtmosphere::LutCache::NumSlots];
};

namespace PlanetAtmosphere
{
	/** The shared instance. */
	FPlanetAtmosphereLutCache& GetLutCache();

	/** Game thread, module shutdown: releases the LUT pools on the rendering thread and waits for it. */
	void ReleaseLutCache_GameThread();
}
