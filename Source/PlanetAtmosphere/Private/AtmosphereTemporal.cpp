// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereTemporal.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "GlobalShader.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "SceneManagement.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

// `stat gpu` -> "PlanetAtmosphere.Temporal" (Step 18, master prompt section 28).
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereTemporalGPU, TEXT("PlanetAtmosphere.Temporal"));

namespace
{
	/** Histories of views not rendered for this many frames are freed (a closed viewport, a finished PIE session). */
	constexpr uint32 TemporalHistoryMaxAgeFrames = 120;

	/** A history older than this many frames is not reused (temporal / the view mode was off meanwhile). */
	constexpr uint32 TemporalMaxFrameGap = 4;

	/**
	 * Interleave order (Step 19): ordered-dither matrices, value = position of the cell in the cycle (row-major cells,
	 * cell index = dy * N + dx). Same tables as the prototype (t19.py).
	 */
	constexpr int32 TemporalDither2[4] = { 0, 2, 3, 1 };
	constexpr int32 TemporalDither3[9] = { 0, 7, 3, 6, 5, 2, 4, 1, 8 };
	constexpr int32 TemporalDither4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

	FIntPoint TemporalInterleaveOffset(int32 Factor, uint32 Counter)
	{
		const int32 NumCells = Factor * Factor;
		// The order is rotated by a pseudo-random amount every cycle. A fixed order revisits a pixel every N^2 frames, so
		// if the engine's TAA / TSR jitter sequence length is a multiple of N^2 the pixel always gets the same sub-pixel
		// jitter phase(s) and the "salt" is never averaged (UE test: 3x3 sparkled on a far planet, 2x2 did not; TSR scales
		// its sample count with the upscale factor, e.g. 9 or 18). With the hashed rotation every pixel meets most phases of
		// any sequence length (simulated for lengths 8..32 and N = 2..4).
		const uint32 Cycle = Counter / static_cast<uint32>(NumCells);
		uint32 Shift = Cycle * 0x9E3779B1u;
		Shift ^= Shift >> 16;
		const uint32 Rank = (Counter % static_cast<uint32>(NumCells) + Shift) % static_cast<uint32>(NumCells);
		const int32* Table = Factor == 2 ? TemporalDither2 : (Factor == 3 ? TemporalDither3 : TemporalDither4);
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if (Table[Cell] == static_cast<int32>(Rank))
			{
				return FIntPoint(Cell % Factor, Cell / Factor);
			}
		}
		return FIntPoint(0, 0);
	}

	/** Row-vector 4x4 in double (UE convention: clip = [x y z 1] x M). Own multiply: no engine matrix operators needed. */
	struct FTemporalMat4d
	{
		double M[4][4] = {};
	};

	FTemporalMat4d TemporalMat4FromMatrix(const FMatrix& In)
	{
		FTemporalMat4d Out;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				Out.M[Row][Col] = In.M[Row][Col];
			}
		}
		return Out;
	}

	FTemporalMat4d TemporalMat4Multiply(const FTemporalMat4d& A, const FTemporalMat4d& B)
	{
		FTemporalMat4d Out;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				double Sum = 0.0;
				for (int32 K = 0; K < 4; ++K)
				{
					Sum += A.M[Row][K] * B.M[K][Col];
				}
				Out.M[Row][Col] = Sum;
			}
		}
		return Out;
	}

	/** [v 1] x Affine = v x R + T (rows 0..2 = R, row 3 = T). */
	FTemporalMat4d TemporalMakeAffine(const double R[3][3], const FVector3d& T)
	{
		FTemporalMat4d Out;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				Out.M[Row][Col] = R[Row][Col];
			}
		}
		Out.M[3][0] = T.X;
		Out.M[3][1] = T.Y;
		Out.M[3][2] = T.Z;
		Out.M[3][3] = 1.0;
		return Out;
	}

	void TemporalSetReprojectionSlot(FAtmosphereTemporalCS::FParameters& Parameters, int32 Slot, const FTemporalMat4d& M, float PrevSlot)
	{
		auto RowOf = [&M](int32 Row)
		{
			return FVector4f(static_cast<float>(M.M[Row][0]), static_cast<float>(M.M[Row][1]),
				static_cast<float>(M.M[Row][2]), static_cast<float>(M.M[Row][3]));
		};
		Parameters.ReprojRow0[Slot] = RowOf(0);
		Parameters.ReprojRow1[Slot] = RowOf(1);
		Parameters.ReprojRow2[Slot] = RowOf(2);
		Parameters.ReprojRow3[Slot] = RowOf(3);
		Parameters.PlanetPrevSlot[Slot] = FVector4f(PrevSlot, 0.0f, 0.0f, 0.0f);
	}

	double TemporalAxisComponent(const FVector3d& V, int32 Index)
	{
		return Index == 0 ? V.X : (Index == 1 ? V.Y : V.Z);
	}

	TGlobalResource<FPlanetAtmosphereTemporalHistory> GPlanetAtmosphereTemporalHistory;
}

