// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "RenderGraphDefinitions.h"
#include "AtmosphereShaders.h"
#include "AtmosphereCVars.h"
#include "PlanetAtmosphereTypes.h"

class FRDGBuilder;
class FGlobalShaderMap;
class FSceneView;

/** Per-view decision of one frame, taken before the raymarch (Step 19: the raymarch needs the interleave offset). */
struct FAtmosphereTemporalViewSetup
{
	bool bEnabled = false;
	/** 1 = every pixel traced; N > 1 = one pixel per N x N block (r.PlanetAtmosphere.Temporal.Interleave). */
	int32 InterleaveFactor = 1;
	/** Pixel of every block traced this frame (block * N + offset), ordered-dither order. */
	FIntPoint InterleaveOffset = FIntPoint(0, 0);
};

/** Inputs of the temporal pass of one view (render thread). */
struct FAtmosphereTemporalInputs
{
	/**
	 * Raymarch output: pre-exposed luminance, alpha = reprojection depth in km along the ray (-1 = infinity).
	 * InterleaveFactor 1: scene color extent, scene-texture pixels; N > 1: one texel per block.
	 */
	FRDGTextureRef Luminance = nullptr;
	/** Raymarch output: transmittance, alpha = reprojection slot (planet index, PLANET_ATMOSPHERE_MAX_VISIBLE = camera only). */
	FRDGTextureRef Transmittance = nullptr;
	FIntRect ViewRect;
	/** Extent of the full-resolution output / history (= the scene color extent). */
	FIntPoint OutputExtent = FIntPoint(0, 0);
	/** Which image is accumulated (debug mode 0 or 5): a history of another type is not reused. */
	int32 ImageType = 0;
	/** The atmospheres of this view in shader order (slot i = Planets[i]). */
	TConstArrayView<FAtmosphereVisibleInstance> Planets;
};

/** New history of the view = the input of the composite pass. */
struct FAtmosphereTemporalOutputs
{
	FRDGTextureRef Luminance = nullptr;
	FRDGTextureRef Transmittance = nullptr;
};

/**
 * Temporal history of the clouds + atmosphere (Phase 3 / Step 18), one entry per view (FSceneViewStateInterface::GetViewKey).
 * One instance per process / RHI, same lifecycle as the noise textures and the LUT cache (TGlobalResource, released in
 * ShutdownModule). Render thread only.
 *
 * Entry: the history (luminance + transmittance RGBA16F + planet slot R16F at the scene color extent, 1 x 1 exposure),
 * the interleave counter (Step 19), and what the
 * reprojection of the next frame needs: the unjittered translated view-projection and pre-view translation of this
 * frame, and the transform of every visible planet (by PlanetId). The new history is written to new RDG textures and
 * extracted into the entry (QueueTextureExtraction); the previous one is registered as an external texture.
 * Entries live in TUniquePtr so the extraction targets keep their address; entries not used for TemporalHistoryMaxAgeFrames
 * frames are dropped (never one used in the current frame, so no extraction is pending on a dropped entry); a history
 * more than a few frames old, of another image type, or before a camera cut is not reused. Perspective views only.
 */
class FPlanetAtmosphereTemporalHistory : public FRenderResource
{
public:
	/**
	 * Render thread, before the raymarch: whether View gets the temporal pass this frame and with which interleave
	 * offset (advances the view's interleave counter). Disabled if the history is unavailable (released at shutdown),
	 * the view has no persistent state, or this view state was already prepared in this frame. A frame that starts a new
	 * history (camera cut, no / stale / other-type history) traces every pixel, so it does not begin as an upsample.
	 */
	FAtmosphereTemporalViewSetup PrepareView(const FSceneView& View, int32 RequestedInterleaveFactor, int32 ImageType);

	/**
	 * Render thread (RDG setup). Adds the temporal pass of View for a Setup returned enabled by PrepareView in the same
	 * frame. Returns false (nothing added) only if the history became unavailable.
	 */
	bool AddTemporalPass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		const FSceneView& View,
		const FAtmosphereViewParameters& ViewParameters,
		const PlanetAtmosphere::CVars::FTemporalSettings& Settings,
		const FAtmosphereTemporalViewSetup& Setup,
		const FAtmosphereTemporalInputs& Inputs,
		FAtmosphereTemporalOutputs& OutOutputs);

	/**
	 * Render thread: frees the histories of views not rendered for TemporalHistoryMaxAgeFrames frames. Called for every
	 * view that reaches the plugin's passes, also when temporal is off or no atmosphere is visible (not while
	 * r.PlanetAtmosphere.Enable is 0: then the histories wait until it is back on or the module shuts down).
	 */
	void CollectGarbage(uint32 FrameNumber);

	// FRenderResource
	virtual void ReleaseRHI() override;

private:
	struct FPlanetState
	{
		uint32 PlanetId = 0;
		FVector3d Center = FVector3d::ZeroVector;
		FVector3d Axes[3] = { FVector3d::XAxisVector, FVector3d::YAxisVector, FVector3d::ZAxisVector };
	};

	struct FViewHistory
	{
		TRefCountPtr<IPooledRenderTarget> Luminance;
		TRefCountPtr<IPooledRenderTarget> Transmittance;
		TRefCountPtr<IPooledRenderTarget> Exposure;
		TRefCountPtr<IPooledRenderTarget> Slot;
		FIntPoint Extent = FIntPoint(0, 0);
		FIntRect ViewRect;
		double TranslatedViewProjectionNoAA[4][4] = {};
		FVector3d PreViewTranslation = FVector3d::ZeroVector;
		int32 ImageType = 0;
		TArray<FPlanetState> Planets;
		uint32 LastUsedFrame = 0;
		bool bHasFrame = false;
		uint32 LastPreparedFrame = 0;
		bool bPrepared = false;
		uint32 InterleaveCounter = 0;
	};

	FCriticalSection Mutex;
	TMap<uint32, TUniquePtr<FViewHistory>> Views;
};

namespace PlanetAtmosphere::Temporal
{
	/** Views that keep a history: a persistent view state, not a scene capture / reflection capture / planar reflection. */
	bool IsAllowedForView(const FSceneView& View);

	/** The shared instance. */
	FPlanetAtmosphereTemporalHistory& GetHistory();

	/** Game thread, module shutdown: releases every history on the rendering thread and waits for it. */
	void ReleaseHistory_GameThread();
}
