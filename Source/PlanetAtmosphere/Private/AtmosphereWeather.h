// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "RenderGraphDefinitions.h"
#include "AtmosphereShaders.h"
#include "PlanetAtmosphereTypes.h"

class FRDGBuilder;
class FGlobalShaderMap;

namespace PlanetAtmosphere::Weather
{
	/** Snapshots kept per planet: the two around the weather time (displayed, interpolated) + the next one (built ahead). */
	constexpr int32 NumSnapshots = 3;
	constexpr int32 NumFaces = 6;
	constexpr uint8 AllFacesMask = static_cast<uint8>((1u << NumFaces) - 1u);
}

/** What one view's weather update needs besides the packed atmosphere parameters. */
struct FAtmosphereWeatherInputs
{
	/** The packed atmospheres of the view (sorted near -> far, NumAtmospheres entries). */
	TConstArrayView<FAtmosphereVisibleInstance> Planets;
	FAtmosphereWeatherTime Time;
};

/**
 * Planetary weather on the GPU (Phase 5 / Step 27, decision AD-30). One instance per process / RHI (TGlobalResource),
 * shared by all worlds and views, released in ShutdownModule. Render thread only.
 *
 *  - Model C (prototype p26) is a pure function of (direction, weather time, planet weather parameters, radius). It is
 *    evaluated into SNAPSHOTS on a global time grid t_k = k x r.PlanetAtmosphere.Weather.SnapshotInterval: the image
 *    interpolates between the snapshots k and k + 1 around the weather time while k + 2 is built in the background,
 *    r.PlanetAtmosphere.Weather.FacesPerFrame cube faces per frame and planet. No lag and no client-specific state: two
 *    machines with the same weather time show the same weather. A snapshot needed for display that is not complete
 *    (first frame, time jump, new planet, changed parameters, a time scale faster than the background build) is built
 *    synchronously in that frame.
 *  - Storage: one atlas, RGBA16F (cloud water, humidity, wind east, wind north), equi-angular cube-sphere, 6 faces of
 *    Res x Res per snapshot, 3 snapshots per planet slot: width 6 Res, height 3 Res x slots (layout: WeatherCommon.ush).
 *    Created on first use (CreateTexture + ConvertToExternalTexture, like the LUT pools), registered in later graphs. The
 *    slot count grows to the number of planets actually seen in one frame by all views together (at most
 *    2 x r.PlanetAtmosphere.Weather.MaxPlanets, which is per view); growing, a new resolution or a lower MaxPlanets
 *    re-create it (everything is rebuilt). Freed after 120 frames without use (weather off, no planets).
 *  - Slots: per planet (FAtmosphereVisibleInstance::PlanetId), LRU like the LUT cache; a slot used in the current frame
 *    (by any view) is never evicted, the atlas grows instead (editor + PIE). Changed weather parameters or radius keep
 *    the slot and rebuild its snapshots. The nearest MaxPlanets planets of a view get weather, the others none
 *    (AtmosphereWeatherInfo.x = -1).
 *  - CPU state per snapshot (its time and which faces are done) is set when the passes are added. It holds because the
 *    passes are never culled: a new atlas is a cull root (ConvertToExternalTexture), afterwards the raymarch pass of the
 *    same graph always binds it.
 *  - Time-dependent inputs (storms, noise anchors, phases) are computed on the CPU in double precision
 *    (AtmosphereWeatherModel.h); the time on the GPU is only ever a small age.
 */
class FPlanetAtmosphereWeather : public FRenderResource
{
public:
	/**
	 * Render thread (RDG setup), before the raymarch. Builds the missing snapshots of the view's planets (stat
	 * PlanetAtmosphere.Weather), writes InOutParameters.AtmosphereWeatherInfo for all NumAtmospheres entries and returns
	 * the atlas, or nullptr when there is no weather this frame (disabled, no weather time, released).
	 * OutResolution = texels per face side.
	 */
	FRDGTextureRef Update(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		uint32 FrameNumber,
		const FAtmosphereWeatherInputs& Inputs,
		FAtmosphereInstanceParameters& InOutParameters,
		int32& OutResolution);

	/** Render thread: frees the slots of planets not rendered for a while, and the atlas when nothing uses it. */
	void CollectGarbage(uint32 FrameNumber);

	// FRenderResource
	virtual void ReleaseRHI() override;

private:
	struct FSnapshot
	{
		bool bAssigned = false;
		double TimeSeconds = 0.0;   // k x interval (compared exactly: same expression every frame)
		uint8 FacesDone = 0;        // bit f = face f written
	};

	struct FSlot
	{
		bool bUsed = false;
		uint32 PlanetId = 0;
		FPlanetWeatherParameters Parameters;
		double RadiusKm = 0.0;
		uint32 LastUsedFrame = 0;
		uint32 LastBackgroundFrame = 0;
		bool bBackgroundDone = false;   // background faces already built in LastBackgroundFrame
		FSnapshot Snapshots[PlanetAtmosphere::Weather::NumSnapshots];
	};

	static bool SameWeather(const FSlot& Slot, const FAtmosphereVisibleInstance& Planet);
	void ResetAtlas(int32 NewResolution, int32 NewCapacity);

	FCriticalSection Mutex;
	TRefCountPtr<IPooledRenderTarget> Atlas;
	int32 Resolution = 0;
	int32 Capacity = 0;
	uint32 AtlasLastUsedFrame = 0;
	TArray<FSlot> Slots;
};

namespace PlanetAtmosphere::Weather
{
	/** The shared instance. */
	FPlanetAtmosphereWeather& Get();

	/** Game thread, module shutdown: releases the weather atlas on the rendering thread and waits for it. */
	void Release_GameThread();
}
