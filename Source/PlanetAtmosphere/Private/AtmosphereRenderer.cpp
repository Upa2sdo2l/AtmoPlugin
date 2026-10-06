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

namespace PlanetAtmosphere
{
	FScreenPassTexture AddAtmosphereBoundsDebugPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs,
		TArray<FAtmosphereVisibleInstance>& Instances)
	{
		const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);

		// BeforeDOF is never the last pass, so an override output is not expected; if one is ever
		// requested we do not draw rather than guess its format/flags.
		if (Instances.Num() == 0
			|| !SceneColorSlice.IsValid()
			|| Inputs.OverrideOutput.IsValid()
			|| !Inputs.SceneTextures.SceneTextures)
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		FRDGTextureRef SceneDepthTexture = Inputs.SceneTextures.SceneTextures->GetContents()->SceneDepthTexture;
		if (!SceneDepthTexture)
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		// 2D texture view of the input (copies only if the input is a texture-array slice).
		const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice);
		const FIntRect ViewRect = SceneColor.ViewRect;
		if (ViewRect.Width() <= 0 || ViewRect.Height() <= 0)
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}

		// --- CPU side, double precision ---------------------------------------------------------
		const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();

		// Near -> far by distance to the atmosphere top sphere (negative when the camera is inside).
		Instances.Sort([&ViewOrigin](const FAtmosphereVisibleInstance& A, const FAtmosphereVisibleInstance& B)
		{
			const double DistA = FVector::Distance(ViewOrigin, A.PlanetCenterWorld) - A.RadiiUU.AtmosphereTop;
			const double DistB = FVector::Distance(ViewOrigin, B.PlanetCenterWorld) - B.RadiiUU.AtmosphereTop;
			return DistA < DistB;
		});
		const int32 NumAtmospheres = FMath::Min(Instances.Num(), CVars::GetMaxVisible());

		// --- Output texture ------------------------------------------------------------------------
		// Always RGBA16F: a guaranteed typed-UAV format; downstream post-processing accepts any HDR color format.
		const FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
			SceneColor.Texture->Desc.Extent,
			PF_FloatRGBA,
			FClearValueBinding::Black,
			ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);
		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("PlanetAtmosphere.BoundsDebug"));

		// --- Parameters ----------------------------------------------------------------------------
		FAtmosphereBoundsDebugCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereBoundsDebugCS::FParameters>();
		Parameters->SceneColorTexture = SceneColor.Texture;
		Parameters->SceneDepthTexture = SceneDepthTexture;
		Parameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);
		Parameters->ClipToTranslatedWorld = FMatrix44f(View.ViewMatrices.GetInvTranslatedViewProjectionMatrix());
		Parameters->CameraTranslatedWorld = FVector3f(ViewOrigin + View.ViewMatrices.GetPreViewTranslation());
		Parameters->ViewRectMinAndSize = FVector4f(
			static_cast<float>(ViewRect.Min.X), static_cast<float>(ViewRect.Min.Y),
			static_cast<float>(ViewRect.Width()), static_cast<float>(ViewRect.Height()));
		Parameters->NumAtmospheres = NumAtmospheres;
		Parameters->DebugIntensity = CVars::GetDebugIntensity();

		for (int32 Index = 0; Index < PLANET_ATMOSPHERE_MAX_VISIBLE; ++Index)
		{
			if (Index < NumAtmospheres)
			{
				const FAtmosphereVisibleInstance& Instance = Instances[Index];
				const FPlanetAtmosphereRadii& R = Instance.RadiiUU;

				// All differences in double, converted to float only at the end. At Earth scale a float
				// position has ~64 cm granularity, so the shader must never derive altitudes itself.
				const FVector3d CameraRelativeToPlanet = ViewOrigin - Instance.PlanetCenterWorld;
				const double CameraAltitude = CameraRelativeToPlanet.Length() - R.Planet;

				Parameters->AtmosphereCameraRelAndPlanetRadius[Index] = FVector4f(
					FVector3f(CameraRelativeToPlanet),
					static_cast<float>(R.Planet));
				Parameters->AtmosphereAltitudes0[Index] = FVector4f(
					static_cast<float>(CameraAltitude),
					static_cast<float>(R.AtmosphereBottom - R.Planet),
					static_cast<float>(R.AtmosphereTop - R.Planet),
					static_cast<float>(R.CloudBottom - R.Planet));
				Parameters->AtmosphereAltitudes1[Index] = FVector4f(
					static_cast<float>(R.CloudTop - R.Planet), 0.0f, 0.0f, 0.0f);
			}
			else
			{
				Parameters->AtmosphereCameraRelAndPlanetRadius[Index] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
				Parameters->AtmosphereAltitudes0[Index] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
				Parameters->AtmosphereAltitudes1[Index] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
			}
		}

		// --- Dispatch ------------------------------------------------------------------------------
		TShaderMapRef<FAtmosphereBoundsDebugCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PlanetAtmosphere.BoundsDebug %dx%d (%d atmospheres)", ViewRect.Width(), ViewRect.Height(), NumAtmospheres),
			ComputeShader,
			Parameters,
			FComputeShaderUtils::GetGroupCount(ViewRect.Size(), FAtmosphereBoundsDebugCS::ThreadGroupSize));

		return FScreenPassTexture(OutputTexture, ViewRect);
	}
}
