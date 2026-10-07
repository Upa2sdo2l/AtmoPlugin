// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereRenderer.h"
#include "AtmosphereShaders.h"
#include "AtmosphereCVars.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "SceneTexturesConfig.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "AtmosphereStats.h"
#include "AtmosphereNoiseTextures.h"
#include "AtmosphereLutCache.h"
#include "AtmosphereTemporal.h"
#include "AtmosphereCloudShadows.h"
#include "RHIStaticStates.h"

// `stat gpu` (Step 17 split, master prompt section 28):
//   "PlanetAtmosphere.Raymarch"  — noise bake + raymarch / bounds debug pass of a view (= the former "PlanetAtmosphere" stat);
//   "PlanetAtmosphere.Composite" — applying the raymarch result to the scene color;
//   "PlanetAtmosphere.TransmittanceLut" / "PlanetAtmosphere.MultipleScatteringLut" (AtmosphereLutCache.cpp) — only on
//   frames where a LUT is rebuilt (Step 16 cache);
//   "PlanetAtmosphere.Temporal" (AtmosphereTemporal.cpp), "PlanetAtmosphere.CloudShadows" (AtmosphereCloudShadows.cpp, Phase 4).
// Distinct identifiers: the macros paste them into symbol names, and "PlanetAtmosphere" is also our namespace.
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereRaymarchGPU, TEXT("PlanetAtmosphere.Raymarch"));
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereCompositeGPU, TEXT("PlanetAtmosphere.Composite"));

// `stat PlanetAtmosphere` (render thread CPU cost of building the passes).
DECLARE_CYCLE_STAT(TEXT("Setup Passes (RT)"), STAT_PlanetAtmosphere_SetupPasses, STATGROUP_PlanetAtmosphere);

namespace PlanetAtmosphere
{
	namespace
	{
		/** Resources and view data common to all PlanetAtmosphere passes of one view. */
		struct FAtmospherePassSetup
		{
			FScreenPassTexture SceneColor;
			FRDGTextureRef SceneDepthTexture = nullptr;
			FRDGTextureRef OutputTexture = nullptr;
			FIntRect ViewRect;
		};

		/** Returns false if the pass cannot / should not run for this view. */
		bool PrepareCommon(
			FRDGBuilder& GraphBuilder,
			const FPostProcessMaterialInputs& Inputs,
			const TArray<FAtmosphereVisibleInstance>& Instances,
			FAtmospherePassSetup& OutSetup)
		{
			const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);

			// BeforeDOF is never the last pass, so an override output is not expected; if one is ever
			// requested we do not draw rather than guess its format/flags.
			if (Instances.Num() == 0
				|| !SceneColorSlice.IsValid()
				|| Inputs.OverrideOutput.IsValid()
				|| !Inputs.SceneTextures.SceneTextures)
			{
				return false;
			}

			OutSetup.SceneDepthTexture = Inputs.SceneTextures.SceneTextures->GetContents()->SceneDepthTexture;
			if (!OutSetup.SceneDepthTexture)
			{
				return false;
			}

			// 2D texture view of the input (copies only if the input is a texture-array slice).
			OutSetup.SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice);
			OutSetup.ViewRect = OutSetup.SceneColor.ViewRect;
			if (OutSetup.ViewRect.Width() <= 0 || OutSetup.ViewRect.Height() <= 0)
			{
				return false;
			}

