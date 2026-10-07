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

bool FPlanetAtmosphereTemporalHistory::AddTemporalPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* GlobalShaderMap,
	const FSceneView& View,
	const FAtmosphereViewParameters& ViewParameters,
	const PlanetAtmosphere::CVars::FTemporalSettings& Settings,
	const FAtmosphereTemporalInputs& Inputs,
	FAtmosphereTemporalOutputs& OutOutputs)
{
	FScopeLock Lock(&Mutex);

	if (!IsInitialized() || View.State == nullptr || !Inputs.Luminance || !Inputs.Transmittance)
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

	// The same view state twice in one frame: its history is already being written -> no temporal for this render.
	if (History.bHasFrame && History.LastUsedFrame == FrameNumber)
	{
		return false;
	}

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
	const FIntPoint Extent = Inputs.Luminance->Desc.Extent;
	const bool bSameExtent = History.Extent.X == Extent.X && History.Extent.Y == Extent.Y;
	const bool bRecentFrame = History.bHasFrame && FrameNumber > History.LastUsedFrame && FrameNumber - History.LastUsedFrame <= TemporalMaxFrameGap;
	const bool bHistoryValid = bRecentFrame
		&& History.Luminance.IsValid() && History.Transmittance.IsValid() && History.Exposure.IsValid()
		&& History.ImageType == Inputs.ImageType && History.Extent.X > 0 && History.Extent.Y > 0
		&& History.ViewRect.Width() > 0 && History.ViewRect.Height() > 0 && !View.bCameraCut;

	if (!bSameExtent || !History.Luminance.IsValid())
	{
		// 2 x RGBA16F at the scene color extent (+ the same again while the next frame is written) + 1 x 1 exposure.
		UE_LOG(LogPlanetAtmosphere, Log, TEXT("Temporal history of view %u: %d x %d, %.1f MB (x2 while a frame is written)"),
			ViewKey, Extent.X, Extent.Y, static_cast<double>(Extent.X) * Extent.Y * 16.0 / (1024.0 * 1024.0));
	}

	// ---- Textures ----
	const FRDGTextureDesc HistoryDesc = FRDGTextureDesc::Create2D(
		Extent, PF_FloatRGBA, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
	const FRDGTextureDesc ExposureDesc = FRDGTextureDesc::Create2D(
		FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black, ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
	FRDGTextureRef NewLuminance = GraphBuilder.CreateTexture(HistoryDesc, TEXT("PlanetAtmosphere.TemporalLuminance"));
	FRDGTextureRef NewTransmittance = GraphBuilder.CreateTexture(HistoryDesc, TEXT("PlanetAtmosphere.TemporalTransmittance"));
	FRDGTextureRef NewExposure = GraphBuilder.CreateTexture(ExposureDesc, TEXT("PlanetAtmosphere.TemporalExposure"));

	FAtmosphereTemporalCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereTemporalCS::FParameters>();
	Parameters->View = View.ViewUniformBuffer;
	Parameters->ViewParams = ViewParameters;
	Parameters->CurrentLuminance = Inputs.Luminance;
	Parameters->CurrentTransmittance = Inputs.Transmittance;
	// Without a valid history the current textures are bound in the history slots (never read: bHistoryValid = 0).
	Parameters->HistoryLuminance = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History.Luminance) : Inputs.Luminance;
	Parameters->HistoryTransmittance = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History.Transmittance) : Inputs.Transmittance;
	Parameters->HistoryExposure = bHistoryValid ? GraphBuilder.RegisterExternalTexture(History.Exposure) : Inputs.Luminance;
	Parameters->HistorySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->OutLuminance = GraphBuilder.CreateUAV(NewLuminance);
	Parameters->OutTransmittance = GraphBuilder.CreateUAV(NewTransmittance);
	Parameters->OutExposure = GraphBuilder.CreateUAV(NewExposure);
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
			RDG_EVENT_NAME("PlanetAtmosphere.Temporal %dx%d (history %d)", Inputs.ViewRect.Width(), Inputs.ViewRect.Height(), bHistoryValid ? 1 : 0),
			ComputeShader,
			Parameters,
			FComputeShaderUtils::GetGroupCount(Inputs.ViewRect.Size(), FAtmosphereTemporalCS::ThreadGroupSize));
	}

	// ---- The new history (filled at graph execution) + what the next frame's reprojection needs ----
	GraphBuilder.QueueTextureExtraction(NewLuminance, &History.Luminance);
	GraphBuilder.QueueTextureExtraction(NewTransmittance, &History.Transmittance);
	GraphBuilder.QueueTextureExtraction(NewExposure, &History.Exposure);
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
		const uint32 LastUsed = It.Value()->LastUsedFrame;
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
