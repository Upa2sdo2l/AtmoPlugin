// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "RenderGraphDefinitions.h"
#include "AtmosphereShaders.h"
#include "AtmosphereNoiseTextures.h"
#include "PlanetAtmosphereTypes.h"

class FRDGBuilder;
class FGlobalShaderMap;
class FSceneView;

namespace PlanetAtmosphere::CloudShadows
{
	constexpr int32 NumCascades = PLANET_ATMOSPHERE_SHADOW_CASCADES;
	/** Step 25: front set (lighting) + back set (rebuilt, then crossfaded in); slot = set x NumCascades + cascade. */
	constexpr int32 NumSets = 2;
	constexpr int32 NumSlots = PLANET_ATMOSPHERE_SHADOW_SLOTS;
	static_assert(NumSlots == NumSets * NumCascades, "PLANET_ATMOSPHERE_SHADOW_SLOTS must be 2 x the cascades");
	/** Texels per tile side = the update granularity. Must match PA_SHADOW_TILE (CloudShadowCommon.ush). */
	constexpr int32 TileSize = 32;
	constexpr int32 MaxTilesPerPass = PLANET_ATMOSPHERE_SHADOW_MAX_TILES_PER_PASS;
}

/** What one view's cascade update needs besides the packed atmosphere parameters. */
struct FAtmosphereCloudShadowInputs
{
	/** The packed atmospheres of the view (sorted near -> far, NumAtmospheres entries) and their radius on screen (px). */
	TConstArrayView<FAtmosphereVisibleInstance> Planets;
	TConstArrayView<double> ScreenRadiiPx;
	FAtmosphereSunLight Sun;
	FPlanetAtmosphereNoiseTexturesRDG Noise;
};

/**
 * Cloud shadow cascades (Phase 4 / Step 22; decisions AD-23..AD-25). One entry per view (FSceneViewStateInterface::GetViewKey,
 * like the temporal history), each with its own atlas; one instance per process / RHI (TGlobalResource), released in
 * ShutdownModule. Render thread only.
 *
 * Per view: 3 cascades for the primary planet (SelectPrimaryPlanet), sun-aligned orthographic grids in the PLANET-LOCAL frame (clouds are static
 * there until Phase 6; planet rotation is not considered, the sun direction in that frame is re-checked every frame).
 *  - Cascade i: half-size S0 x 4^i, S0 = MinExtent x 2^k with k from the camera height above the cloud top, capped
 *    so that cascade 2 just covers the planet (hysteresis of one level), Res x Res texels; window centred on the sub-camera point, snapped to whole tiles (no shimmer: a texel
 *    always covers the same piece of the planet). Storage is toroidal, so a moving window only invalidates the tiles that
 *    left it.
 *  - Atlas Res x 6 Res RGBA16F (Beer Shadow Map: front, back, optical depth, valid; two sets of 3 cascades), created on
 *    first use (CreateTexture + ConvertToExternalTexture, like the LUT pools) and registered in later graphs.
 *  - Step 25 (AD-28), double buffering: the FRONT set lights the image. When the sun direction in the planet frame moves
 *    by more than r.PlanetAtmosphere.CloudShadows.SunRebuildAngle or the extent level leaves its hysteresis range, the
 *    BACK set is configured for the new state and generated in the background (after the front's own new tiles); when it
 *    is complete the image crossfades to it over CrossfadeFrames, then the sets swap. No fallback march and no image jump
 *    at a rebuild (prototype p25: a rebuild without it brightened low-sun clouds by 2-20 % for ~24 frames; the swap
 *    itself changes the image by 0.4-5 %, the crossfade spreads that). Planet / cloud parameter / resolution changes
 *    still start over (nothing valid to show).
 *  - CPU state per storage tile: which global tile it holds and whether it is valid. A tile that changes owner is
 *    invalidated on the GPU (alpha 0, cheap) unless it is regenerated in the same frame. A new sun direction / extent level
 *    rebuilds the back set (see Step 25 below); planet, resolution, cloud parameters, noise / generation / extent settings
 *    start both sets over.
 *  - Generation: r.PlanetAtmosphere.CloudShadows.UpdateBudget tiles per frame, round-robin over the cascades, nearest to
 *    the window centre first. Invalid texels are never used: the raymarch falls back to its light march there (Step 23),
 *    so a new region costs quality for a moment, never a hitch.
 *  - Validity of the CPU state relies on the passes being executed: the raymarch pass of the same graph always binds
 *    the atlas (never culled), and a new atlas is a cull root at creation (ConvertToExternalTexture).
 *  - Hook for Phase 6 (moving clouds): every tile records the frame it was generated in, so a maximum age can schedule
 *    refreshes through the same queue.
 */
