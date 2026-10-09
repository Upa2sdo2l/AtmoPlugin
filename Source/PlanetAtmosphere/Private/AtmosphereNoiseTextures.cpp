// Copyright Epic Games, Inc. All Rights Reserved.

#include "AtmosphereNoiseTextures.h"
#include "AtmosphereShaders.h"
#include "PlanetAtmosphereTypes.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "GlobalShader.h"

namespace
{
	/** Creates a 3D R16F texture with NumMips mips and bakes every mip with FAtmosphereNoiseBakeCS (Kind 0 fBm, 1 Worley). */
	FRDGTextureRef BakeNoiseTexture(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* GlobalShaderMap,
		int32 Size,
		int32 NumMips,
		int32 Octaves,
		int32 Kind,
		const TCHAR* Name)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create3D(
			FIntVector(Size, Size, Size),
			PF_R16F,
			FClearValueBinding::Black,
			ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV,
			static_cast<uint8>(NumMips));
		FRDGTextureRef Texture = GraphBuilder.CreateTexture(Desc, Name);

		TShaderMapRef<FAtmosphereNoiseBakeCS> ComputeShader(GlobalShaderMap);
		for (int32 Mip = 0; Mip < NumMips; ++Mip)
		{
			const int32 MipSize = FMath::Max(Size >> Mip, 1);

			FAtmosphereNoiseBakeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FAtmosphereNoiseBakeCS::FParameters>();
			Parameters->OutNoise = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Texture, static_cast<uint8>(Mip)));
			Parameters->MipSize = MipSize;
			Parameters->Octaves = Octaves;
			Parameters->NoiseKind = Kind;
			Parameters->TexelSize = PlanetAtmosphere::NoiseTextures::TileSize / static_cast<float>(MipSize);

			const int32 Groups = FMath::DivideAndRoundUp(MipSize, FAtmosphereNoiseBakeCS::ThreadGroupSize);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PlanetAtmosphere.BakeNoise %s mip %d (%d^3)", Name, Mip, MipSize),
				ComputeShader,
				Parameters,
				FIntVector(Groups, Groups, Groups));
		}
		return Texture;
	}

	TGlobalResource<FPlanetAtmosphereNoiseTextures> GPlanetAtmosphereNoiseTextures;
}

bool FPlanetAtmosphereNoiseTextures::GetOrBake(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* GlobalShaderMap,
	FPlanetAtmosphereNoiseTexturesRDG& OutTextures)
{
	FScopeLock Lock(&Mutex);

	// Released at module shutdown (or never initialized): never re-create.
	if (!IsInitialized())
	{
		return false;
	}

	if (BaseShape.IsValid() && Erosion.IsValid() && Detail.IsValid())
	{
		OutTextures.BaseShape = GraphBuilder.RegisterExternalTexture(BaseShape);
		OutTextures.Erosion = GraphBuilder.RegisterExternalTexture(Erosion);
		OutTextures.Detail = GraphBuilder.RegisterExternalTexture(Detail);
		return true;
	}

	using namespace PlanetAtmosphere::NoiseTextures;
	BaseShape.SafeRelease();
	Erosion.SafeRelease();
	Detail.SafeRelease();

	OutTextures.BaseShape = BakeNoiseTexture(GraphBuilder, GlobalShaderMap, BaseSize, BaseNumMips, BaseOctaves, 0, TEXT("PlanetAtmosphere.BaseShapeNoise"));
	OutTextures.Erosion = BakeNoiseTexture(GraphBuilder, GlobalShaderMap, ErosionSize, ErosionNumMips, ErosionOctaves, 0, TEXT("PlanetAtmosphere.ErosionNoise"));
	OutTextures.Detail = BakeNoiseTexture(GraphBuilder, GlobalShaderMap, DetailSize, DetailNumMips, 0, 1, TEXT("PlanetAtmosphere.DetailNoise"));

	// Immediate allocation + external: the textures outlive this graph; the bake passes are not culled.
	BaseShape = GraphBuilder.ConvertToExternalTexture(OutTextures.BaseShape);
	Erosion = GraphBuilder.ConvertToExternalTexture(OutTextures.Erosion);
	Detail = GraphBuilder.ConvertToExternalTexture(OutTextures.Detail);

	UE_LOG(LogPlanetAtmosphere, Log,
		TEXT("Noise textures baked: base %d^3 x %d mips (%u bytes), erosion %d^3 x %d mips (%u bytes), detail (Worley) %d^3 x %d mips (%u bytes) [ComputeMemorySize]"),
		BaseSize, BaseNumMips, BaseShape.IsValid() ? BaseShape->ComputeMemorySize() : 0u,
		ErosionSize, ErosionNumMips, Erosion.IsValid() ? Erosion->ComputeMemorySize() : 0u,
		DetailSize, DetailNumMips, Detail.IsValid() ? Detail->ComputeMemorySize() : 0u);
	return true;
}

void FPlanetAtmosphereNoiseTextures::ReleaseRHI()
{
	FScopeLock Lock(&Mutex);
	BaseShape.SafeRelease();
	Erosion.SafeRelease();
	Detail.SafeRelease();
}

namespace PlanetAtmosphere
{
	FPlanetAtmosphereNoiseTextures& GetNoiseTextures()
	{
		return GPlanetAtmosphereNoiseTextures;
	}

	void ReleaseNoiseTextures_GameThread()
	{
		BeginReleaseResource(&GPlanetAtmosphereNoiseTextures);
		FlushRenderingCommands();
	}
}