FAtmosphereTemporalViewSetup FPlanetAtmosphereTemporalHistory::PrepareView(const FSceneView& View, int32 RequestedInterleaveFactor, int32 ImageType)
{
	FScopeLock Lock(&Mutex);

	FAtmosphereTemporalViewSetup Setup;
	if (!IsInitialized() || View.State == nullptr)
	{
		return Setup;
	}

	const uint32 FrameNumber = View.Family->FrameNumber;
	TUniquePtr<FViewHistory>& Entry = Views.FindOrAdd(View.State->GetViewKey());
	if (!Entry.IsValid())
	{
		Entry = MakeUnique<FViewHistory>();
	}
	FViewHistory& History = *Entry;

	// The same view state twice in one frame: its history is already being written -> no temporal for this render.
	if (History.bPrepared && History.LastPreparedFrame == FrameNumber)
	{
		return Setup;
	}
	History.bPrepared = true;
	History.LastPreparedFrame = FrameNumber;

	Setup.bEnabled = true;
	Setup.InterleaveFactor = FMath::Clamp(RequestedInterleaveFactor, 1, 4);

	// The history will be dropped (same conditions as in AddTemporalPass): trace every pixel this frame.
	const bool bHistoryUsable = History.bHasFrame && FrameNumber > History.LastUsedFrame
		&& FrameNumber - History.LastUsedFrame <= TemporalMaxFrameGap && History.ImageType == ImageType && !View.bCameraCut;
	if (!bHistoryUsable)
	{
		Setup.InterleaveFactor = 1;
	}
	if (Setup.InterleaveFactor > 1)
	{
		Setup.InterleaveOffset = TemporalInterleaveOffset(Setup.InterleaveFactor, History.InterleaveCounter);
		++History.InterleaveCounter;
	}
	return Setup;
}

