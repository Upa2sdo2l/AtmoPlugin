// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereCloudShadows.h"
#include "AtmosphereCVars.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "GlobalShader.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "SceneManagement.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

// `stat gpu` -> "PlanetAtmosphere.CloudShadows" (Phase 4, master prompt section 28): generation + invalidation passes.
// Empty on frames without work (static camera, everything generated).
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereCloudShadowsGPU, TEXT("PlanetAtmosphere.CloudShadows"));

namespace
{
	/** Cascades of views not rendered for this many frames are freed (closed viewport, finished PIE session). */
	constexpr uint32 CloudShadowMaxAgeFrames = 120;

	/** The cascades are regenerated when the sun direction in the planet frame changes by more than 0.25 deg. */
	constexpr double CloudShadowSunChangeCos = 0.99999048072;   // cos(0.25 deg)

	/** Extent levels: S0 = MinExtent x 2^k. 2^30 x 1 km covers any planet. */
	constexpr int32 CloudShadowMaxExtentLevel = 30;

	/** The previous primary planet stays primary while the largest one on screen is at most this much larger. */
	constexpr double CloudShadowPrimaryHysteresis = 1.25;

	int64 CloudShadowPositiveMod(int64 Value, int64 Divisor)
	{
		const int64 Mod = Value % Divisor;
		return Mod < 0 ? Mod + Divisor : Mod;
	}

	/** World-axis vector -> planet local frame (rows of the planet axes, as PA_ToPlanetLocal). */
	FVector3d CloudShadowToPlanetLocal(const FVector3d& V, const FAtmosphereVisibleInstance& Planet)
	{
		return FVector3d(
			FVector3d::DotProduct(V, Planet.PlanetAxisX),
			FVector3d::DotProduct(V, Planet.PlanetAxisY),
			FVector3d::DotProduct(V, Planet.PlanetAxisZ));
	}

	/** Light-plane axes perpendicular to the sun direction (right-handed: U x V = Sun). */
	void CloudShadowMakeAxes(const FVector3d& Sun, FVector3d& OutU, FVector3d& OutV)
	{
		const FVector3d Reference = FMath::Abs(Sun.Z) < 0.9 ? FVector3d::ZAxisVector : FVector3d::XAxisVector;
		OutU = FVector3d::CrossProduct(Reference, Sun).GetSafeNormal();
		OutV = FVector3d::CrossProduct(Sun, OutU);
	}

	/** Adds the tile passes (generation or invalidation) for Tiles, in batches of MaxTilesPerPass. */
	void CloudShadowAddTilePasses(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		const FAtmosphereInstanceParameters& InstanceParameters,
		const FAtmosphereCloudShadowParameters& ShadowParameters,
		const FAtmosphereCloudShadowInputs& Inputs,
		int32 GenerationSteps,
		FRDGTextureRef Atlas,
		TConstArrayView<FVector4f> Tiles,
		const TCHAR* What)
	{
		using PlanetAtmosphere::CloudShadows::MaxTilesPerPass;
		using PlanetAtmosphere::CloudShadows::TileSize;
		TShaderMapRef<FAtmosphereCloudShadowGenerateCS> ComputeShader(GlobalShaderMap);
		for (int32 First = 0; First < Tiles.Num(); First += MaxTilesPerPass)
		{
			const int32 Count = FMath::Min(MaxTilesPerPass, Tiles.Num() - First);
			FAtmosphereCloudShadowGenerateCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereCloudShadowGenerateCS::FParameters>();
			Parameters->AtmosphereParams = InstanceParameters;
			Parameters->CloudShadowParams = ShadowParameters;
			Parameters->OutShadowAtlas = GraphBuilder.CreateUAV(Atlas);
			for (int32 Index = 0; Index < MaxTilesPerPass; ++Index)
			{
				Parameters->ShadowTiles[Index] = Index < Count ? Tiles[First + Index] : FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
			}
			Parameters->NumShadowTiles = Count;
			Parameters->ShadowGenerationSteps = GenerationSteps;
			Parameters->BaseNoiseTexture = Inputs.Noise.BaseShape;
			Parameters->ErosionNoiseTexture = Inputs.Noise.Erosion;
			Parameters->NoiseSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			Parameters->NoiseSource = static_cast<int32>(PlanetAtmosphere::CVars::GetNoiseSource());
			Parameters->NoiseFootprintScale = PlanetAtmosphere::CVars::GetNoiseFootprintScale();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.CloudShadow%s %d tiles", What, Count),
				ComputeShader,
				Parameters,
				FIntVector(TileSize / FAtmosphereCloudShadowGenerateCS::ThreadGroupSize, TileSize / FAtmosphereCloudShadowGenerateCS::ThreadGroupSize, Count));
		}
	}

	TGlobalResource<FPlanetAtmosphereCloudShadows> GPlanetAtmosphereCloudShadows;
}

