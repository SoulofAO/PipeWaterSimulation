#include "Core/Simulation1D/FluidSegment1DGpuSimulation.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidConsumerActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/Actors/PipeFluidPressureConsumerActor.h"
#include "Core/Actors/PipeFluidSourceActor.h"
#include "Core/Simulation1D/FluidSegment1DGpuTypes.h"
#include "Data/FluidData.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "GlobalShader.h"
#include "Math/UnrealMathUtility.h"
#include "RHICommandList.h"
#include "RHIResourceUtils.h"
#include "RHIUtilities.h"
#include "RenderGraphUtils.h"
#include "Core/Simulation/FluidPipeLumpedPhysicsLibrary.h"
#include "Core/Simulation/FluidSimulationStateLimits.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"

static float FluidSegment1DComputeCrossSectionArea(const FFluidSegmentStateOneD& SegmentState)
{
	const float SafePipeDiameter = FMath::Max(SegmentState.PipeDiameter, 0.001f);
	return UE_PI * 0.25f * SafePipeDiameter * SafePipeDiameter;
}

static EFluidSegment1DEndpointGpuKind FluidSegment1DClassifyEndpointActor(APipeFluidBasePointActor* EndpointActor)
{
	if (!EndpointActor)
	{
		return EFluidSegment1DEndpointGpuKind::None;
	}
	if (Cast<APipeFluidSourceActor>(EndpointActor))
	{
		return EFluidSegment1DEndpointGpuKind::Source;
	}
	if (Cast<APipeFluidPressureConsumerActor>(EndpointActor))
	{
		return EFluidSegment1DEndpointGpuKind::PressureConsumer;
	}
	if (Cast<APipeFluidConsumerActor>(EndpointActor))
	{
		return EFluidSegment1DEndpointGpuKind::ConstantFlow;
	}
	return EFluidSegment1DEndpointGpuKind::None;
}

static void FluidSegment1DWriteFloatToUintTable(TArray<uint32>& Table, const int32 SegmentIndex, const uint32 FieldIndex, const float Value)
{
	Table[SegmentIndex * static_cast<int32>(FluidSegment1DGpuFieldIndex::UIntsPerSegment) + static_cast<int32>(FieldIndex)] = *reinterpret_cast<const uint32*>(&Value);
}

static void FluidSegment1DWriteUintToUintTable(TArray<uint32>& Table, const int32 SegmentIndex, const uint32 FieldIndex, const uint32 Value)
{
	Table[SegmentIndex * static_cast<int32>(FluidSegment1DGpuFieldIndex::UIntsPerSegment) + static_cast<int32>(FieldIndex)] = Value;
}

class FFluidSegment1DCopyCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidSegment1DCopyCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidSegment1DCopyCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, TotalCells)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, CurrentPressure)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, CurrentFlow)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextPressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextFlow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidSegment1DCopyCS, "/Plugin/FluidPipesPlugin/Private/FluidSegment1DCopy.usf", "FluidSegment1DCopyMain", SF_Compute);

class FFluidSegment1DInteriorCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidSegment1DInteriorCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidSegment1DInteriorCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, TotalInteriorCells)
		SHADER_PARAMETER(float, SimulationStepTime)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, SegmentUintTable)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, InteriorWorkPacked)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, CurrentPressure)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, CurrentFlow)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextPressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextFlow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidSegment1DInteriorCS, "/Plugin/FluidPipesPlugin/Private/FluidSegment1DInterior.usf", "FluidSegment1DInteriorMain", SF_Compute);

class FFluidSegment1DBoundaryGeneralCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidSegment1DBoundaryGeneralCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidSegment1DBoundaryGeneralCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, SegmentCount)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, SegmentUintTable)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextPressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextFlow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidSegment1DBoundaryGeneralCS, "/Plugin/FluidPipesPlugin/Private/FluidSegment1DBoundaryGeneral.usf", "FluidSegment1DBoundaryGeneralMain", SF_Compute);

class FFluidSegment1DBoundarySourceCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidSegment1DBoundarySourceCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidSegment1DBoundarySourceCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, SourceBoundaryCount)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, SegmentUintTable)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, BoundarySourceWorkPacked)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextPressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextFlow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidSegment1DBoundarySourceCS, "/Plugin/FluidPipesPlugin/Private/FluidSegment1DBoundarySource.usf", "FluidSegment1DBoundarySourceMain", SF_Compute);

class FFluidSegment1DBoundaryPressureConsumerCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidSegment1DBoundaryPressureConsumerCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidSegment1DBoundaryPressureConsumerCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, PressureConsumerBoundaryCount)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, SegmentUintTable)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, BoundaryPressureConsumerWorkPacked)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextPressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextFlow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidSegment1DBoundaryPressureConsumerCS, "/Plugin/FluidPipesPlugin/Private/FluidSegment1DBoundaryPressureConsumer.usf", "FluidSegment1DBoundaryPressureConsumerMain", SF_Compute);

class FFluidSegment1DJunctionReduceCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidSegment1DJunctionReduceCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidSegment1DJunctionReduceCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, JunctionCount)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, SegmentUintTable)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, JunctionHeadersPacked)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, JunctionIncidentsPacked)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, NextPressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, JunctionPressureOut)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidSegment1DJunctionReduceCS, "/Plugin/FluidPipesPlugin/Private/FluidSegment1DJunctionReduce.usf", "FluidSegment1DJunctionReduceMain", SF_Compute);

class FFluidSegment1DJunctionApplyCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidSegment1DJunctionApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidSegment1DJunctionApplyCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, TotalJunctionIncidents)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, SegmentUintTable)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, JunctionIncidentsPacked)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, JunctionPressureOut)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextPressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextFlow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidSegment1DJunctionApplyCS, "/Plugin/FluidPipesPlugin/Private/FluidSegment1DJunctionApply.usf", "FluidSegment1DJunctionApplyMain", SF_Compute);

class FFluidSegment1DStateClampCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidSegment1DStateClampCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidSegment1DStateClampCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, TotalCells)
		SHADER_PARAMETER(uint32, bEnableClamping)
		SHADER_PARAMETER(float, MinimumPressure)
		SHADER_PARAMETER(float, MaximumPressure)
		SHADER_PARAMETER(float, MinimumVolumeFlowRate)
		SHADER_PARAMETER(float, MaximumVolumeFlowRate)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextPressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NextFlow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidSegment1DStateClampCS, "/Plugin/FluidPipesPlugin/Private/FluidSegment1DStateClamp.usf", "FluidSegment1DStateClampMain", SF_Compute);