bool FPlanetAtmosphereTemporalHistory::AddTemporalPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* GlobalShaderMap,
	const FSceneView& View,
	const FAtmosphereViewParameters& ViewParameters,
	const PlanetAtmosphere::CVars::FTemporalSettings& Settings,
	const FAtmosphereTemporalViewSetup& Setup,
	const FAtmosphereTemporalInputs& Inputs,
	FAtmosphereTemporalOutputs& OutOutputs)
{
	FScopeLock Lock(&Mutex);

	if (!IsInitialized() || !Setup.bEnabled || View.State == nullptr || !Inputs.Luminance || !Inputs.Transmittance
		|| Inputs.OutputExtent.X <= 0 || Inputs.OutputExtent.Y <= 0)
	{
		return false;
	}

	const uint32 ViewKey = View.State->GetViewKey();
	const uint32 FrameNumber = View.Family->FrameNumber;

	TUniquePtr<FViewHistory>& Entry = Views.FindOrAdd(ViewKey);
	if (!Entry.IsValid())
	{
		Entry = MakeUnique<FViewHistory>();
	}
	FViewHistory& History = *Entry;

	// ---- Current camera (unjittered projection: the history is aligned to pixel centers). Perspective only
	// (IsAllowedForView): HackAddTemporalAAProjectionJitter adds the TAA jitter (NDC) to ProjectionMatrix.M[2][0/1]. ----
	const FViewMatrices& Matrices = View.ViewMatrices;
	const FMatrix& Projection = Matrices.GetProjectionMatrix();
	const FMatrix& ProjectionNoAA = Matrices.GetProjectionNoAAMatrix();
	const FVector4f JitterNDC(
		static_cast<float>(Projection.M[2][0] - ProjectionNoAA.M[2][0]),
		static_cast<float>(Projection.M[2][1] - ProjectionNoAA.M[2][1]), 0.0f, 0.0f);
	const FMatrix& TranslatedView = Matrices.GetTranslatedViewMatrix();
	const FVector4f ViewForward(
		static_cast<float>(TranslatedView.M[0][2]), static_cast<float>(TranslatedView.M[1][2]), static_cast<float>(TranslatedView.M[2][2]), 0.0f);
	const FTemporalMat4d CurrentTranslatedViewProjectionNoAA = TemporalMat4Multiply(TemporalMat4FromMatrix(TranslatedView), TemporalMat4FromMatrix(ProjectionNoAA));
	const FVector3d ViewOrigin = Matrices.GetViewOrigin();
	const FVector3d PreViewTranslation = Matrices.GetPreViewTranslation();

	// ---- History validity ----
	// A different extent / view rect (dynamic resolution, screen percentage) is fine: the history is addressed through
	// its own rect. Dropped: camera cut, another image type (final vs atmosphere only), or a gap of more than
	// TemporalMaxFrameGap frames (temporal or the view mode was switched off meanwhile: the content is stale).
	const FIntPoint Extent = Inputs.OutputExtent;
	const bool bSameExtent = History.Extent.X == Extent.X && History.Extent.Y == Extent.Y;
	const bool bRecentFrame = History.bHasFrame && FrameNumber > History.LastUsedFrame && FrameNumber - History.LastUsedFrame <= TemporalMaxFrameGap;
	const bool bHistoryValid = bRecentFrame
		&& History.Luminance.IsValid() && History.Transmittance.IsValid() && History.Exposure.IsValid() && History.Slot.IsValid()
		&& History.ImageType == Inputs.ImageType && History.Extent.X > 0 && History.Extent.Y > 0
		&& History.ViewRect.Width() > 0 && History.ViewRect.Height() > 0 && !View.bCameraCut;

	if (!bSameExtent || !History.Luminance.IsValid())
	{
		// 2 x RGBA16F + R16F at the scene color extent (+ the same again while the next frame is written) + 1 x 1 exposure.
		// The interleave of the settings (the first frame of a new history always traces every pixel).
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("Temporal history of view %u: %d x %d, %.1f MB (x2 while a frame is written), interleave %dx%d"),
			ViewKey, Extent.X, Extent.Y, static_cast<double>(Extent.X) * Extent.Y * 18.0 / (1024.0 * 1024.0),
			Settings.InterleaveFactor, Settings.InterleaveFactor);
	}

	// ---- Textures ----
	const FRDGTextureDesc HistoryDesc = FRDGTextureDesc::Create2D(
		Extent, PF_FloatRGBA, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
	const FRDGTextureDesc ExposureDesc = FRDGTextureDesc::Create2D(
		FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
	FRDGTextureRef NewLuminance = GraphBuilder.CreateTexture(HistoryDesc, TEXT("PlanetAtmosphere.TemporalLuminance"));
	FRDGTextureRef NewTransmittance = GraphBuilder.CreateTexture(HistoryDesc, TEXT("PlanetAtmosphere.TemporalTransmittance"));
	FRDGTextureRef NewExposure = GraphBuilder.CreateTexture(ExposureDesc, TEXT("PlanetAtmosphere.TemporalExposure"));
	const FRDGTextureDesc SlotDesc = FRDGTextureDesc::Create2D(
		Extent, PF_R16F, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
	FRDGTextureRef NewSlot = GraphBuilder.CreateTexture(SlotDesc, TEXT("PlanetAtmosphere.TemporalSlot"));

	FAtmosphereTemporalCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereTemporalCS::FParameters>();
	Parameters->View = View.ViewUniformBuffer;
	Parameters->ViewParams = ViewParameters;
	Parameters->CurrentLuminance = Inputs.Luminance;
	Parameters->CurrentTransmittance = Inputs.Transmittance;
	// Without a valid history the current textures are bound in the history slots (never read: bHistoryValid = 0).
	Parameters->HistoryLuminance = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History.Luminance) : Inputs.Luminance;
	Parameters->HistoryTransmittance = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History.Transmittance) : Inputs.Transmittance;
	Parameters->HistoryExposure = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History.Exposure) : Inputs.Luminance;
	Parameters->HistorySlot = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History.Slot) : Inputs.Transmittance;
	Parameters->HistorySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->OutLuminance = GraphBuilder.CreateUAV(NewLuminance);
	Parameters->OutTransmittance = GraphBuilder.CreateUAV(NewTransmittance);
	Parameters->OutExposure = GraphBuilder.CreateUAV(NewExposure);
	Parameters->OutSlot = GraphBuilder.CreateUAV(NewSlot);
	Parameters->CurrentJitterNDC = JitterNDC;
	Parameters->CurrentViewForward = ViewForward;
	const FIntPoint HistoryExtent = bHistoryValid ? History.Extent : Extent;
	const FIntRect HistoryRect = bHistoryValid ? History.ViewRect : Inputs.ViewRect;
	Parameters->HistoryTextureSizeAndInvSize = FVector4f(
		static_cast<float>(HistoryExtent.X), static_cast<float>(HistoryExtent.Y),
		1.0f / static_cast<float>(HistoryExtent.X), 1.0f / static_cast<float>(HistoryExtent.Y));
	Parameters->PrevViewRectMinAndSize = FVector4f(
		static_cast<float>(HistoryRect.Min.X), static_cast<float>(HistoryRect.Min.Y),
		static_cast<float>(HistoryRect.Width()), static_cast<float>(HistoryRect.Height()));
	Parameters->bHistoryValid = bHistoryValid ? 1 : 0;
	Parameters->CurrentFrameWeight = Settings.CurrentFrameWeight;
	Parameters->ClampGamma = Settings.ClampGamma;
	Parameters->DepthRejectRatio = Settings.DepthRejectRatio;
	Parameters->InterleaveFactor = Setup.InterleaveFactor;
	Parameters->InterleaveOffsetX = Setup.InterleaveOffset.X;
	Parameters->InterleaveOffsetY = Setup.InterleaveOffset.Y;
	Parameters->StaticClampGamma = Settings.StaticClampGamma;
	Parameters->ClampMotionPixels = Settings.ClampMotionPixels;

	// ---- Reprojection matrices (double): current camera-relative point -> previous clip ----
	// Planet motion: p_prev = C_prev + (p - C_cur) x A_cur^T x A_prev (A = rows of the planet axes, row vectors), so with
	// p = ViewOrigin + v:  p_prev + PreViewTranslation_prev = v x R + (ViewOrigin - C_cur) x R + (C_prev + PreViewTranslation_prev),
	// R = A_cur^T x A_prev. Camera only: R = I, T = ViewOrigin + PreViewTranslation_prev. Then x the previous unjittered
	// translated view-projection.
	const int32 NumPlanets = FMath::Min(Inputs.Planets.Num(), PLANET_ATMOSPHERE_MAX_VISIBLE);
	const double Identity[3][3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
	FTemporalMat4d PrevViewProjection;
	FMemory::Memcpy(PrevViewProjection.M, History.TranslatedViewProjectionNoAA, sizeof(PrevViewProjection.M));
	const FTemporalMat4d CameraOnly = TemporalMat4Multiply(TemporalMakeAffine(Identity, ViewOrigin + History.PreViewTranslation), PrevViewProjection);

	for (int32 Slot = 0; Slot < PLANET_ATMOSPHERE_REPROJ_SLOTS; ++Slot)
	{
		// Unused slots and new planets: camera only, previous slot -1 (their pixels never match a history slot).
		TemporalSetReprojectionSlot(*Parameters, Slot, CameraOnly, Slot == PLANET_ATMOSPHERE_MAX_VISIBLE ? static_cast<float>(Slot) : -1.0f);
	}

	TArray<FPlanetState> CurrentPlanets;
	CurrentPlanets.Reserve(NumPlanets);
	for (int32 Slot = 0; Slot < NumPlanets; ++Slot)
	{
		const FAtmosphereVisibleInstance& Planet = Inputs.Planets[Slot];
		FPlanetState Current;
		Current.PlanetId = Planet.PlanetId;
		Current.Center = Planet.PlanetCenterWorld;
		Current.Axes[0] = Planet.PlanetAxisX;
		Current.Axes[1] = Planet.PlanetAxisY;
		Current.Axes[2] = Planet.PlanetAxisZ;
		CurrentPlanets.Add(Current);

		int32 PrevSlot = INDEX_NONE;
		for (int32 Candidate = 0; Candidate < History.Planets.Num(); ++Candidate)
		{
			if (History.Planets[Candidate].PlanetId == Planet.PlanetId)
			{
				PrevSlot = Candidate;
				break;
			}
		}
		if (PrevSlot == INDEX_NONE)
		{
			continue;
		}

		const FPlanetState& Previous = History.Planets[PrevSlot];
		double R[3][3];
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				R[Row][Col] = TemporalAxisComponent(Current.Axes[0], Row) * TemporalAxisComponent(Previous.Axes[0], Col)
					+ TemporalAxisComponent(Current.Axes[1], Row) * TemporalAxisComponent(Previous.Axes[1], Col)
					+ TemporalAxisComponent(Current.Axes[2], Row) * TemporalAxisComponent(Previous.Axes[2], Col);
			}
		}
		const FVector3d Offset = ViewOrigin - Current.Center;
		const FVector3d Translation(
			Offset.X * R[0][0] + Offset.Y * R[1][0] + Offset.Z * R[2][0] + (Previous.Center.X + History.PreViewTranslation.X),
			Offset.X * R[0][1] + Offset.Y * R[1][1] + Offset.Z * R[2][1] + (Previous.Center.Y + History.PreViewTranslation.Y),
			Offset.X * R[0][2] + Offset.Y * R[1][2] + Offset.Z * R[2][2] + (Previous.Center.Z + History.PreViewTranslation.Z));
		TemporalSetReprojectionSlot(*Parameters, Slot, TemporalMat4Multiply(TemporalMakeAffine(R, Translation), PrevViewProjection), static_cast<float>(PrevSlot));
	}

	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereTemporalGPU, "PlanetAtmosphere.Temporal");
		TShaderMapRef<FAtmosphereTemporalCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PlanetAtmosphere.Temporal %dx%d (history %d, interleave %d, offset %d %d)", Inputs.ViewRect.Width(), Inputs.ViewRect.Height(),
				bHistoryValid ? 1 : 0, Setup.InterleaveFactor, Setup.InterleaveOffset.X, Setup.InterleaveOffset.Y),
			ComputeShader,
			Parameters,
			FComputeShaderUtils::GetGroupCount(Inputs.ViewRect.Size(), FAtmosphereTemporalCS::ThreadGroupSize));
	}

	// ---- The new history (filled at graph execution) + what the next frame's reprojection needs ----
	GraphBuilder.QueueTextureExtraction(NewLuminance, &History.Luminance);
	GraphBuilder.QueueTextureExtraction(NewTransmittance, &History.Transmittance);
	GraphBuilder.QueueTextureExtraction(NewExposure, &History.Exposure);
	GraphBuilder.QueueTextureExtraction(NewSlot, &History.Slot);
	History.Extent = Extent;
	History.ViewRect = Inputs.ViewRect;
	FMemory::Memcpy(History.TranslatedViewProjectionNoAA, CurrentTranslatedViewProjectionNoAA.M, sizeof(History.TranslatedViewProjectionNoAA));
	History.PreViewTranslation = PreViewTranslation;
	History.ImageType = Inputs.ImageType;
	History.Planets = MoveTemp(CurrentPlanets);
	History.LastUsedFrame = FrameNumber;
	History.bHasFrame = true;

	OutOutputs.Luminance = NewLuminance;
	OutOutputs.Transmittance = NewTransmittance;
	return true;
}