void FPlanetAtmosphereCloudShadows::FillParameters(const FViewEntry& Entry, int32 PlanetIndex, FAtmosphereCloudShadowParameters& OutParameters)
{
	using PlanetAtmosphere::CloudShadows::TileSize;
	OutParameters.CloudShadowPlanet = PlanetIndex;
	OutParameters.CloudShadowResolution = Entry.Resolution;
	for (int32 Cascade = 0; Cascade < PlanetAtmosphere::CloudShadows::NumCascades; ++Cascade)
	{
		const FCascade& C = Entry.Cascades[Cascade];
		const double Texel = FMath::Max(C.TexelSize, 1.0);
		OutParameters.CloudShadowAxisU[Cascade] = FVector4f(
			static_cast<float>(C.AxisU.X), static_cast<float>(C.AxisU.Y), static_cast<float>(C.AxisU.Z), static_cast<float>(Texel));
		OutParameters.CloudShadowAxisV[Cascade] = FVector4f(
			static_cast<float>(C.AxisV.X), static_cast<float>(C.AxisV.Y), static_cast<float>(C.AxisV.Z), static_cast<float>(1.0 / Texel));
		OutParameters.CloudShadowSun[Cascade] = FVector4f(
			static_cast<float>(C.Sun.X), static_cast<float>(C.Sun.Y), static_cast<float>(C.Sun.Z), C.bConfigured ? 1.0f : 0.0f);
		// Global texel indices stay far below 2^24 (exact in float): planet radius / smallest texel (1 km x 2 / 1024).
		OutParameters.CloudShadowWindow[Cascade] = FVector4f(
			static_cast<float>(C.OriginTileX * TileSize), static_cast<float>(C.OriginTileY * TileSize),
			static_cast<float>(C.CameraWindowTexel.X), static_cast<float>(C.CameraWindowTexel.Y));
	}
}

int32 FPlanetAtmosphereCloudShadows::SelectPrimaryPlanet(const FViewEntry& Entry, const FAtmosphereCloudShadowInputs& Inputs)
{
	const int32 NumPlanets = FMath::Min(Inputs.Planets.Num(), Inputs.ScreenRadiiPx.Num());
	int32 Best = INDEX_NONE;
	int32 Previous = INDEX_NONE;
	for (int32 Index = 0; Index < NumPlanets; ++Index)
	{
		const FAtmosphereVisibleInstance& Planet = Inputs.Planets[Index];
		if (Planet.CloudCoverage <= 0.0f || Planet.CloudDensity <= 0.0f || Planet.RadiiUU.CloudTop <= Planet.RadiiUU.CloudBottom)
		{
			continue;
		}
		if (Best == INDEX_NONE || Inputs.ScreenRadiiPx[Index] > Inputs.ScreenRadiiPx[Best])
		{
			Best = Index;
		}
		if (Entry.bHasPlanet && Planet.PlanetId == Entry.PlanetId)
		{
			Previous = Index;
		}
	}
	if (Previous != INDEX_NONE && Best != INDEX_NONE
		&& Inputs.ScreenRadiiPx[Previous] * CloudShadowPrimaryHysteresis >= Inputs.ScreenRadiiPx[Best])
	{
		return Previous;
	}
	return Best;
}