FFluidSegment1DGpuSimulation::FFluidSegment1DGpuSimulation() = default;

FFluidSegment1DGpuSimulation::~FFluidSegment1DGpuSimulation()
{
	Release();
}

bool FFluidSegment1DGpuSimulation::IsAvailable() const
{
	return GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
}

bool FFluidSegment1DGpuSimulation::IsGpuStateResident() const
{
	return bGpuStateResident;
}

void FFluidSegment1DGpuSimulation::Release()
{
	if (!bResourcesAllocated)
	{
		return;
	}
	ReleaseInternal();
}

void FFluidSegment1DGpuSimulation::ReleaseInternal()
{
	PartialReadbackResources.Reset();
	GpuStepCompletionFence.SafeRelease();
	bGpuStepCompletionFenceWritten = false;
	bGpuStateResident = false;
	ENQUEUE_RENDER_COMMAND(FluidSegment1DGpuRelease)(
		[this](FRHICommandListImmediate& ImmediateCommands)
		{
			SegmentUintGpuBuffer.SafeRelease();
			SegmentUintGpuSrv.SafeRelease();
			InteriorWorkGpuBuffer.SafeRelease();
			InteriorWorkGpuSrv.SafeRelease();
			SourceBoundaryWorkGpuBuffer.SafeRelease();
			SourceBoundaryWorkGpuSrv.SafeRelease();
			PressureConsumerBoundaryWorkGpuBuffer.SafeRelease();
			PressureConsumerBoundaryWorkGpuSrv.SafeRelease();
			JunctionHeadersGpuBuffer.SafeRelease();
			JunctionHeadersGpuSrv.SafeRelease();
			JunctionIncidentsGpuBuffer.SafeRelease();
			JunctionIncidentsGpuSrv.SafeRelease();
			JunctionPressureGpuBuffer.SafeRelease();
			JunctionPressureGpuSrv.SafeRelease();
			JunctionPressureGpuUav.SafeRelease();
			PressureGpuBufferA.SafeRelease();
			PressureGpuBufferB.SafeRelease();
			FlowGpuBufferA.SafeRelease();
			FlowGpuBufferB.SafeRelease();
			PressureGpuBufferASrv.SafeRelease();
			PressureGpuBufferBSrv.SafeRelease();
			FlowGpuBufferASrv.SafeRelease();
			FlowGpuBufferBSrv.SafeRelease();
			PressureGpuBufferAUav.SafeRelease();
			PressureGpuBufferBUav.SafeRelease();
			FlowGpuBufferAUav.SafeRelease();
			FlowGpuBufferBUav.SafeRelease();
		});
	FlushRenderingCommands();
	bResourcesAllocated = false;
}

void FFluidSegment1DGpuSimulation::BuildJunctionGpuWorkLists(const TArray<FFluidSegmentStateOneD>& SegmentStates)
{
	JunctionHeadersPackedCpu.Reset();
	JunctionIncidentsPackedCpu.Reset();
	JunctionCount = 0u;
	TotalJunctionIncidents = 0u;

	TMap<int32, TArray<TPair<int32, bool>>> JunctionSceneNodeKeyToIncidentEndpoints;
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}
		if (SegmentState.LeftSceneNodeKey != INDEX_NONE)
		{
			JunctionSceneNodeKeyToIncidentEndpoints.FindOrAdd(SegmentState.LeftSceneNodeKey).Add(TPair<int32, bool>(SegmentIndex, true));
		}
		if (SegmentState.RightSceneNodeKey != INDEX_NONE)
		{
			JunctionSceneNodeKeyToIncidentEndpoints.FindOrAdd(SegmentState.RightSceneNodeKey).Add(TPair<int32, bool>(SegmentIndex, false));
		}
	}

	uint32 IncidentStart = 0u;
	uint32 JunctionIndex = 0u;
	for (const TPair<int32, TArray<TPair<int32, bool>>>& JunctionEntry : JunctionSceneNodeKeyToIncidentEndpoints)
	{
		const TArray<TPair<int32, bool>>& IncidentEndpoints = JunctionEntry.Value;
		if (IncidentEndpoints.Num() < 2)
		{
			continue;
		}
		JunctionHeadersPackedCpu.Add(IncidentStart);
		JunctionHeadersPackedCpu.Add(static_cast<uint32>(IncidentEndpoints.Num()));
		for (const TPair<int32, bool>& IncidentEndpoint : IncidentEndpoints)
		{
			JunctionIncidentsPackedCpu.Add(static_cast<uint32>(IncidentEndpoint.Key));
			JunctionIncidentsPackedCpu.Add(IncidentEndpoint.Value ? 1u : 0u);
			JunctionIncidentsPackedCpu.Add(JunctionIndex);
		}
		IncidentStart += static_cast<uint32>(IncidentEndpoints.Num());
		++JunctionIndex;
	}
	JunctionCount = JunctionIndex;
	TotalJunctionIncidents = IncidentStart;
}