void FPlanetAtmosphereTemporalHistory::CollectGarbage(uint32 FrameNumber)
{
	FScopeLock Lock(&Mutex);
	// Only strictly older frames: an entry used in this frame may have a pending extraction.
	for (auto It = Views.CreateIterator(); It; ++It)
	{
		const uint32 LastUsed = FMath::Max(It.Value()->LastUsedFrame, It.Value()->LastPreparedFrame);
		if (FrameNumber > LastUsed && FrameNumber - LastUsed > TemporalHistoryMaxAgeFrames)
		{
			UE_LOG(LogPlanetAtmosphere, Log, TEXT("Temporal history of view %u released (unused for %u frames)"), It.Key(), FrameNumber - LastUsed);
			It.RemoveCurrent();
		}
	}
}

void FPlanetAtmosphereTemporalHistory::ReleaseRHI()
{
	FScopeLock Lock(&Mutex);
	Views.Empty();
}

namespace PlanetAtmosphere::Temporal
{
	bool IsAllowedForView(const FSceneView& View)
	{
		// Perspective only: the raymarch rays fan out from the camera in every projection, which matches an orthographic
		// reprojection only on one plane (and infinite depth has no orthographic image).
		const bool bPerspective = View.ViewMatrices.GetProjectionNoAAMatrix().M[3][3] < 0.5;
		return View.State != nullptr && bPerspective && !View.bIsSceneCapture && !View.bIsReflectionCapture && !View.bIsPlanarReflection;
	}

	FPlanetAtmosphereTemporalHistory& GetHistory()
	{
		return GPlanetAtmosphereTemporalHistory;
	}

	void ReleaseHistory_GameThread()
	{
		BeginReleaseResource(&GPlanetAtmosphereTemporalHistory);
		FlushRenderingCommands();
	}
}