class FPlanetAtmosphereCloudShadows : public FRenderResource
{
public:
	/**
	 * Render thread (RDG setup), before the raymarch. Updates View's cascades (clear + generation passes, stat
	 * PlanetAtmosphere.CloudShadows) and fills OutParameters. Returns the atlas, or nullptr when the view has no cascades
	 * this frame (disabled, no persistent view state, no primary planet, no sun, resource released): then
	 * OutParameters.CloudShadowPlanet = -1.
	 */
	FRDGTextureRef Update(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		const FSceneView& View,
		const FAtmosphereInstanceParameters& InstanceParameters,
		const FAtmosphereCloudShadowInputs& Inputs,
		FAtmosphereCloudShadowParameters& OutParameters);

	/** Render thread: frees the cascades of views not rendered for a while (same policy as the temporal history). */
	void CollectGarbage(uint32 FrameNumber);

	// FRenderResource
	virtual void ReleaseRHI() override;

private:
	enum class ETileState : uint8
	{
		Stale,     // GPU content may belong to another tile: must be invalidated (or regenerated) before use
		Invalid,   // alpha 0 on the GPU: waiting for generation
		Valid,
	};

	struct FCascade
	{
		bool bConfigured = false;
		double TexelSize = 0.0;                       // cm
		FVector3d Sun = FVector3d::ZAxisVector;       // planet local, toward the sun
		FVector3d AxisU = FVector3d::XAxisVector;
		FVector3d AxisV = FVector3d::YAxisVector;
		int64 OriginTileX = 0;                        // window = global tiles [Origin, Origin + Res / TileSize)
		int64 OriginTileY = 0;
		FVector2d CameraWindowTexel = FVector2d::ZeroVector;
		// Per storage tile (row-major, Res / TileSize per side).
		TArray<int64> HeldTileX;
		TArray<int64> HeldTileY;
		TArray<ETileState> State;
		TArray<uint32> GeneratedFrame;
	};

	struct FCascadeSet
	{
		bool bConfigured = false;
		int32 ExtentLevel = -1;
	};

	struct FViewEntry
	{
		TRefCountPtr<IPooledRenderTarget> Atlas;
		int32 Resolution = 0;
		bool bHasPlanet = false;
		uint32 PlanetId = 0;
		FVector4f ContentKey[3];
		uint32 RoundRobin = 0;
		uint32 LastUsedFrame = 0;
		bool bUsed = false;
		// Step 25: slot = set x NumCascades + cascade.
		FCascade Cascades[PlanetAtmosphere::CloudShadows::NumSlots];
		FCascadeSet Sets[PlanetAtmosphere::CloudShadows::NumSets];
		int32 FrontSet = 0;
		bool bBackActive = false;     // the back set is configured and being generated / crossfaded in
		int32 CrossfadeFrame = 0;     // > 0: crossfading front -> back, weight CrossfadeFrame / (CrossfadeFrames + 1)
		int32 CrossfadeFrames = 0;    // length of the current crossfade
	};

	/** Blend = weight of the back set during a crossfade (0 = front only). */
	static void FillParameters(const FViewEntry& Entry, int32 PlanetIndex, float Blend, FAtmosphereCloudShadowParameters& OutParameters);

	/**
	 * Primary planet of the view: the largest on screen (camera inside = largest; ties -> the nearer one) among the planets
	 * with a cloud layer. The previous primary planet is kept while it is at least 1 / 1.25 of the largest (no switching
	 * back and forth, every switch regenerates the cascades). INDEX_NONE if none.
	 */
	static int32 SelectPrimaryPlanet(const FViewEntry& Entry, const FAtmosphereCloudShadowInputs& Inputs);

	FCriticalSection Mutex;
	TMap<uint32, TUniquePtr<FViewEntry>> Views;
};

namespace PlanetAtmosphere::CloudShadows
{
	/** The shared instance. */
	FPlanetAtmosphereCloudShadows& Get();

	/** Fills the parameters of "no cascades" (CloudShadowPlanet = -1). */
	void SetNoCascades(FAtmosphereCloudShadowParameters& OutParameters);

	/** Game thread, module shutdown: releases every view's atlas on the rendering thread and waits for it. */
	void Release_GameThread();
}