void FFluidSegment1DGpuSimulation::BakeSegmentUintTableAtImport(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float GravityAcceleration)
{
	const FVector GravityDirectionWorld = FVector::UpVector;

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		const int32 CellCount = SegmentState.CellStates.Num();
		const uint32 CellCountU = static_cast<uint32>(CellCount);
		const uint32 CellBaseU = SegmentCellBaseCpu[SegmentIndex];

		FluidSegment1DWriteUintToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::CellBaseIndex, CellBaseU);
		FluidSegment1DWriteUintToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::CellCount, CellCountU);
		FluidSegment1DWriteUintToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftBoundaryKind, static_cast<uint32>(SegmentState.LeftBoundaryConditionType));
		FluidSegment1DWriteUintToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightBoundaryKind, static_cast<uint32>(SegmentState.RightBoundaryConditionType));
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftBoundaryPressure, SegmentState.LeftBoundaryPressure);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightBoundaryPressure, SegmentState.RightBoundaryPressure);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftBoundaryFlowUpload, SegmentState.LeftBoundaryFlow);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightBoundaryFlowUpload, SegmentState.RightBoundaryFlow);
		FluidSegment1DWriteFloatToUintTable(
			SegmentUintTableCpu,
			SegmentIndex,
			FluidSegment1DGpuFieldIndex::CellLength,
			FFluidPipeLumpedPhysicsLibrary::ComputeOneDimensionCellLengthMeters(SegmentState.CellLength));

		const float CrossSectionArea = FluidSegment1DComputeCrossSectionArea(SegmentState);
		const float SafeDensity = FMath::Max(SegmentState.Density, KINDA_SMALL_NUMBER);
		const float SafeWaveSpeed = FMath::Max(SegmentState.WaveSpeed, 1.0f);
		const float FrictionResistance = SegmentState.FrictionFactor / (2.0f * FMath::Max(SegmentState.PipeDiameter, 0.001f) * FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER));
		const float PressureCoefficient = SafeDensity * SafeWaveSpeed * SafeWaveSpeed / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);

		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::CrossSectionArea, CrossSectionArea);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::SafeDensity, SafeDensity);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::FrictionResistance, FrictionResistance);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::PressureCoefficient, PressureCoefficient);

		float GravityAxisComponent = 0.0f;
		if (SegmentPipeActors.IsValidIndex(SegmentIndex))
		{
			const APipeFluidPipeActor* PipeActor = SegmentPipeActors[SegmentIndex].Get();
			if (PipeActor)
			{
				const FVector PipeAxisDirectionWorld = PipeActor->GetActorForwardVector().GetSafeNormal();
				GravityAxisComponent = FVector::DotProduct(GravityDirectionWorld, PipeAxisDirectionWorld);
			}
		}
		const float GravityAccelerationAlongAxis = GravityAcceleration * GravityAxisComponent;
		const float GravitySourceTerm = -CrossSectionArea * GravityAccelerationAlongAxis;
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::GravitySourceTerm, GravitySourceTerm);

		APipeFluidBasePointActor* FirstEndpoint = nullptr;
		APipeFluidBasePointActor* SecondEndpoint = nullptr;
		if (SegmentPipeActors.IsValidIndex(SegmentIndex))
		{
			const APipeFluidPipeActor* PipeActor = SegmentPipeActors[SegmentIndex].Get();
			if (PipeActor)
			{
				FirstEndpoint = PipeActor->PipeEndpointFirst;
				SecondEndpoint = PipeActor->PipeEndpointSecond;
			}
		}

		EFluidSegment1DEndpointGpuKind LeftKind = EFluidSegment1DEndpointGpuKind::None;
		EFluidSegment1DEndpointGpuKind RightKind = EFluidSegment1DEndpointGpuKind::None;
		if (FirstEndpoint && FirstEndpoint->SceneNodeKey == SegmentState.LeftSceneNodeKey)
		{
			LeftKind = FluidSegment1DClassifyEndpointActor(FirstEndpoint);
		}
		else if (SecondEndpoint && SecondEndpoint->SceneNodeKey == SegmentState.LeftSceneNodeKey)
		{
			LeftKind = FluidSegment1DClassifyEndpointActor(SecondEndpoint);
		}
		if (FirstEndpoint && FirstEndpoint->SceneNodeKey == SegmentState.RightSceneNodeKey)
		{
			RightKind = FluidSegment1DClassifyEndpointActor(FirstEndpoint);
		}
		else if (SecondEndpoint && SecondEndpoint->SceneNodeKey == SegmentState.RightSceneNodeKey)
		{
			RightKind = FluidSegment1DClassifyEndpointActor(SecondEndpoint);
		}

		FluidSegment1DWriteUintToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftEndpointKind, static_cast<uint32>(LeftKind));
		FluidSegment1DWriteUintToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightEndpointKind, static_cast<uint32>(RightKind));

		const APipeFluidPressureConsumerActor* LeftPressureConsumer = nullptr;
		const APipeFluidPressureConsumerActor* RightPressureConsumer = nullptr;
		if (LeftKind == EFluidSegment1DEndpointGpuKind::PressureConsumer)
		{
			if (FirstEndpoint && FirstEndpoint->SceneNodeKey == SegmentState.LeftSceneNodeKey)
			{
				LeftPressureConsumer = Cast<APipeFluidPressureConsumerActor>(FirstEndpoint);
			}
			else if (SecondEndpoint && SecondEndpoint->SceneNodeKey == SegmentState.LeftSceneNodeKey)
			{
				LeftPressureConsumer = Cast<APipeFluidPressureConsumerActor>(SecondEndpoint);
			}
		}
		if (RightKind == EFluidSegment1DEndpointGpuKind::PressureConsumer)
		{
			if (FirstEndpoint && FirstEndpoint->SceneNodeKey == SegmentState.RightSceneNodeKey)
			{
				RightPressureConsumer = Cast<APipeFluidPressureConsumerActor>(FirstEndpoint);
			}
			else if (SecondEndpoint && SecondEndpoint->SceneNodeKey == SegmentState.RightSceneNodeKey)
			{
				RightPressureConsumer = Cast<APipeFluidPressureConsumerActor>(SecondEndpoint);
			}
		}

		if (LeftPressureConsumer)
		{
			FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftConsumerReferenceGaugePressure, LeftPressureConsumer->ConsumerReferenceGaugePressure);
			FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftConsumerVolumeFlowRatePerGaugePressureExcess, LeftPressureConsumer->ConsumerVolumeFlowRatePerGaugePressureExcess);
			FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftMinimumPressureConsumerVolumeFlowRate, LeftPressureConsumer->MinimumPressureConsumerVolumeFlowRate);
			FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftMaximumPressureConsumerVolumeFlowRate, LeftPressureConsumer->MaximumPressureConsumerVolumeFlowRate);
		}
		if (RightPressureConsumer)
		{
			FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightConsumerReferenceGaugePressure, RightPressureConsumer->ConsumerReferenceGaugePressure);
			FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightConsumerVolumeFlowRatePerGaugePressureExcess, RightPressureConsumer->ConsumerVolumeFlowRatePerGaugePressureExcess);
			FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightMinimumPressureConsumerVolumeFlowRate, RightPressureConsumer->MinimumPressureConsumerVolumeFlowRate);
			FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightMaximumPressureConsumerVolumeFlowRate, RightPressureConsumer->MaximumPressureConsumerVolumeFlowRate);
		}

		const APipeFluidSourceActor* LeftSource = nullptr;
		const APipeFluidSourceActor* RightSource = nullptr;
		if (LeftKind == EFluidSegment1DEndpointGpuKind::Source)
		{
			if (FirstEndpoint && FirstEndpoint->SceneNodeKey == SegmentState.LeftSceneNodeKey)
			{
				LeftSource = Cast<APipeFluidSourceActor>(FirstEndpoint);
			}
			else if (SecondEndpoint && SecondEndpoint->SceneNodeKey == SegmentState.LeftSceneNodeKey)
			{
				LeftSource = Cast<APipeFluidSourceActor>(SecondEndpoint);
			}
		}
		if (RightKind == EFluidSegment1DEndpointGpuKind::Source)
		{
			if (FirstEndpoint && FirstEndpoint->SceneNodeKey == SegmentState.RightSceneNodeKey)
			{
				RightSource = Cast<APipeFluidSourceActor>(FirstEndpoint);
			}
			else if (SecondEndpoint && SecondEndpoint->SceneNodeKey == SegmentState.RightSceneNodeKey)
			{
				RightSource = Cast<APipeFluidSourceActor>(SecondEndpoint);
			}
		}

		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftSourceVolumeFlowRate, LeftSource ? LeftSource->SourceVolumeFlowRate : 0.0f);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightSourceVolumeFlowRate, RightSource ? RightSource->SourceVolumeFlowRate : 0.0f);

		if (CellCount >= 2)
		{
			for (int32 LocalCellIndex = 1; LocalCellIndex <= CellCount - 2; ++LocalCellIndex)
			{
				InteriorWorkPackedCpu.Add(static_cast<uint32>(SegmentIndex));
				InteriorWorkPackedCpu.Add(static_cast<uint32>(LocalCellIndex));
			}
		}

		if (SegmentState.LeftBoundaryConditionType == EFluidBoundaryConditionTypeOneD::FixedFlow && LeftKind == EFluidSegment1DEndpointGpuKind::Source)
		{
			SourceBoundaryWorkPackedCpu.Add(static_cast<uint32>(SegmentIndex));
			SourceBoundaryWorkPackedCpu.Add(1u);
		}
		if (SegmentState.RightBoundaryConditionType == EFluidBoundaryConditionTypeOneD::FixedFlow && RightKind == EFluidSegment1DEndpointGpuKind::Source)
		{
			SourceBoundaryWorkPackedCpu.Add(static_cast<uint32>(SegmentIndex));
			SourceBoundaryWorkPackedCpu.Add(0u);
		}
		if (SegmentState.LeftBoundaryConditionType == EFluidBoundaryConditionTypeOneD::FixedFlow && LeftKind == EFluidSegment1DEndpointGpuKind::PressureConsumer)
		{
			PressureConsumerBoundaryWorkPackedCpu.Add(static_cast<uint32>(SegmentIndex));
			PressureConsumerBoundaryWorkPackedCpu.Add(1u);
		}
		if (SegmentState.RightBoundaryConditionType == EFluidBoundaryConditionTypeOneD::FixedFlow && RightKind == EFluidSegment1DEndpointGpuKind::PressureConsumer)
		{
			PressureConsumerBoundaryWorkPackedCpu.Add(static_cast<uint32>(SegmentIndex));
			PressureConsumerBoundaryWorkPackedCpu.Add(0u);
		}
	}
}

