// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "RenderGraphDefinitions.h"

class FRDGBuilder;
class FGlobalShaderMap;

namespace PlanetAtmosphere::NoiseTextures
{
	// Must match PA_*_NOISE_* in Shaders/Private/PlanetAtmosphereNoise.ush.
	constexpr int32 BaseSize = 128;      // 128^3 R16F, mips 128..8  -> 4 793 344 bytes of texel data
	constexpr int32 BaseOctaves = 4;
	constexpr int32 BaseNumMips = 5;
	constexpr int32 ErosionSize = 64;    // 64^3 R16F, mips 64..8    ->   599 040 bytes of texel data
	constexpr int32 ErosionOctaves = 2;
	constexpr int32 ErosionNumMips = 4;
	constexpr int32 DetailSize = 128;    // Step 32: Worley fBm; Step 32b: 128^3 R16F, mips 128..8 -> 4 793 344 bytes of texel data
	constexpr int32 DetailNumMips = 5;   // (the third Worley octave, ~45 m billows at CloudShapeScale 700 m, survives in mip 0)
	constexpr float TileSize = 4.0f;     // PA_NOISE_TILE: noise units covered by one texture repeat
}

/** RDG handles of the noise textures for the current graph. */
struct FPlanetAtmosphereNoiseTexturesRDG
{
	FRDGTextureRef BaseShape = nullptr;
	FRDGTextureRef Erosion = nullptr;
	FRDGTextureRef Detail = nullptr;   // Step 32
};

/**
 * Shared, immutable noise textures of the plugin (Phase 2 / Step 10): one instance per process / RHI,
 * shared by all worlds and planets. Lifecycle (verified against UE 5.6 sources, see ue56-verified-api.md):
 *
 *  - Declared as a TGlobalResource in AtmosphereNoiseTextures.cpp. Its constructor runs when the module DLL is
 *    loaded (PostConfigInit, before the rendering thread / RHI exist) and calls InitResource: the resource is only
 *    registered, InitRHI is empty — nothing is allocated up front.
 *  - Render thread, first use: GetOrBake() creates the textures in RDG, bakes every mip with FAtmosphereNoiseBakeCS
 *    and promotes them with ConvertToExternalTexture (immediate allocation; the bake passes become cull roots).
 *    Later graphs, and other views of the same graph, get them back with RegisterExternalTexture
 *    (returns the already registered RDG texture inside one graph).
 *  - ReleaseRHI drops both pooled render targets. It is called explicitly at module shutdown
 *    (ReleaseNoiseTextures_GameThread: BeginReleaseResource + FlushRenderingCommands), which runs before RHIExit()
 *    in FEngineLoop::Exit. After that IsInitialized() is false, GetOrBake() refuses to re-create anything and the
 *    TGlobalResource destructor's ReleaseResource is a no-op.
 */
class FPlanetAtmosphereNoiseTextures : public FRenderResource
{
public:
	/**
	 * Render thread (RDG setup). Returns false if the textures are unavailable (resource released at shutdown);
	 * the caller then skips the pass.
	 */
	bool GetOrBake(FRDGBuilder& GraphBuilder, FGlobalShaderMap* GlobalShaderMap, FPlanetAtmosphereNoiseTexturesRDG& OutTextures);

	// FRenderResource
	virtual void ReleaseRHI() override;

private:
	/** Guards the pointers in case views are set up from more than one thread. */
	FCriticalSection Mutex;
	TRefCountPtr<IPooledRenderTarget> BaseShape;
	TRefCountPtr<IPooledRenderTarget> Erosion;
	TRefCountPtr<IPooledRenderTarget> Detail;
};

namespace PlanetAtmosphere
{
	/** The shared instance. */
	FPlanetAtmosphereNoiseTextures& GetNoiseTextures();

	/** Game thread, module shutdown: releases the GPU textures on the rendering thread and waits for it. */
	void ReleaseNoiseTextures_GameThread();
}
