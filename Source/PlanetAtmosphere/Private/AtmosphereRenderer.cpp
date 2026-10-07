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
#include "RHIStaticStates.h"

// `stat gpu` -> "PlanetAtmosphere" (noise bake + raymarch / debug pass of a view) and
// "PlanetAtmosphere.TransmittanceLut" (Step 13 LUT pass, measured separately). Total cost = sum of both.
// Distinct identifiers: the macros paste them into symbol names, and "PlanetAtmosphere" is also our namespace.
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereGPU, TEXT("PlanetAtmosphere"));
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereTransmittanceLutGPU, TEXT("PlanetAtmosphere.TransmittanceLut"));

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
			FRDGBuilder& GraphBuilder,
			const FSceneView& View,
			const FAtmospherePassSetup& Setup,
			FAtmosphereViewParameters& OutParameters)
		{
			OutParameters.SceneColorTexture = Setup.SceneColor.Texture;
			OutParameters.SceneDepthTexture = Setup.SceneDepthTexture;
			OutParameters.OutputTexture = GraphBuilder.CreateUAV(Setup.OutputTexture);
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
		 * Transmittance LUT atlas of the visible atmospheres (Step 13): transient, rebuilt every frame for every view
		 * (no caching yet, by design: correctness first, then measure). Row block i = planet i of InstanceParameters.
		 */
		FRDGTextureRef AddTransmittanceLutPass(
			FRDGBuilder& GraphBuilder,
			FGlobalShaderMap* GlobalShaderMap,
			const FAtmosphereInstanceParameters& InstanceParameters)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereTransmittanceLutGPU, "PlanetAtmosphere.TransmittanceLut");

			const int32 NumAtmospheres = FMath::Max(InstanceParameters.NumAtmospheres, 1);
			const FIntPoint AtlasSize(
				FAtmosphereTransmittanceLutCS::LutWidth,
				FAtmosphereTransmittanceLutCS::LutHeight * NumAtmospheres);

			const FRDGTextureDesc AtlasDesc = FRDGTextureDesc::Create2D(
				AtlasSize,
				PF_FloatRGBA,
				FClearValueBinding::Black,
				ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
			FRDGTextureRef Atlas = GraphBuilder.CreateTexture(AtlasDesc, TEXT("PlanetAtmosphere.TransmittanceLutAtlas"));

			FAtmosphereTransmittanceLutCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereTransmittanceLutCS::FParameters>();
			Parameters->AtmosphereParams = InstanceParameters;
			Parameters->OutTransmittanceLut = GraphBuilder.CreateUAV(Atlas);

			TShaderMapRef<FAtmosphereTransmittanceLutCS> ComputeShader(GlobalShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.TransmittanceLut %dx%d (%d atmospheres)", AtlasSize.X, AtlasSize.Y, NumAtmospheres),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(AtlasSize, FAtmosphereTransmittanceLutCS::ThreadGroupSize));
			return Atlas;
		}

		/** Sorts near -> far, truncates to MaxVisible and packs. All large-number differences in double. */
		void FillInstanceParameters(
			const FSceneView& View,
			const FIntRect& ViewRect,
			TArray<FAtmosphereVisibleInstance>& Instances,
			FAtmosphereInstanceParameters& OutParameters)
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
					0.0f);
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

		FAtmospherePassSetup Setup;
		if (!PrepareCommon(GraphBuilder, Inputs, Instances, Setup))
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		const CVars::EDebugMode DebugMode = CVars::GetDebugMode();
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

		if (DebugMode == CVars::EDebugMode::AtmosphereBounds)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereGPU, "PlanetAtmosphere");

			FAtmosphereBoundsDebugCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereBoundsDebugCS::FParameters>();
			FillViewParameters(GraphBuilder, View, Setup, Parameters->ViewParams);
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

		// Per-planet data is filled ONCE and shared by the LUT pass and the raymarch pass, so both see the same
		// near -> far order (planet i = atlas row block i).
		FAtmosphereCloudRaymarchCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereCloudRaymarchCS::FParameters>();
		FillInstanceParameters(View, Setup.ViewRect, Instances, Parameters->AtmosphereParams);

		// Step 13: transmittance LUT (own GPU stat). Built even with r.PlanetAtmosphere.Atmosphere 0, because the
		// raymarch shader always binds the atlas; the pass is tiny (256 x 64 texels x 40 steps per planet).
		FRDGTextureRef TransmittanceLutAtlas = AddTransmittanceLutPass(GraphBuilder, GlobalShaderMap, Parameters->AtmosphereParams);

		RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereGPU, "PlanetAtmosphere");

		// Shared noise textures (baked on first use). Unavailable only after module shutdown -> draw nothing
		// (the LUT pass then has no consumer and is culled by RDG).
		FPlanetAtmosphereNoiseTexturesRDG NoiseTextures;
		if (!GetNoiseTextures().GetOrBake(GraphBuilder, GlobalShaderMap, NoiseTextures))
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		Parameters->View = View.ViewUniformBuffer;
		FillViewParameters(GraphBuilder, View, Setup, Parameters->ViewParams);
		Parameters->DebugMode = static_cast<int32>(DebugMode);
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
		Parameters->TransmittanceLutAtlas = TransmittanceLutAtlas;
		Parameters->TransmittanceLutSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->bAtmosphereEnabled = CVars::IsAtmosphereEnabled() ? 1 : 0;
		Parameters->AtmosphereSteps = CVars::GetAtmosphereSteps();

		TShaderMapRef<FAtmosphereCloudRaymarchCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PlanetAtmosphere.CloudRaymarch %dx%d (%d atmospheres, mode %d, atmosphere %d)",
				Setup.ViewRect.Width(), Setup.ViewRect.Height(), Parameters->AtmosphereParams.NumAtmospheres, Parameters->DebugMode,
				Parameters->bAtmosphereEnabled),
			ComputeShader,
			Parameters,
			FComputeShaderUtils::GetGroupCount(Setup.ViewRect.Size(), FAtmosphereCloudRaymarchCS::ThreadGroupSize));

		return FScreenPassTexture(Setup.OutputTexture, Setup.ViewRect);
	}
}