void FFluidSegment1DGpuSimulation::RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors)
{
	RebuildFromSegments(SegmentStates, SegmentPipeActors, nullptr);
}

void FFluidSegment1DGpuSimulation::RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, UWorld* SimulationWorld)
{
	ReleaseInternal();

	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(SimulationWorld);
	bEnableOneDStateVariableClamping = Settings.EnableOneDSimulationStateVariableClamping;
	OneDMinimumPressure = Settings.OneDMinimumPressure;
	OneDMaximumPressure = Settings.OneDMaximumPressure;
	OneDMinimumVolumeFlowRate = Settings.OneDMinimumVolumeFlowRate;
	OneDMaximumVolumeFlowRate = Settings.OneDMaximumVolumeFlowRate;

	SegmentCount = static_cast<uint32>(SegmentStates.Num());
	SegmentCellBaseCpu.Reset();
	SegmentCellCountCpu.Reset();
	SegmentUintTableCpu.Reset();
	InteriorWorkPackedCpu.Reset();
	SourceBoundaryWorkPackedCpu.Reset();
	PressureConsumerBoundaryWorkPackedCpu.Reset();
	TotalCellsGlobal = 0u;
	TotalInteriorCells = 0u;
	SourceBoundaryCount = 0u;
	PressureConsumerBoundaryCount = 0u;
	bGpuStateResident = false;
	bReadFromBufferA = true;

	if (SegmentCount == 0u)
	{
		return;
	}

	uint32 RunningCellBase = 0u;
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		const int32 CellCount = SegmentState.CellStates.Num();
		SegmentCellBaseCpu.Add(RunningCellBase);
		SegmentCellCountCpu.Add(static_cast<uint32>(CellCount));
		if (CellCount >= 2)
		{
			TotalInteriorCells += static_cast<uint32>(CellCount - 2);
		}
		RunningCellBase += static_cast<uint32>(FMath::Max(CellCount, 0));
	}
	TotalCellsGlobal = RunningCellBase;

	SegmentUintTableCpu.SetNumZeroed(static_cast<int32>(SegmentCount * FluidSegment1DGpuFieldIndex::UIntsPerSegment));

	const float GravityAccelerationMetersPerSecondSquared = FFluidPipeLumpedPhysicsLibrary::ConvertGravityAccelerationCentimetersPerSecondSquaredToMetersPerSecondSquared(
		SimulationWorld ? FMath::Abs(SimulationWorld->GetGravityZ()) : 980.0f);
	BakeSegmentUintTableAtImport(SegmentStates, SegmentPipeActors, GravityAccelerationMetersPerSecondSquared);
	BuildJunctionGpuWorkLists(SegmentStates);

	if (InteriorWorkPackedCpu.Num() == 0)
	{
		InteriorWorkPackedCpu.Add(0u);
		InteriorWorkPackedCpu.Add(1u);
	}
	SourceBoundaryCount = static_cast<uint32>(SourceBoundaryWorkPackedCpu.Num() / 2);
	PressureConsumerBoundaryCount = static_cast<uint32>(PressureConsumerBoundaryWorkPackedCpu.Num() / 2);

	if (TotalCellsGlobal == 0u)
	{
		return;
	}

	TArray<float> InitialPressure;
	TArray<float> InitialFlow;
	InitialPressure.SetNumUninitialized(static_cast<int32>(TotalCellsGlobal));
	InitialFlow.SetNumUninitialized(static_cast<int32>(TotalCellsGlobal));
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		const int32 CellCount = SegmentState.CellStates.Num();
		const uint32 BaseIndex = SegmentCellBaseCpu[SegmentIndex];
		for (int32 LocalIndex = 0; LocalIndex < CellCount; ++LocalIndex)
		{
			const int32 GlobalIndex = static_cast<int32>(BaseIndex) + LocalIndex;
			InitialPressure[GlobalIndex] = SegmentState.CellStates[LocalIndex].Pressure;
			InitialFlow[GlobalIndex] = SegmentState.CellStates[LocalIndex].FlowRate;
		}
	}

	const uint32 SafeJunctionCount = FMath::Max(JunctionCount, 1u);
	const uint32 SafeJunctionIncidentCount = FMath::Max(TotalJunctionIncidents, 1u);

	ENQUEUE_RENDER_COMMAND(FluidSegment1DGpuAlloc)(
		[this, InitialPressure, InitialFlow, SafeJunctionCount, SafeJunctionIncidentCount](FRHICommandListImmediate& ImmediateCommands)
		{
			auto CreateStructuredSrvOnly = [&ImmediateCommands](FBufferRHIRef& OutBuffer, FShaderResourceViewRHIRef& OutSrv, const TCHAR* DebugName, const uint32 BytesPerElement, const uint32 NumElements, const void* InitialData)
			{
				const uint32 SafeNumElements = FMath::Max(NumElements, 1u);
				const uint32 NumBytes = BytesPerElement * SafeNumElements;
				FRHIBufferCreateDesc CreateDesc = FRHIBufferCreateDesc::CreateStructured(DebugName, NumBytes, BytesPerElement)
					.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::StructuredBuffer)
					.SetInitialState(ERHIAccess::SRVMask);
				CreateDesc.SetInitActionZeroData();
				OutBuffer = ImmediateCommands.CreateBuffer(CreateDesc);
				if (InitialData && NumElements > 0u)
				{
					const uint32 UploadBytes = BytesPerElement * NumElements;
					void* DestinationData = ImmediateCommands.LockBuffer(OutBuffer, 0, UploadBytes, RLM_WriteOnly);
					FMemory::Memcpy(DestinationData, InitialData, UploadBytes);
					ImmediateCommands.UnlockBuffer(OutBuffer);
				}
				OutSrv = ImmediateCommands.CreateShaderResourceView(OutBuffer, FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(OutBuffer));
			};

			auto CreateStructuredSrvUav = [&ImmediateCommands](FBufferRHIRef& OutBuffer, FShaderResourceViewRHIRef& OutSrv, FUnorderedAccessViewRHIRef& OutUav, const TCHAR* DebugName, const uint32 BytesPerElement, const uint32 NumElements, const void* InitialData)
			{
				const uint32 SafeNumElements = FMath::Max(NumElements, 1u);
				const uint32 NumBytes = BytesPerElement * SafeNumElements;
				FRHIBufferCreateDesc CreateDesc = FRHIBufferCreateDesc::CreateStructured(DebugName, NumBytes, BytesPerElement)
					.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::StructuredBuffer)
					.SetInitialState(ERHIAccess::UAVMask);
				CreateDesc.SetInitActionZeroData();
				OutBuffer = ImmediateCommands.CreateBuffer(CreateDesc);
				if (InitialData && NumElements > 0u)
				{
					const uint32 UploadBytes = BytesPerElement * NumElements;
					void* DestinationData = ImmediateCommands.LockBuffer(OutBuffer, 0, UploadBytes, RLM_WriteOnly);
					FMemory::Memcpy(DestinationData, InitialData, UploadBytes);
					ImmediateCommands.UnlockBuffer(OutBuffer);
				}
				OutSrv = ImmediateCommands.CreateShaderResourceView(OutBuffer, FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(OutBuffer));
				OutUav = ImmediateCommands.CreateUnorderedAccessView(OutBuffer, FRHIViewDesc::CreateBufferUAV().SetTypeFromBuffer(OutBuffer));
			};

			const uint32 FloatStride = sizeof(float);
			CreateStructuredSrvOnly(SegmentUintGpuBuffer, SegmentUintGpuSrv, TEXT("FluidSegment1DSegmentUintTable"), sizeof(uint32), static_cast<uint32>(SegmentUintTableCpu.Num()), SegmentUintTableCpu.GetData());
			CreateStructuredSrvOnly(InteriorWorkGpuBuffer, InteriorWorkGpuSrv, TEXT("FluidSegment1DInteriorWork"), sizeof(uint32), static_cast<uint32>(InteriorWorkPackedCpu.Num()), InteriorWorkPackedCpu.GetData());
			CreateStructuredSrvOnly(SourceBoundaryWorkGpuBuffer, SourceBoundaryWorkGpuSrv, TEXT("FluidSegment1DSourceBoundaryWork"), sizeof(uint32), static_cast<uint32>(SourceBoundaryWorkPackedCpu.Num()), SourceBoundaryWorkPackedCpu.GetData());
			CreateStructuredSrvOnly(PressureConsumerBoundaryWorkGpuBuffer, PressureConsumerBoundaryWorkGpuSrv, TEXT("FluidSegment1DPressureConsumerBoundaryWork"), sizeof(uint32), static_cast<uint32>(PressureConsumerBoundaryWorkPackedCpu.Num()), PressureConsumerBoundaryWorkPackedCpu.GetData());
			CreateStructuredSrvOnly(JunctionHeadersGpuBuffer, JunctionHeadersGpuSrv, TEXT("FluidSegment1DJunctionHeaders"), sizeof(uint32), SafeJunctionCount * 2u, JunctionHeadersPackedCpu.GetData());
			CreateStructuredSrvOnly(JunctionIncidentsGpuBuffer, JunctionIncidentsGpuSrv, TEXT("FluidSegment1DJunctionIncidents"), sizeof(uint32), SafeJunctionIncidentCount * 3u, JunctionIncidentsPackedCpu.GetData());
			CreateStructuredSrvUav(JunctionPressureGpuBuffer, JunctionPressureGpuSrv, JunctionPressureGpuUav, TEXT("FluidSegment1DJunctionPressure"), FloatStride, SafeJunctionCount, nullptr);

			CreateStructuredSrvUav(PressureGpuBufferA, PressureGpuBufferASrv, PressureGpuBufferAUav, TEXT("FluidSegment1DPressureA"), FloatStride, TotalCellsGlobal, InitialPressure.GetData());
			CreateStructuredSrvUav(PressureGpuBufferB, PressureGpuBufferBSrv, PressureGpuBufferBUav, TEXT("FluidSegment1DPressureB"), FloatStride, TotalCellsGlobal, nullptr);
			CreateStructuredSrvUav(FlowGpuBufferA, FlowGpuBufferASrv, FlowGpuBufferAUav, TEXT("FluidSegment1DFlowA"), FloatStride, TotalCellsGlobal, InitialFlow.GetData());
			CreateStructuredSrvUav(FlowGpuBufferB, FlowGpuBufferBSrv, FlowGpuBufferBUav, TEXT("FluidSegment1DFlowB"), FloatStride, TotalCellsGlobal, nullptr);
		});
	FlushRenderingCommands();
	bResourcesAllocated = true;
	bGpuStateResident = true;
	bReadFromBufferA = true;
}

