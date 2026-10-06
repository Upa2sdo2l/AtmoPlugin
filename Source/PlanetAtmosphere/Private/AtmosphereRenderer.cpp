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

// `stat gpu` -> "PlanetAtmosphere" (all plugin passes of a view are inside this scope).
// Distinct identifier: the macros paste it into symbol names, and "PlanetAtmosphere" is also our namespace.
DECLARE_GPU_STAT_NAMED(PlanetAtmosphereGPU, TEXT("PlanetAtmosphere"));

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

		FVector4f ToAxis4f(const FVector3d& Axis)
		{
			return FVector4f(static_cast<float>(Axis.X), static_cast<float>(Axis.Y), static_cast<float>(Axis.Z), 0.0f);
		}

		/** Sorts near -> far, truncates to MaxVisible and packs. All large-number differences in double. */
		void FillInstanceParameters(
			const FSceneView& View,
			TArray<FAtmosphereVisibleInstance>& Instances,
			FAtmosphereInstanceParameters& OutParameters)
		{
			const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();

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
					continue;
				}

				const FAtmosphereVisibleInstance& Instance = Instances[Index];
				const FPlanetAtmosphereRadii& R = Instance.RadiiUU;

				// At Earth scale a float position has ~64 cm granularity, so the shader must never derive
				// altitudes itself: camera offset and all altitudes are computed here in double.
				const FVector3d CameraRelativeToPlanet = ViewOrigin - Instance.PlanetCenterWorld;
				const double CameraAltitude = CameraRelativeToPlanet.Length() - R.Planet;

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
					static_cast<float>(Instance.RaymarchSteps),
					0.0f,
					0.0f);
				OutParameters.AtmosphereAxisX[Index] = ToAxis4f(Instance.PlanetAxisX);
				OutParameters.AtmosphereAxisY[Index] = ToAxis4f(Instance.PlanetAxisY);
				OutParameters.AtmosphereAxisZ[Index] = ToAxis4f(Instance.PlanetAxisZ);
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

		RDG_EVENT_SCOPE_STAT(GraphBuilder, PlanetAtmosphereGPU, "PlanetAtmosphere");

		const CVars::EDebugMode DebugMode = CVars::GetDebugMode();
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

		if (DebugMode == CVars::EDebugMode::AtmosphereBounds)
		{
			FAtmosphereBoundsDebugCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereBoundsDebugCS::FParameters>();
			FillViewParameters(GraphBuilder, View, Setup, Parameters->ViewParams);
			FillInstanceParameters(View, Instances, Parameters->AtmosphereParams);
			Parameters->DebugIntensity = CVars::GetDebugIntensity();

			TShaderMapRef<FAtmosphereBoundsDebugCS> ComputeShader(GlobalShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.BoundsDebug %dx%d (%d atmospheres)",
					Setup.ViewRect.Width(), Setup.ViewRect.Height(), Parameters->AtmosphereParams.NumAtmospheres),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(Setup.ViewRect.Size(), FAtmosphereBoundsDebugCS::ThreadGroupSize));
		}
		else
		{
			FAtmosphereCloudRaymarchCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereCloudRaymarchCS::FParameters>();
			Parameters->View = View.ViewUniformBuffer;
			FillViewParameters(GraphBuilder, View, Setup, Parameters->ViewParams);
			FillInstanceParameters(View, Instances, Parameters->AtmosphereParams);
			Parameters->DebugMode = static_cast<int32>(DebugMode);
			Parameters->bDrawPlanetSurface = CVars::ShouldDrawPlanetSurface() ? 1 : 0;
			FillLightingParameters(Sun, *Parameters);

			TShaderMapRef<FAtmosphereCloudRaymarchCS> ComputeShader(GlobalShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.CloudRaymarch %dx%d (%d atmospheres, mode %d)",
					Setup.ViewRect.Width(), Setup.ViewRect.Height(), Parameters->AtmosphereParams.NumAtmospheres, Parameters->DebugMode),
				ComputeShader,
				Parameters,
				FComputeShaderUtils::GetGroupCount(Setup.ViewRect.Size(), FAtmosphereCloudRaymarchCS::ThreadGroupSize));
		}

		return FScreenPassTexture(Setup.OutputTexture, Setup.ViewRect);
	}
}