FRDGTextureRef FPlanetAtmosphereCloudShadows::Update(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* GlobalShaderMap,
	const FSceneView& View,
	const FAtmosphereInstanceParameters& InstanceParameters,
	const FAtmosphereCloudShadowInputs& Inputs,
	FAtmosphereCloudShadowParameters& OutParameters)
{
	using namespace PlanetAtmosphere::CloudShadows;
	FScopeLock Lock(&Mutex);
	SetNoCascades(OutParameters);

	const PlanetAtmosphere::CVars::FCloudShadowSettings Settings = PlanetAtmosphere::CVars::GetCloudShadowSettings();
	if (!IsInitialized() || !Settings.bEnabled || View.State == nullptr
		|| !Inputs.Sun.bValid || Inputs.Noise.BaseShape == nullptr || Inputs.Noise.Erosion == nullptr)
	{
		return nullptr;
	}

	const uint32 ViewKey = View.State->GetViewKey();
	const uint32 FrameNumber = View.Family->FrameNumber;
	TUniquePtr<FViewEntry>& EntryPtr = Views.FindOrAdd(ViewKey);
	if (!EntryPtr.IsValid())
	{
		EntryPtr = MakeUnique<FViewEntry>();
	}
	FViewEntry& Entry = *EntryPtr;

	const int32 Index = SelectPrimaryPlanet(Entry, Inputs);
	if (Index == INDEX_NONE || Index >= InstanceParameters.NumAtmospheres)
	{
		return nullptr;   // the entry is not marked used: freed after CloudShadowMaxAgeFrames if this persists
	}
	const FAtmosphereVisibleInstance& Planet = Inputs.Planets[Index];

	// The same view state rendered twice in one frame: the cascades as the first render left them, no new passes.
	if (Entry.bUsed && Entry.LastUsedFrame == FrameNumber)
	{
		if (!Entry.Atlas.IsValid() || !Entry.bHasPlanet || Entry.PlanetId != Planet.PlanetId)
		{
			return nullptr;
		}
		FillParameters(Entry, Index, OutParameters);
		return GraphBuilder.RegisterExternalTexture(Entry.Atlas);
	}
	Entry.bUsed = true;
	Entry.LastUsedFrame = FrameNumber;

	const int32 Res = Settings.Resolution;
	const int32 TilesPerSide = Res / TileSize;
	const int32 TilesPerCascade = TilesPerSide * TilesPerSide;

	// ---- Atlas (new on first use or after a resolution change: every cascade starts over) ----
	FRDGTextureRef Atlas = nullptr;
	const bool bNewAtlas = !Entry.Atlas.IsValid() || Entry.Resolution != Res;
	if (bNewAtlas)
	{
		Entry.Atlas.SafeRelease();
		Entry.Resolution = Res;
		for (FCascade& Cascade : Entry.Cascades)
		{
			Cascade = FCascade();
		}
		Atlas = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(FIntPoint(Res, Res * NumCascades), PF_FloatRGBA, FClearValueBinding::Black,
				ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV),
			TEXT("PlanetAtmosphere.CloudShadowAtlas"));
	}
	else
	{
		Atlas = GraphBuilder.RegisterExternalTexture(Entry.Atlas);
	}

	// ---- What the content depends on: the planet, its cloud layer (bitwise, as packed for the shader), noise and
	// generation settings. Any change regenerates every cascade. ----
	FVector4f ContentKey[3];
	ContentKey[0] = FVector4f(
		InstanceParameters.AtmosphereData0[Index].W, InstanceParameters.AtmosphereData1[Index].W,
		InstanceParameters.AtmosphereData2[Index].X, InstanceParameters.AtmosphereData2[Index].Y);
	ContentKey[1] = FVector4f(
		InstanceParameters.AtmosphereData2[Index].Z, InstanceParameters.AtmosphereData2[Index].W,
		InstanceParameters.AtmosphereData3[Index].X, 0.0f);
	ContentKey[2] = FVector4f(
		static_cast<float>(static_cast<int32>(PlanetAtmosphere::CVars::GetNoiseSource())), PlanetAtmosphere::CVars::GetNoiseFootprintScale(),
		static_cast<float>(Settings.GenerationSteps), 0.0f);
	const bool bSamePlanet = Entry.bHasPlanet && Entry.PlanetId == Planet.PlanetId;
	if (!bSamePlanet || FMemory::Memcmp(Entry.ContentKey, ContentKey, sizeof(ContentKey)) != 0)
	{
		if (!bSamePlanet)
		{
			UE_LOG(LogPlanetAtmosphere, Log, TEXT("Cloud shadows of view %u: primary planet %u (cascades regenerated)"), ViewKey, Planet.PlanetId);
		}
		for (FCascade& Cascade : Entry.Cascades)
		{
			Cascade.bConfigured = false;
		}
		Entry.bHasPlanet = true;
		Entry.PlanetId = Planet.PlanetId;
		FMemory::Memcpy(Entry.ContentKey, ContentKey, sizeof(ContentKey));
	}

	// ---- Camera and sun in the planet frame (double) ----
	const FVector3d CameraLocal = CloudShadowToPlanetLocal(View.ViewMatrices.GetViewOrigin() - Planet.PlanetCenterWorld, Planet);
	const FVector3d SunLocal = CloudShadowToPlanetLocal(Inputs.Sun.DirectionToSun, Planet).GetSafeNormal();
	const FPlanetAtmosphereRadii& Radii = Planet.RadiiUU;
	const double HeightAboveClouds = FMath::Max(CameraLocal.Length() - Radii.CloudTop, 0.0);

	// Extent level k: cascade 0 half-size S0 = MinExtent x 2^k, the smallest k with S0 >= height above the clouds, but no
	// larger than needed for cascade 2 (16 S0) to cover the planet (far away the whole planet is in view: finer texels
	// instead of cascades many times its size). The current level is kept while it is at most one above the needed one
	// (no regeneration back and forth around a threshold).
	const double TargetHalfSize = FMath::Min(HeightAboveClouds, Radii.CloudTop / 16.0);
	int32 NeededLevel = 0;
	while (NeededLevel < CloudShadowMaxExtentLevel && Settings.MinExtentCm * FMath::Pow(2.0, static_cast<double>(NeededLevel)) < TargetHalfSize)
	{
		++NeededLevel;
	}
	if (Entry.ExtentLevel < NeededLevel || Entry.ExtentLevel > NeededLevel + 1)
	{
		UE_LOG(LogPlanetAtmosphere, Verbose, TEXT("Cloud shadows of view %u: extent level %d -> %d (camera %.1f km above the clouds)"),
			ViewKey, Entry.ExtentLevel, NeededLevel, HeightAboveClouds * 1e-5);
		Entry.ExtentLevel = NeededLevel;
	}
	const double HalfSize0 = Settings.MinExtentCm * FMath::Pow(2.0, static_cast<double>(Entry.ExtentLevel));

	// Window centre: the sub-camera point on the middle of the cloud layer.
	const FVector3d CenterPoint = CameraLocal.GetSafeNormal() * (0.5 * (Radii.CloudBottom + Radii.CloudTop));

	// ---- Per cascade: configuration, window, tile ownership ----
	int32 ClearMask = 0;
	TArray<FVector4f> InvalidateTiles;
	int64 CenterTileX[NumCascades];
	int64 CenterTileY[NumCascades];
	for (int32 CascadeIndex = 0; CascadeIndex < NumCascades; ++CascadeIndex)
	{
		FCascade& C = Entry.Cascades[CascadeIndex];
		const double Texel = 2.0 * HalfSize0 * FMath::Pow(4.0, static_cast<double>(CascadeIndex)) / static_cast<double>(Res);
		const bool bReconfigure = !C.bConfigured || C.TexelSize != Texel || C.State.Num() != TilesPerCascade
			|| FVector3d::DotProduct(C.Sun, SunLocal) < CloudShadowSunChangeCos;
		if (bReconfigure)
		{
			// Should be rare (camera height crossing a level, sun turned, parameter edit): repeated lines with a static
			// camera mean the cascades never finish (diagnostic for the UE test).
			UE_LOG(LogPlanetAtmosphere, Log, TEXT("Cloud shadows of view %u, frame %u: cascade %d regenerated (%s), texel %.1f m"),
				ViewKey, FrameNumber, CascadeIndex,
				!C.bConfigured ? (bNewAtlas ? TEXT("new atlas") : TEXT("planet / cloud parameters / settings / extent level"))
					: (C.TexelSize != Texel ? TEXT("texel size")
					: (C.State.Num() != TilesPerCascade ? TEXT("resolution") : TEXT("sun direction"))),
				Texel * 1e-2);
			C.bConfigured = true;
			C.TexelSize = Texel;
			C.Sun = SunLocal;
			CloudShadowMakeAxes(SunLocal, C.AxisU, C.AxisV);
			C.HeldTileX.Init(TNumericLimits<int64>::Max(), TilesPerCascade);
			C.HeldTileY.Init(TNumericLimits<int64>::Max(), TilesPerCascade);
			C.State.Init(ETileState::Invalid, TilesPerCascade);
			C.GeneratedFrame.Init(0, TilesPerCascade);
			ClearMask |= 1 << CascadeIndex;
		}

		// Window snapped to whole tiles around the sub-camera point.
		const double TileWorld = static_cast<double>(TileSize) * Texel;
		const double CenterU = FVector3d::DotProduct(CenterPoint, C.AxisU);
		const double CenterV = FVector3d::DotProduct(CenterPoint, C.AxisV);
		CenterTileX[CascadeIndex] = static_cast<int64>(FMath::RoundToDouble(CenterU / TileWorld));
		CenterTileY[CascadeIndex] = static_cast<int64>(FMath::RoundToDouble(CenterV / TileWorld));
		C.OriginTileX = CenterTileX[CascadeIndex] - TilesPerSide / 2;
		C.OriginTileY = CenterTileY[CascadeIndex] - TilesPerSide / 2;
		C.CameraWindowTexel = FVector2d(
			CenterU / Texel - static_cast<double>(C.OriginTileX * TileSize),
			CenterV / Texel - static_cast<double>(C.OriginTileY * TileSize));

		// Storage tile s holds the global tile g of the window with g = s (mod TilesPerSide). A change of owner makes the
		// old content stale.
		for (int32 StorageY = 0; StorageY < TilesPerSide; ++StorageY)
		{
			const int64 GlobalY = C.OriginTileY + CloudShadowPositiveMod(StorageY - C.OriginTileY, TilesPerSide);
			for (int32 StorageX = 0; StorageX < TilesPerSide; ++StorageX)
			{
				const int64 GlobalX = C.OriginTileX + CloudShadowPositiveMod(StorageX - C.OriginTileX, TilesPerSide);
				const int32 Tile = StorageY * TilesPerSide + StorageX;
				if (C.HeldTileX[Tile] != GlobalX || C.HeldTileY[Tile] != GlobalY)
				{
					C.HeldTileX[Tile] = GlobalX;
					C.HeldTileY[Tile] = GlobalY;
					if (C.State[Tile] != ETileState::Invalid)
					{
						C.State[Tile] = ETileState::Stale;
					}
				}
			}
		}
	}

	// ---- Generation queue: nearest to the window centre first, round-robin over the cascades ----
	struct FCandidate
	{
		int32 Tile;
		int64 Distance2;
	};
	TArray<FCandidate> Candidates[NumCascades];
	int32 NumPending[NumCascades];
	for (int32 CascadeIndex = 0; CascadeIndex < NumCascades; ++CascadeIndex)
	{
		const FCascade& C = Entry.Cascades[CascadeIndex];
		for (int32 Tile = 0; Tile < TilesPerCascade; ++Tile)
		{
			if (C.State[Tile] != ETileState::Valid)
			{
				const int64 DX = C.HeldTileX[Tile] - CenterTileX[CascadeIndex];
				const int64 DY = C.HeldTileY[Tile] - CenterTileY[CascadeIndex];
				Candidates[CascadeIndex].Add({ Tile, DX * DX + DY * DY });
			}
		}
		Candidates[CascadeIndex].Sort([](const FCandidate& A, const FCandidate& B) { return A.Distance2 < B.Distance2; });
		NumPending[CascadeIndex] = Candidates[CascadeIndex].Num();
	}

	TArray<FVector4f> GenerateTiles;
	int32 Next[NumCascades] = {};
	const uint32 FirstCascade = Entry.RoundRobin++;
	while (GenerateTiles.Num() < Settings.UpdateBudgetTiles)
	{
		bool bAny = false;
		for (int32 Offset = 0; Offset < NumCascades && GenerateTiles.Num() < Settings.UpdateBudgetTiles; ++Offset)
		{
			const int32 CascadeIndex = static_cast<int32>((FirstCascade + static_cast<uint32>(Offset)) % NumCascades);
			if (Next[CascadeIndex] < Candidates[CascadeIndex].Num())
			{
				FCascade& C = Entry.Cascades[CascadeIndex];
				const int32 Tile = Candidates[CascadeIndex][Next[CascadeIndex]++].Tile;
				C.State[Tile] = ETileState::Valid;
				C.GeneratedFrame[Tile] = FrameNumber;
				GenerateTiles.Add(FVector4f(static_cast<float>(C.HeldTileX[Tile]), static_cast<float>(C.HeldTileY[Tile]),
					static_cast<float>(CascadeIndex), 1.0f));
				bAny = true;
			}
		}
		if (!bAny)
		{
			break;
		}
	}

	// Stale tiles not regenerated this frame: invalidated on the GPU (writes only, outside the generation budget).
	for (int32 CascadeIndex = 0; CascadeIndex < NumCascades; ++CascadeIndex)
	{
		FCascade& C = Entry.Cascades[CascadeIndex];
		for (int32 Tile = 0; Tile < TilesPerCascade; ++Tile)
		{
			if (C.State[Tile] == ETileState::Stale)
			{
				C.State[Tile] = ETileState::Invalid;
				InvalidateTiles.Add(FVector4f(static_cast<float>(C.HeldTileX[Tile]), static_cast<float>(C.HeldTileY[Tile]),
					static_cast<float>(CascadeIndex), 0.0f));
			}
		}
	}

	FillParameters(Entry, Index, OutParameters);

	// ---- Passes: whole-cascade clears, tile invalidations, then generation (RDG keeps this order: same UAV) ----
	if (ClearMask != 0 || InvalidateTiles.Num() > 0 || GenerateTiles.Num() > 0)
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereCloudShadowsGPU, "PlanetAtmosphere.CloudShadows");

		if (ClearMask != 0)
		{
			FAtmosphereCloudShadowClearCS::FParameters* ClearParameters = GraphBuilder.AllocParameters<FAtmosphereCloudShadowClearCS::FParameters>();
			ClearParameters->OutShadowAtlas = GraphBuilder.CreateUAV(Atlas);
			ClearParameters->ShadowAtlasResolution = Res;
			ClearParameters->ClearCascadeMask = ClearMask;
			TShaderMapRef<FAtmosphereCloudShadowClearCS> ClearShader(GlobalShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.CloudShadowClear mask %d", ClearMask),
				ClearShader,
				ClearParameters,
				FComputeShaderUtils::GetGroupCount(FIntPoint(Res, Res * NumCascades), FAtmosphereCloudShadowClearCS::ThreadGroupSize));
		}
		CloudShadowAddTilePasses(GraphBuilder, GlobalShaderMap, InstanceParameters, OutParameters, Inputs, Settings.GenerationSteps,
			Atlas, InvalidateTiles, TEXT("Invalidate"));
		CloudShadowAddTilePasses(GraphBuilder, GlobalShaderMap, InstanceParameters, OutParameters, Inputs, Settings.GenerationSteps,
			Atlas, GenerateTiles, TEXT("Generate"));

		UE_LOG(LogPlanetAtmosphere, Verbose,
			TEXT("Cloud shadows of view %u, frame %u: generated %d tiles, invalidated %d tiles + cascade mask %d, pending %d / %d / %d (budget %d), ")
			TEXT("cascade 0 half-size %.1f km, texel %.1f m"),
			ViewKey, FrameNumber, GenerateTiles.Num(), InvalidateTiles.Num(), ClearMask,
			NumPending[0] - Next[0], NumPending[1] - Next[1], NumPending[2] - Next[2], Settings.UpdateBudgetTiles,
			HalfSize0 * 1e-5, Entry.Cascades[0].TexelSize * 1e-2);
	}

	if (bNewAtlas)
	{
		// Immediate allocation + external: the atlas outlives this graph, its first passes are cull roots.
		Entry.Atlas = GraphBuilder.ConvertToExternalTexture(Atlas);
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("Cloud shadows of view %u: atlas %d x %d RGBA16F (%u bytes) [ComputeMemorySize], %d tiles per cascade"),
			ViewKey, Res, Res * NumCascades, Entry.Atlas.IsValid() ? Entry.Atlas->ComputeMemorySize() : 0u, TilesPerCascade);
	}
	return Atlas;
}