void FFluidSegment1DGpuSimulation::DispatchGpuSimulationStep(FRHICommandListImmediate& ImmediateCommands, float SimulationStepTime, bool bReadFromA)
{
	FShaderResourceViewRHIRef CurrentPressureSrv = bReadFromA ? PressureGpuBufferASrv : PressureGpuBufferBSrv;
	FShaderResourceViewRHIRef CurrentFlowSrv = bReadFromA ? FlowGpuBufferASrv : FlowGpuBufferBSrv;
	FShaderResourceViewRHIRef NextPressureSrv = bReadFromA ? PressureGpuBufferBSrv : PressureGpuBufferASrv;
	FShaderResourceViewRHIRef NextFlowSrv = bReadFromA ? FlowGpuBufferBSrv : FlowGpuBufferASrv;
	FUnorderedAccessViewRHIRef NextPressureUav = bReadFromA ? PressureGpuBufferBUav : PressureGpuBufferAUav;
	FUnorderedAccessViewRHIRef NextFlowUav = bReadFromA ? FlowGpuBufferBUav : FlowGpuBufferAUav;

	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	{
		FFluidSegment1DCopyCS::FParameters PassParameters;
		PassParameters.TotalCells = TotalCellsGlobal;
		PassParameters.CurrentPressure = CurrentPressureSrv;
		PassParameters.CurrentFlow = CurrentFlowSrv;
		PassParameters.NextPressure = NextPressureUav;
		PassParameters.NextFlow = NextFlowUav;
		TShaderMapRef<FFluidSegment1DCopyCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(TotalCellsGlobal), static_cast<int32>(FFluidSegment1DCopyCS::ThreadGroupSize)), 1, 1));
	}

	{
		FFluidSegment1DInteriorCS::FParameters PassParameters;
		PassParameters.TotalInteriorCells = TotalInteriorCells;
		PassParameters.SimulationStepTime = SimulationStepTime;
		PassParameters.SegmentUintTable = SegmentUintGpuSrv;
		PassParameters.InteriorWorkPacked = InteriorWorkGpuSrv;
		PassParameters.CurrentPressure = CurrentPressureSrv;
		PassParameters.CurrentFlow = CurrentFlowSrv;
		PassParameters.NextPressure = NextPressureUav;
		PassParameters.NextFlow = NextFlowUav;
		TShaderMapRef<FFluidSegment1DInteriorCS> ComputeShader(GlobalShaderMap);
		const int32 InteriorGroups = TotalInteriorCells > 0u ? FMath::DivideAndRoundUp(static_cast<int32>(TotalInteriorCells), static_cast<int32>(FFluidSegment1DInteriorCS::ThreadGroupSize)) : 0;
		if (InteriorGroups > 0)
		{
			FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(InteriorGroups, 1, 1));
		}
	}

	{
		FFluidSegment1DBoundaryGeneralCS::FParameters PassParameters;
		PassParameters.SegmentCount = SegmentCount;
		PassParameters.SegmentUintTable = SegmentUintGpuSrv;
		PassParameters.NextPressure = NextPressureUav;
		PassParameters.NextFlow = NextFlowUav;
		TShaderMapRef<FFluidSegment1DBoundaryGeneralCS> ComputeShader(GlobalShaderMap);
		const int32 BoundaryGroups = FMath::DivideAndRoundUp(static_cast<int32>(SegmentCount * 2u), static_cast<int32>(FFluidSegment1DBoundaryGeneralCS::ThreadGroupSize));
		FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(BoundaryGroups, 1, 1));
	}

	if (SourceBoundaryCount > 0u)
	{
		FFluidSegment1DBoundarySourceCS::FParameters PassParameters;
		PassParameters.SourceBoundaryCount = SourceBoundaryCount;
		PassParameters.SegmentUintTable = SegmentUintGpuSrv;
		PassParameters.BoundarySourceWorkPacked = SourceBoundaryWorkGpuSrv;
		PassParameters.NextPressure = NextPressureUav;
		PassParameters.NextFlow = NextFlowUav;
		TShaderMapRef<FFluidSegment1DBoundarySourceCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(SourceBoundaryCount), static_cast<int32>(FFluidSegment1DBoundarySourceCS::ThreadGroupSize)), 1, 1));
	}

	if (PressureConsumerBoundaryCount > 0u)
	{
		FFluidSegment1DBoundaryPressureConsumerCS::FParameters PassParameters;
		PassParameters.PressureConsumerBoundaryCount = PressureConsumerBoundaryCount;
		PassParameters.SegmentUintTable = SegmentUintGpuSrv;
		PassParameters.BoundaryPressureConsumerWorkPacked = PressureConsumerBoundaryWorkGpuSrv;
		PassParameters.NextPressure = NextPressureUav;
		PassParameters.NextFlow = NextFlowUav;
		TShaderMapRef<FFluidSegment1DBoundaryPressureConsumerCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(PressureConsumerBoundaryCount), static_cast<int32>(FFluidSegment1DBoundaryPressureConsumerCS::ThreadGroupSize)), 1, 1));
	}

	if (JunctionCount > 0u)
	{
		FFluidSegment1DJunctionReduceCS::FParameters ReduceParameters;
		ReduceParameters.JunctionCount = JunctionCount;
		ReduceParameters.SegmentUintTable = SegmentUintGpuSrv;
		ReduceParameters.JunctionHeadersPacked = JunctionHeadersGpuSrv;
		ReduceParameters.JunctionIncidentsPacked = JunctionIncidentsGpuSrv;
		ReduceParameters.NextPressure = NextPressureSrv;
		ReduceParameters.JunctionPressureOut = JunctionPressureGpuUav;
		TShaderMapRef<FFluidSegment1DJunctionReduceCS> ReduceShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ReduceShader, ReduceParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(JunctionCount), static_cast<int32>(FFluidSegment1DJunctionReduceCS::ThreadGroupSize)), 1, 1));

		FFluidSegment1DJunctionApplyCS::FParameters ApplyParameters;
		ApplyParameters.TotalJunctionIncidents = TotalJunctionIncidents;
		ApplyParameters.SegmentUintTable = SegmentUintGpuSrv;
		ApplyParameters.JunctionIncidentsPacked = JunctionIncidentsGpuSrv;
		ApplyParameters.JunctionPressureOut = JunctionPressureGpuSrv;
		ApplyParameters.NextPressure = NextPressureUav;
		ApplyParameters.NextFlow = NextFlowUav;
		TShaderMapRef<FFluidSegment1DJunctionApplyCS> ApplyShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ApplyShader, ApplyParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(TotalJunctionIncidents), static_cast<int32>(FFluidSegment1DJunctionApplyCS::ThreadGroupSize)), 1, 1));
	}

	if (bEnableOneDStateVariableClamping && TotalCellsGlobal > 0u)
	{
		FFluidSegment1DStateClampCS::FParameters ClampParameters;
		ClampParameters.TotalCells = TotalCellsGlobal;
		ClampParameters.bEnableClamping = 1u;
		ClampParameters.MinimumPressure = OneDMinimumPressure;
		ClampParameters.MaximumPressure = OneDMaximumPressure;
		ClampParameters.MinimumVolumeFlowRate = OneDMinimumVolumeFlowRate;
		ClampParameters.MaximumVolumeFlowRate = OneDMaximumVolumeFlowRate;
		ClampParameters.NextPressure = NextPressureUav;
		ClampParameters.NextFlow = NextFlowUav;
		TShaderMapRef<FFluidSegment1DStateClampCS> ClampShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ClampShader, ClampParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(TotalCellsGlobal), static_cast<int32>(FFluidSegment1DStateClampCS::ThreadGroupSize)), 1, 1));
	}

}