			// Always RGBA16F: a guaranteed typed-UAV format; downstream post-processing accepts any HDR color format.
			const FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
				OutSetup.SceneColor.Texture->Desc.Extent,
				PF_FloatRGBA,
				FClearValueBinding::Black,
				ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
			OutSetup.OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("PlanetAtmosphere.SceneColor"));
			return true;
		}

		void FillViewParameters(
			const FSceneView& View,
			const FAtmospherePassSetup& Setup,
			FAtmosphereViewParameters& OutParameters)
		{
			OutParameters.SceneDepthTexture = Setup.SceneDepthTexture;
			OutParameters.ClipToTranslatedWorld = FMatrix44f(View.ViewMatrices.GetInvTranslatedViewProjectionMatrix());
			OutParameters.CameraTranslatedWorld = FVector3f(View.ViewMatrices.GetViewOrigin() + View.ViewMatrices.GetPreViewTranslation());
			OutParameters.ViewRectMinAndSize = FVector4f(
				static_cast<float>(Setup.ViewRect.Min.X), static_cast<float>(Setup.ViewRect.Min.Y),
				static_cast<float>(Setup.ViewRect.Width()), static_cast<float>(Setup.ViewRect.Height()));
		}

		/**
		 * Lighting inputs (Step 7). Illuminance in lux. Ambient is a fraction of the sun
		 * (r.PlanetAtmosphere.CloudAmbientIntensity); without a sun, a neutral fallback keeps clouds visible.
		 */
		void FillLightingParameters(const FAtmosphereSunLight& Sun, FAtmosphereCloudRaymarchCS::FParameters& OutParameters)
		{
			constexpr float FallbackAmbientIlluminanceLux = 10.0f; // ~ the default Directional Light intensity
			const float AmbientFraction = CVars::GetCloudAmbientIntensity();

			if (Sun.bValid)
			{
				const FVector3f SunIlluminance(Sun.Illuminance.R, Sun.Illuminance.G, Sun.Illuminance.B);
				OutParameters.SunDirection = FVector3f(Sun.DirectionToSun.GetSafeNormal());
				OutParameters.SunIlluminance = SunIlluminance;
				OutParameters.AmbientIlluminance = SunIlluminance * AmbientFraction;
				OutParameters.bHasSun = 1;
			}
			else
			{
				OutParameters.SunDirection = FVector3f(0.0f, 0.0f, 1.0f);
				OutParameters.SunIlluminance = FVector3f(0.0f, 0.0f, 0.0f);
				OutParameters.AmbientIlluminance = FVector3f(FallbackAmbientIlluminanceLux * AmbientFraction);
				OutParameters.bHasSun = 0;
			}
			OutParameters.LightSteps = CVars::GetLightSteps();
		}

		/** Sample distribution, jitter and early exit of the cloud raymarch (Phase 2 / Step 9). */
		void FillMarchParameters(FAtmosphereCloudRaymarchCS::FParameters& OutParameters)
		{
			OutParameters.StepDistribution = static_cast<int32>(CVars::GetStepDistribution());
			OutParameters.StepNearDistanceScale = CVars::GetStepNearDistance();
			OutParameters.StepRatioMax = CVars::GetStepRatioMax();
			OutParameters.JitterMode = static_cast<int32>(CVars::GetJitterMode());
			OutParameters.EmptySpaceSkipSpan = CVars::GetEmptySpaceSkipSpan();
			OutParameters.MinTransmittance = CVars::GetMinTransmittance();
		}

		/**
		 * Radius of the atmosphere top sphere on screen, in pixels of the view rect (render resolution).
		 * Returns a huge value when the camera is inside the sphere (always full detail).
		 * Double precision: distances can be thousands of km.
		 */
		double ComputeScreenRadiusPx(const FSceneView& View, const FIntRect& ViewRect, const FVector3d& CameraRelativeToPlanet, double AtmosphereTopRadius)
		{
			const double Distance = CameraRelativeToPlanet.Length();
			if (Distance <= AtmosphereTopRadius * 1.0001)
			{
				return TNumericLimits<float>::Max();
			}

			// Projection: M[0][0] = horizontal scale (cot(half FOV) for perspective, 1 / half-width for orthographic);
			// UE perspective matrices have M[3][3] = 0, orthographic ones M[3][3] = 1.
			const FMatrix& Projection = View.ViewMatrices.GetProjectionMatrix();
			const double HalfWidthPx = 0.5 * static_cast<double>(ViewRect.Width());
			const bool bPerspective = Projection.M[3][3] < 0.5;
			if (!bPerspective)
			{
				return AtmosphereTopRadius * Projection.M[0][0] * HalfWidthPx;
			}

			// Angular radius of a sphere seen from outside: sin(a) = R / d -> tan(a) = R / sqrt(d^2 - R^2).
			const double TanAngularRadius = AtmosphereTopRadius / FMath::Sqrt(Distance * Distance - AtmosphereTopRadius * AtmosphereTopRadius);
			return TanAngularRadius * Projection.M[0][0] * HalfWidthPx;
		}

		/**
		 * Screen-space LOD (Phase 2 / Step 11, CPU prototype): detail is interpolated in log2(radius) between
		 * MinDetailRadius (minimum) and FullDetailRadius (full RaymarchSteps / LightSteps).
		 */
		void ApplyScreenLOD(const CVars::FScreenLODSettings& LOD, double RadiusPx, int32 FullRaymarchSteps, int32 FullLightSteps,
			int32& OutRaymarchSteps, int32& OutLightSteps)
		{
			OutRaymarchSteps = FullRaymarchSteps;
			OutLightSteps = FullLightSteps;
			if (!LOD.bEnabled || RadiusPx >= LOD.FullDetailRadiusPx)
			{
				return;
			}

			const double Detail = FMath::Clamp(
				FMath::Log2(FMath::Max(RadiusPx, 1e-3) / LOD.MinDetailRadiusPx) / FMath::Log2(LOD.FullDetailRadiusPx / LOD.MinDetailRadiusPx),
				0.0, 1.0);
			const double StepFraction = FMath::Lerp(static_cast<double>(LOD.MinStepFraction), 1.0, Detail);
			const int32 MinRaymarchSteps = FMath::Min(4, FullRaymarchSteps);
			OutRaymarchSteps = FMath::Clamp(FMath::RoundToInt32(FullRaymarchSteps * StepFraction), MinRaymarchSteps, FullRaymarchSteps);

			const int32 MinLightSteps = FMath::Min(LOD.MinLightSteps, FullLightSteps);
			OutLightSteps = FMath::Clamp(
				FMath::RoundToInt32(FMath::Lerp(static_cast<double>(MinLightSteps), static_cast<double>(FullLightSteps), Detail)),
				MinLightSteps, FullLightSteps);
		}

		FVector4f ToAxis4f(const FVector3d& Axis)
		{
			return FVector4f(static_cast<float>(Axis.X), static_cast<float>(Axis.Y), static_cast<float>(Axis.Z), 0.0f);
		}

		FVector4f ToVector4f(const FVector3f& XYZ, float W)
		{
			return FVector4f(XYZ.X, XYZ.Y, XYZ.Z, W);
		}

		/**
		 * Sorts near -> far, truncates to MaxVisible and packs. All large-number differences in double.
		 * OutScreenRadiiPx (optional): radius on screen of every packed atmosphere (Step 11 LOD input), same order.
		 */
		void FillInstanceParameters(
			const FSceneView& View,
			const FIntRect& ViewRect,
			TArray<FAtmosphereVisibleInstance>& Instances,
			FAtmosphereInstanceParameters& OutParameters,
			TArray<double>* OutScreenRadiiPx = nullptr)
		{
			const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();
			const CVars::FScreenLODSettings LOD = CVars::GetScreenLODSettings();
			const int32 FullLightSteps = CVars::GetLightSteps();

			// Near -> far by distance to the atmosphere top sphere (negative when the camera is inside).
			Instances.Sort([&ViewOrigin](const FAtmosphereVisibleInstance& A, const FAtmosphereVisibleInstance& B)
			{
				const double DistA = FVector::Distance(ViewOrigin, A.PlanetCenterWorld) - A.RadiiUU.AtmosphereTop;
				const double DistB = FVector::Distance(ViewOrigin, B.PlanetCenterWorld) - B.RadiiUU.AtmosphereTop;
				return DistA < DistB;
			});

			const int32 NumAtmospheres = FMath::Min(Instances.Num(), CVars::GetMaxVisible());
			OutParameters.NumAtmospheres = NumAtmospheres;

			const FVector4f Zero(0.0f, 0.0f, 0.0f, 0.0f);
			for (int32 Index = 0; Index < PLANET_ATMOSPHERE_MAX_VISIBLE; ++Index)
			{
				if (Index >= NumAtmospheres)
				{
					OutParameters.AtmosphereData0[Index] = Zero;
					OutParameters.AtmosphereData1[Index] = Zero;
					OutParameters.AtmosphereData2[Index] = Zero;
					OutParameters.AtmosphereData3[Index] = Zero;
					OutParameters.AtmosphereAxisX[Index] = Zero;
					OutParameters.AtmosphereAxisY[Index] = Zero;
					OutParameters.AtmosphereAxisZ[Index] = Zero;
					OutParameters.AtmosphereRayleigh[Index] = Zero;
					OutParameters.AtmosphereMieScattering[Index] = Zero;
					OutParameters.AtmosphereMieAbsorption[Index] = Zero;
					OutParameters.AtmosphereOzone[Index] = Zero;
					OutParameters.AtmosphereSurface[Index] = Zero;
					OutParameters.AtmosphereLutInfo[Index] = Zero;
					continue;
				}

				const FAtmosphereVisibleInstance& Instance = Instances[Index];
				const FPlanetAtmosphereRadii& R = Instance.RadiiUU;

				// At Earth scale a float position has ~64 cm granularity, so the shader must never derive
				// altitudes itself: camera offset and all altitudes are computed here in double.
				const FVector3d CameraRelativeToPlanet = ViewOrigin - Instance.PlanetCenterWorld;
				const double CameraAltitude = CameraRelativeToPlanet.Length() - R.Planet;

				// Screen-space LOD (Step 11).
				const double RadiusPx = ComputeScreenRadiusPx(View, ViewRect, CameraRelativeToPlanet, R.AtmosphereTop);
				if (OutScreenRadiiPx)
				{
					OutScreenRadiiPx->Add(RadiusPx);
				}
				int32 RaymarchSteps = Instance.RaymarchSteps;
				int32 LightSteps = FullLightSteps;
				ApplyScreenLOD(LOD, RadiusPx, Instance.RaymarchSteps, FullLightSteps, RaymarchSteps, LightSteps);
				UE_LOG(LogPlanetAtmosphere, VeryVerbose, TEXT("Atmosphere %d: screen radius %.0f px -> raymarch steps %d / %d, light steps %d / %d"),
					Index, RadiusPx, RaymarchSteps, Instance.RaymarchSteps, LightSteps, FullLightSteps);

				const double ExtinctionPerCm =
					static_cast<double>(Instance.CloudDensity) * BaseCloudExtinctionPerMeter / MetersToUnrealUnits;

				OutParameters.AtmosphereData0[Index] = FVector4f(
					FVector3f(CameraRelativeToPlanet),
					static_cast<float>(R.Planet));
				OutParameters.AtmosphereData1[Index] = FVector4f(
					static_cast<float>(CameraAltitude),
					static_cast<float>(R.AtmosphereBottom - R.Planet),
					static_cast<float>(R.AtmosphereTop - R.Planet),
					static_cast<float>(R.CloudBottom - R.Planet));
				OutParameters.AtmosphereData2[Index] = FVector4f(
					static_cast<float>(R.CloudTop - R.Planet),
					Instance.CloudCoverage,
					static_cast<float>(ExtinctionPerCm),
					static_cast<float>(Instance.CloudShapeScaleUU));
				OutParameters.AtmosphereData3[Index] = FVector4f(
					Instance.CloudErosion,
					static_cast<float>(RaymarchSteps),
					static_cast<float>(LightSteps),
					Instance.CloudSkyAmbientScale);
				OutParameters.AtmosphereAxisX[Index] = ToAxis4f(Instance.PlanetAxisX);
				OutParameters.AtmosphereAxisY[Index] = ToAxis4f(Instance.PlanetAxisY);
				OutParameters.AtmosphereAxisZ[Index] = ToAxis4f(Instance.PlanetAxisZ);

				// Atmosphere scattering (Step 13): already validated and in cm / 1/cm (UPlanetAtmosphereComponent).
				const FPlanetAtmosphereScattering& S = Instance.ScatteringUU;
				OutParameters.AtmosphereRayleigh[Index] = ToVector4f(S.RayleighScattering, S.RayleighScaleHeight);
				OutParameters.AtmosphereMieScattering[Index] = ToVector4f(S.MieScattering, S.MieScaleHeight);
				OutParameters.AtmosphereMieAbsorption[Index] = ToVector4f(S.MieAbsorption, S.MieAnisotropy);
				OutParameters.AtmosphereOzone[Index] = ToVector4f(S.OzoneAbsorption, S.OzoneLayerAltitude);
				OutParameters.AtmosphereSurface[Index] = ToVector4f(S.SurfaceAlbedo, S.OzoneLayerWidth);

				// LUT pool slot: assigned by the LUT cache (Step 16) for the raymarch pass; 0 for the bounds debug pass.
				OutParameters.AtmosphereLutInfo[Index] = Zero;
			}
		}
	}

	FScreenPassTexture AddAtmospherePasses(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs,
		TArray<FAtmosphereVisibleInstance>& Instances,
		const FAtmosphereSunLight& Sun)
	{
		SCOPE_CYCLE_COUNTER(STAT_PlanetAtmosphere_SetupPasses);

		// Step 18 / 22: free the temporal histories and cloud shadow cascades of views that stopped rendering (runs even with
		// nothing to draw).
		Temporal::GetHistory().CollectGarbage(View.Family->FrameNumber);
		CloudShadows::Get().CollectGarbage(View.Family->FrameNumber);

		FAtmospherePassSetup Setup;
		if (!PrepareCommon(GraphBuilder, Inputs, Instances, Setup))
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		const CVars::EDebugMode DebugMode = CVars::GetDebugMode();
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

		if (DebugMode == CVars::EDebugMode::AtmosphereBounds)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereRaymarchGPU, "PlanetAtmosphere.Raymarch");

			FAtmosphereBoundsDebugCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereBoundsDebugCS::FParameters>();
			FillViewParameters(View, Setup, Parameters->ViewParams);
			Parameters->SceneColorTexture = Setup.SceneColor.Texture;
			Parameters->OutputTexture = GraphBuilder.CreateUAV(Setup.OutputTexture);
			FillInstanceParameters(View, Setup.ViewRect, Instances, Parameters->AtmosphereParams);
			Parameters->DebugIntensity = CVars::GetDebugIntensity();

			TShaderMapRef<FAtmosphereBoundsDebugCS> ComputeShader(GlobalShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.BoundsDebug %dx%d (%d atmospheres)",
					Setup.ViewRect.Width(), Setup.ViewRect.Height(), Parameters->AtmosphereParams.NumAtmospheres),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(Setup.ViewRect.Size(), FAtmosphereBoundsDebugCS::ThreadGroupSize));
			return FScreenPassTexture(Setup.OutputTexture, Setup.ViewRect);
		}

		// Shared noise textures (baked on first use). Unavailable only after module shutdown -> draw nothing.
		// Acquired BEFORE the LUT cache: once Prepare() has queued LUT rebuilds, the raymarch pass below must consume them.
		FPlanetAtmosphereNoiseTexturesRDG NoiseTextures;
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereRaymarchGPU, "PlanetAtmosphere.Raymarch");
			if (!GetNoiseTextures().GetOrBake(GraphBuilder, GlobalShaderMap, NoiseTextures))
			{
				return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			}
		}

		FAtmosphereCloudRaymarchCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereCloudRaymarchCS::FParameters>();
		TArray<double> ScreenRadiiPx;
		FillInstanceParameters(View, Setup.ViewRect, Instances, Parameters->AtmosphereParams, &ScreenRadiiPx);

		// Step 16: LUTs cached across frames (AtmosphereLutCache.h). Assigns each planet its pool slot
		// (AtmosphereLutInfo) and rebuilds only LUTs that are missing (first use, changed parameters, new planet).
		// The transmittance LUT is always prepared (always bound; Step 13), the multiple-scattering LUT (Step 14) only
		// when the atmosphere and multiple scattering are on.
		const bool bAtmosphereEnabled = CVars::IsAtmosphereEnabled();
		const bool bMultipleScattering = bAtmosphereEnabled && CVars::IsMultipleScatteringEnabled();
		FPlanetAtmosphereLutsRDG Luts;
		if (!GetLutCache().Prepare(GraphBuilder, GlobalShaderMap, View.Family->FrameNumber, bMultipleScattering,
			!CVars::IsLutCacheEnabled(), Parameters->AtmosphereParams, Luts))
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		// Phase 4 / Step 22: cloud shadow cascades of the primary planet (largest on screen, with hysteresis), updated
		// progressively (AtmosphereCloudShadows.h).
		// Before the raymarch, which always binds the atlas (so these passes are never culled); without cascades the
		// transmittance pool is bound in its slot (CloudShadowPlanet = -1: never sampled).
		{
			FAtmosphereCloudShadowInputs ShadowInputs;
			ShadowInputs.Planets = TConstArrayView<FAtmosphereVisibleInstance>(Instances.GetData(), Parameters->AtmosphereParams.NumAtmospheres);
			ShadowInputs.ScreenRadiiPx = ScreenRadiiPx;
			ShadowInputs.Sun = Sun;
			ShadowInputs.Noise = NoiseTextures;
			const FRDGTextureRef CloudShadowAtlas = CloudShadows::Get().Update(
				GraphBuilder, GlobalShaderMap, View, Parameters->AtmosphereParams, ShadowInputs, Parameters->CloudShadowParams);
			Parameters->CloudShadowAtlas = CloudShadowAtlas ? CloudShadowAtlas : Luts.Transmittance;
		}

		// DebugMode 8 (Temporal Weight) marches like the final image; the composite overlays the temporal weight.
		const bool bTemporalWeightView = DebugMode == CVars::EDebugMode::TemporalWeight;
		const CVars::EDebugMode MarchDebugMode = bTemporalWeightView ? CVars::EDebugMode::FinalClouds : DebugMode;

		// Step 18 / 19: temporal accumulation for the final and atmosphere-only images (the other debug views stay per
		// frame). Decided before the raymarch: with interleaving (Step 19) the raymarch traces one pixel per N x N block at
		// the offset of this frame, and the temporal pass reconstructs the full image.
		const CVars::FTemporalSettings TemporalSettings = CVars::GetTemporalSettings();
		FAtmosphereTemporalViewSetup TemporalSetup;
		if (TemporalSettings.bEnabled
			&& (MarchDebugMode == CVars::EDebugMode::FinalClouds || MarchDebugMode == CVars::EDebugMode::AtmosphereOnly)
			&& Temporal::IsAllowedForView(View))
		{
			TemporalSetup = Temporal::GetHistory().PrepareView(View, TemporalSettings.InterleaveFactor, static_cast<int32>(MarchDebugMode));
		}
		const int32 InterleaveFactor = TemporalSetup.bEnabled ? TemporalSetup.InterleaveFactor : 1;
		const FIntPoint MarchThreads(
			FMath::DivideAndRoundUp(Setup.ViewRect.Width(), InterleaveFactor),
			FMath::DivideAndRoundUp(Setup.ViewRect.Height(), InterleaveFactor));

		// Step 17: the raymarch writes its own luminance / transmittance buffers (N = 1: same extent and pixel coordinates
		// as the scene color; N > 1: one texel per block); the composite pass below applies them (through the temporal
		// pass when it runs). RGBA16F: guaranteed typed-UAV format; alpha = reprojection data (Step 18).
		const FRDGTextureDesc RaymarchOutputDesc = FRDGTextureDesc::Create2D(
			InterleaveFactor == 1 ? Setup.SceneColor.Texture->Desc.Extent : MarchThreads,
			PF_FloatRGBA,
			FClearValueBinding::Black,
			ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
		FRDGTextureRef LuminanceTexture = GraphBuilder.CreateTexture(RaymarchOutputDesc, TEXT("PlanetAtmosphere.Luminance"));
		FRDGTextureRef TransmittanceTexture = GraphBuilder.CreateTexture(RaymarchOutputDesc, TEXT("PlanetAtmosphere.Transmittance"));

		// LUT rebuilds above are measured by their own stats, outside these scopes (as before Step 16).
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereRaymarchGPU, "PlanetAtmosphere.Raymarch");

			Parameters->View = View.ViewUniformBuffer;
			FillViewParameters(View, Setup, Parameters->ViewParams);
			Parameters->OutLuminance = GraphBuilder.CreateUAV(LuminanceTexture);
			Parameters->OutTransmittance = GraphBuilder.CreateUAV(TransmittanceTexture);
			Parameters->InterleaveFactor = InterleaveFactor;
			Parameters->InterleaveOffsetX = TemporalSetup.InterleaveOffset.X;
			Parameters->InterleaveOffsetY = TemporalSetup.InterleaveOffset.Y;
			Parameters->DebugMode = static_cast<int32>(MarchDebugMode);
			Parameters->bDrawPlanetSurface = CVars::ShouldDrawPlanetSurface() ? 1 : 0;
			FillLightingParameters(Sun, *Parameters);
			FillMarchParameters(*Parameters);
			Parameters->BaseNoiseTexture = NoiseTextures.BaseShape;
			Parameters->ErosionNoiseTexture = NoiseTextures.Erosion;
			Parameters->NoiseSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			Parameters->NoiseSource = static_cast<int32>(CVars::GetNoiseSource());
			Parameters->NoiseFootprintScale = CVars::GetNoiseFootprintScale();
			const CVars::FLightLODSettings LightLOD = CVars::GetLightLODSettings();
			Parameters->LightLODEnable = LightLOD.bEnabled ? 1 : 0;
			Parameters->LightLODFullFootprint = LightLOD.FullDetailFootprint;
			Parameters->LightLODMinFootprint = LightLOD.MinDetailFootprint;
			Parameters->LightLODMinSteps = LightLOD.MinLightSteps;
			Parameters->TransmittanceLutAtlas = Luts.Transmittance;
			Parameters->TransmittanceLutSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->bAtmosphereEnabled = bAtmosphereEnabled ? 1 : 0;
			Parameters->AtmosphereSteps = CVars::GetAtmosphereSteps();
			Parameters->MultipleScatteringLutAtlas = Luts.MultipleScattering;
			Parameters->bMultipleScattering = bMultipleScattering ? 1 : 0;
			Parameters->CloudSkyAmbientScaleMultiplier = CVars::GetCloudSkyAmbientScaleMultiplier();

			TShaderMapRef<FAtmosphereCloudRaymarchCS> ComputeShader(GlobalShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.CloudRaymarch %dx%d (%d atmospheres, mode %d, atmosphere %d, ms %d, interleave %d)",
					MarchThreads.X, MarchThreads.Y, Parameters->AtmosphereParams.NumAtmospheres, Parameters->DebugMode,
					Parameters->bAtmosphereEnabled, Parameters->bMultipleScattering, InterleaveFactor),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(MarchThreads, FAtmosphereCloudRaymarchCS::ThreadGroupSize));
		}

		FRDGTextureRef CompositeLuminance = LuminanceTexture;
		FRDGTextureRef CompositeTransmittance = TransmittanceTexture;
		bool bTemporalApplied = false;
		if (TemporalSetup.bEnabled)
		{
			FAtmosphereViewParameters TemporalViewParameters;
			FillViewParameters(View, Setup, TemporalViewParameters);

			FAtmosphereTemporalInputs TemporalInputs;
			TemporalInputs.Luminance = LuminanceTexture;
			TemporalInputs.Transmittance = TransmittanceTexture;
			TemporalInputs.ViewRect = Setup.ViewRect;
			TemporalInputs.OutputExtent = Setup.SceneColor.Texture->Desc.Extent;
			TemporalInputs.ImageType = static_cast<int32>(MarchDebugMode);
			TemporalInputs.Planets = TConstArrayView<FAtmosphereVisibleInstance>(Instances.GetData(), Parameters->AtmosphereParams.NumAtmospheres);

			FAtmosphereTemporalOutputs TemporalOutputs;
			if (Temporal::GetHistory().AddTemporalPass(GraphBuilder, GlobalShaderMap, View, TemporalViewParameters, TemporalSettings,
				TemporalSetup, TemporalInputs, TemporalOutputs))
			{
				CompositeLuminance = TemporalOutputs.Luminance;
				CompositeTransmittance = TemporalOutputs.Transmittance;
				bTemporalApplied = true;
			}
			else if (InterleaveFactor > 1)
			{
				// Only if the history was released meanwhile (shutdown): the reduced raymarch output cannot be composited.
				return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			}
		}

		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereCompositeGPU, "PlanetAtmosphere.Composite");

			FAtmosphereCompositeCS::FParameters* CompositeParameters = GraphBuilder.AllocParameters<FAtmosphereCompositeCS::FParameters>();
			CompositeParameters->SceneColorTexture = Setup.SceneColor.Texture;
			CompositeParameters->LuminanceTexture = CompositeLuminance;
			CompositeParameters->TransmittanceTexture = CompositeTransmittance;
			CompositeParameters->OutputTexture = GraphBuilder.CreateUAV(Setup.OutputTexture);
			CompositeParameters->ViewRectMinAndSize = FVector4f(
				static_cast<float>(Setup.ViewRect.Min.X), static_cast<float>(Setup.ViewRect.Min.Y),
				static_cast<float>(Setup.ViewRect.Width()), static_cast<float>(Setup.ViewRect.Height()));
			CompositeParameters->bShowTemporalWeight = (bTemporalWeightView && bTemporalApplied) ? 1 : 0;
			CompositeParameters->CurrentFrameWeight = TemporalSettings.CurrentFrameWeight;

			TShaderMapRef<FAtmosphereCompositeCS> CompositeShader(GlobalShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.Composite %dx%d", Setup.ViewRect.Width(), Setup.ViewRect.Height()),
				CompositeShader,
				CompositeParameters,
				FComputeShaderUtils::GetGroupCount(Setup.ViewRect.Size(), FAtmosphereCompositeCS::ThreadGroupSize));
		}

		return FScreenPassTexture(Setup.OutputTexture, Setup.ViewRect);
	}
}