void FPlanetAtmosphereCloudShadows::CollectGarbage(uint32 FrameNumber)
{
	FScopeLock Lock(&Mutex);
	// Only strictly older frames: an entry used in this frame is referenced by passes of the current graph.
	for (auto It = Views.CreateIterator(); It; ++It)
	{
		const uint32 LastUsed = It.Value()->LastUsedFrame;
		if (FrameNumber > LastUsed && FrameNumber - LastUsed > CloudShadowMaxAgeFrames)
		{
			UE_LOG(LogPlanetAtmosphere, Log, TEXT("Cloud shadows of view %u released (unused for %u frames)"), It.Key(), FrameNumber - LastUsed);
			It.RemoveCurrent();
		}
	}
}

void FPlanetAtmosphereCloudShadows::ReleaseRHI()
{
	FScopeLock Lock(&Mutex);
	Views.Empty();
}

namespace PlanetAtmosphere::CloudShadows
{
	FPlanetAtmosphereCloudShadows& Get()
	{
		return GPlanetAtmosphereCloudShadows;
	}

	void SetNoCascades(FAtmosphereCloudShadowParameters& OutParameters)
	{
		OutParameters.CloudShadowPlanet = -1;
		OutParameters.CloudShadowResolution = 0;
		const FVector4f Zero(0.0f, 0.0f, 0.0f, 0.0f);
		for (int32 Cascade = 0; Cascade < NumCascades; ++Cascade)
		{
			OutParameters.CloudShadowAxisU[Cascade] = Zero;
			OutParameters.CloudShadowAxisV[Cascade] = Zero;
			OutParameters.CloudShadowSun[Cascade] = Zero;
			OutParameters.CloudShadowWindow[Cascade] = Zero;
		}
	}

	void Release_GameThread()
	{
		BeginReleaseResource(&GPlanetAtmosphereCloudShadows);
		FlushRenderingCommands();
	}
}