void FFluidSegment1DGpuSimulation::SimulateStepGpuOnly(float SimulationStepTime)
{
	if (!bResourcesAllocated || !bGpuStateResident || TotalCellsGlobal == 0u || !IsAvailable())
	{
		bGpuStepCompletionFenceWritten = false;
		return;
	}

	const bool bReadFromA = bReadFromBufferA;
	ENQUEUE_RENDER_COMMAND(FluidSegment1DGpuStepGpuOnly)(
		[this, SimulationStepTime, bReadFromA](FRHICommandListImmediate& ImmediateCommands)
		{
			DispatchGpuSimulationStep(ImmediateCommands, SimulationStepTime, bReadFromA);
			if (!GpuStepCompletionFence.IsValid())
			{
				GpuStepCompletionFence = RHICreateGPUFence(TEXT("FluidSegment1DGpuStepCompletionFence"));
			}
			GpuStepCompletionFence->Clear();
			ImmediateCommands.WriteGPUFence(GpuStepCompletionFence);
		});
	bGpuStepCompletionFenceWritten = true;
	bReadFromBufferA = !bReadFromA;
}

void FFluidSegment1DGpuSimulation::WaitForGpuStepCompletion()
{
	if (!bGpuStepCompletionFenceWritten || !GpuStepCompletionFence.IsValid())
	{
		return;
	}

	FlushRenderingCommands();

	const double WaitStartSeconds = FPlatformTime::Seconds();
	const double WaitTimeoutSeconds = 30.0;
	while (FPlatformTime::Seconds() - WaitStartSeconds <= WaitTimeoutSeconds)
	{
		if (GpuStepCompletionFence->NumPendingWriteCommands.GetValue() == 0 && GpuStepCompletionFence->Poll())
		{
			return;
		}
		FPlatformProcess::SleepNoStats(0.0f);
	}

	UE_LOG(LogTemp, Warning, TEXT("Fluid 1D GPU step completion wait timed out after 30 seconds."));
}

void FFluidSegment1DGpuSimulation::SimulateStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, const float SimulationStepTime)
{
	SimulateStepGpuOnly(SimulationStepTime);
}

void FFluidSegment1DGpuSimulation::ReadbackSegmentIndicesToSegmentStates(TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<int32>& SegmentIndices)
{
	if (!bResourcesAllocated || !bGpuStateResident || TotalCellsGlobal == 0u || SegmentIndices.Num() == 0)
	{
		return;
	}

	struct FFluidSegmentPartialReadbackSlot
	{
		int32 SegmentIndex = INDEX_NONE;
		uint32 SourceByteOffset = 0u;
		uint32 ReadByteCount = 0u;
		TArray<float> PressureScratch;
		TArray<float> FlowScratch;
	};

	TArray<FFluidSegmentPartialReadbackSlot> ReadbackSlots;
	ReadbackSlots.Reserve(SegmentIndices.Num());
	TArray<uint32> EnqueueSourceByteOffsets;
	TArray<uint32> EnqueueReadByteCounts;
	EnqueueSourceByteOffsets.Reserve(SegmentIndices.Num());
	EnqueueReadByteCounts.Reserve(SegmentIndices.Num());

	for (const int32 SegmentIndex : SegmentIndices)
	{
		if (!SegmentStates.IsValidIndex(SegmentIndex) || !SegmentCellBaseCpu.IsValidIndex(SegmentIndex) || !SegmentCellCountCpu.IsValidIndex(SegmentIndex))
		{
			continue;
		}

		const int32 CellCount = static_cast<int32>(SegmentCellCountCpu[SegmentIndex]);
		if (CellCount <= 0 || SegmentStates[SegmentIndex].CellStates.Num() != CellCount)
		{
			continue;
		}

		FFluidSegmentPartialReadbackSlot& Slot = ReadbackSlots.AddDefaulted_GetRef();
		Slot.SegmentIndex = SegmentIndex;
		Slot.SourceByteOffset = SegmentCellBaseCpu[SegmentIndex] * static_cast<uint32>(sizeof(float));
		Slot.ReadByteCount = static_cast<uint32>(CellCount) * static_cast<uint32>(sizeof(float));
		Slot.PressureScratch.SetNumUninitialized(CellCount);
		Slot.FlowScratch.SetNumUninitialized(CellCount);
		EnqueueSourceByteOffsets.Add(Slot.SourceByteOffset);
		EnqueueReadByteCounts.Add(Slot.ReadByteCount);
	}

	if (ReadbackSlots.Num() == 0)
	{
		return;
	}

	while (PartialReadbackResources.Num() < ReadbackSlots.Num())
	{
		PartialReadbackResources.AddDefaulted();
	}

	FBufferRHIRef LatestPressureBuffer = bReadFromBufferA ? PressureGpuBufferA : PressureGpuBufferB;
	FBufferRHIRef LatestFlowBuffer = bReadFromBufferA ? FlowGpuBufferA : FlowGpuBufferB;

	ENQUEUE_RENDER_COMMAND(FluidSegment1DGpuPartialReadbackEnqueue)(
		[this, LatestPressureBuffer, LatestFlowBuffer, EnqueueSourceByteOffsets, EnqueueReadByteCounts](FRHICommandListImmediate& ImmediateCommands)
		{
			ImmediateCommands.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			for (int32 SlotIndex = 0; SlotIndex < EnqueueSourceByteOffsets.Num(); ++SlotIndex)
			{
				const uint32 SourceByteOffset = EnqueueSourceByteOffsets[SlotIndex];
				const uint32 ReadByteCount = EnqueueReadByteCounts[SlotIndex];
				FFluidSegmentPartialReadbackResources& Resources = PartialReadbackResources[SlotIndex];
				if (!Resources.Fence.IsValid())
				{
					Resources.Fence = RHICreateGPUFence(TEXT("FluidSegment1DPartialReadbackFence"));
				}
				if (!Resources.PressureStaging.IsValid())
				{
					Resources.PressureStaging = RHICreateStagingBuffer();
				}
				if (!Resources.FlowStaging.IsValid())
				{
					Resources.FlowStaging = RHICreateStagingBuffer();
				}
				Resources.Fence->Clear();
				ImmediateCommands.CopyToStagingBuffer(LatestPressureBuffer, Resources.PressureStaging, SourceByteOffset, ReadByteCount);
				ImmediateCommands.CopyToStagingBuffer(LatestFlowBuffer, Resources.FlowStaging, SourceByteOffset, ReadByteCount);
				ImmediateCommands.WriteGPUFence(Resources.Fence);
			}
			ImmediateCommands.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		});
	FlushRenderingCommands();

	const double WaitStartSeconds = FPlatformTime::Seconds();
	const double WaitTimeoutSeconds = 5.0;
	bool bAllReadbacksReady = false;
	while (FPlatformTime::Seconds() - WaitStartSeconds <= WaitTimeoutSeconds)
	{
		bAllReadbacksReady = true;
		for (int32 SlotIndex = 0; SlotIndex < ReadbackSlots.Num(); ++SlotIndex)
		{
			const FFluidSegmentPartialReadbackResources& Resources = PartialReadbackResources[SlotIndex];
			if (!Resources.Fence.IsValid() || Resources.Fence->NumPendingWriteCommands.GetValue() != 0 || !Resources.Fence->Poll())
			{
				bAllReadbacksReady = false;
				break;
			}
		}
		if (bAllReadbacksReady)
		{
			break;
		}
		FPlatformProcess::Sleep(0.0f);
	}

	if (!bAllReadbacksReady)
	{
		return;
	}

	TArray<bool> SlotCopySucceeded;
	SlotCopySucceeded.Init(false, ReadbackSlots.Num());

	ENQUEUE_RENDER_COMMAND(FluidSegment1DGpuPartialReadbackLock)(
		[this, &ReadbackSlots, &SlotCopySucceeded](FRHICommandListImmediate& ImmediateCommands)
		{
			for (int32 SlotIndex = 0; SlotIndex < ReadbackSlots.Num(); ++SlotIndex)
			{
				FFluidSegmentPartialReadbackSlot& Slot = ReadbackSlots[SlotIndex];
				FFluidSegmentPartialReadbackResources& Resources = PartialReadbackResources[SlotIndex];
				void* PressureRead = ImmediateCommands.LockStagingBuffer(Resources.PressureStaging, Resources.Fence.GetReference(), 0, Slot.ReadByteCount);
				void* FlowRead = ImmediateCommands.LockStagingBuffer(Resources.FlowStaging, Resources.Fence.GetReference(), 0, Slot.ReadByteCount);
				if (PressureRead && FlowRead)
				{
					FMemory::Memcpy(Slot.PressureScratch.GetData(), PressureRead, Slot.ReadByteCount);
					FMemory::Memcpy(Slot.FlowScratch.GetData(), FlowRead, Slot.ReadByteCount);
					SlotCopySucceeded[SlotIndex] = true;
				}
				if (PressureRead)
				{
					ImmediateCommands.UnlockStagingBuffer(Resources.PressureStaging);
				}
				if (FlowRead)
				{
					ImmediateCommands.UnlockStagingBuffer(Resources.FlowStaging);
				}
			}
		});
	FlushRenderingCommands();

	for (int32 SlotIndex = 0; SlotIndex < ReadbackSlots.Num(); ++SlotIndex)
	{
		if (!SlotCopySucceeded[SlotIndex])
		{
			continue;
		}

		const FFluidSegmentPartialReadbackSlot& Slot = ReadbackSlots[SlotIndex];
		FFluidSegmentStateOneD& SegmentState = SegmentStates[Slot.SegmentIndex];
		const int32 CellCount = Slot.PressureScratch.Num();
		for (int32 LocalIndex = 0; LocalIndex < CellCount; ++LocalIndex)
		{
			SegmentState.CellStates[LocalIndex].Pressure = Slot.PressureScratch[LocalIndex];
			SegmentState.CellStates[LocalIndex].FlowRate = Slot.FlowScratch[LocalIndex];
		}
		const float CrossSectionArea = FluidSegment1DComputeCrossSectionArea(SegmentState);
		for (FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
		{
			CellState.Velocity = CellState.FlowRate / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);
		}
	}
}
