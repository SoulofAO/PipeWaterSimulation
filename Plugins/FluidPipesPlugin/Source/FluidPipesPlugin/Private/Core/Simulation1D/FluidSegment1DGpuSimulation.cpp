#include "Core/Simulation1D/FluidSegment1DGpuSimulation.h"

#include "Core/Actors/PipeFluidBasePointActor.h"
#include "Core/Actors/PipeFluidConsumerActor.h"
#include "Core/Actors/PipeFluidPipeActor.h"
#include "Core/Actors/PipeFluidPressureConsumerActor.h"
#include "Core/Actors/PipeFluidSourceActor.h"
#include "Core/Simulation1D/FluidSegment1DGpuTypes.h"
#include "Data/FluidData.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "Math/UnrealMathUtility.h"
#include "RHICommandList.h"
#include "RHIResourceUtils.h"
#include "RHIUtilities.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderCompilerCore.h"
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

FFluidSegment1DGpuSimulation::FFluidSegment1DGpuSimulation() = default;

FFluidSegment1DGpuSimulation::~FFluidSegment1DGpuSimulation()
{
	Release();
}

bool FFluidSegment1DGpuSimulation::IsAvailable() const
{
	return GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
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
	GpuPressureReadback.Reset();
	GpuFlowReadback.Reset();
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

void FFluidSegment1DGpuSimulation::RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors)
{
	ReleaseInternal();

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
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::CellLength, SegmentState.CellLength);

		const float CrossSectionArea = FluidSegment1DComputeCrossSectionArea(SegmentState);
		const float SafeDensity = FMath::Max(SegmentState.Density, KINDA_SMALL_NUMBER);
		const float SafeWaveSpeed = FMath::Max(SegmentState.WaveSpeed, 1.0f);
		const float FrictionResistance = SegmentState.FrictionFactor / (2.0f * FMath::Max(SegmentState.PipeDiameter, 0.001f) * FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER));
		const float PressureCoefficient = SafeDensity * SafeWaveSpeed * SafeWaveSpeed / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);

		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::CrossSectionArea, CrossSectionArea);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::SafeDensity, SafeDensity);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::FrictionResistance, FrictionResistance);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::PressureCoefficient, PressureCoefficient);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::GravitySourceTerm, 0.0f);

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

	ENQUEUE_RENDER_COMMAND(FluidSegment1DGpuAlloc)(
		[this](FRHICommandListImmediate& ImmediateCommands)
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

			CreateStructuredSrvUav(PressureGpuBufferA, PressureGpuBufferASrv, PressureGpuBufferAUav, TEXT("FluidSegment1DPressureA"), FloatStride, TotalCellsGlobal, nullptr);
			CreateStructuredSrvUav(PressureGpuBufferB, PressureGpuBufferBSrv, PressureGpuBufferBUav, TEXT("FluidSegment1DPressureB"), FloatStride, TotalCellsGlobal, nullptr);
			CreateStructuredSrvUav(FlowGpuBufferA, FlowGpuBufferASrv, FlowGpuBufferAUav, TEXT("FluidSegment1DFlowA"), FloatStride, TotalCellsGlobal, nullptr);
			CreateStructuredSrvUav(FlowGpuBufferB, FlowGpuBufferBSrv, FlowGpuBufferBUav, TEXT("FluidSegment1DFlowB"), FloatStride, TotalCellsGlobal, nullptr);
		});
	FlushRenderingCommands();
	GpuPressureReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("FluidSegment1DPressureReadback"));
	GpuFlowReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("FluidSegment1DFlowReadback"));
	bResourcesAllocated = true;
}

void FFluidSegment1DGpuSimulation::SimulateStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, const float SimulationStepTime)
{
	if (!bResourcesAllocated || TotalCellsGlobal == 0u || !IsAvailable())
	{
		return;
	}

	const float GravityAcceleration = World ? FMath::Abs(World->GetGravityZ()) : 980.0f;
	const FVector GravityDirectionWorld = FVector::UpVector;

	TArray<float> PressureScratch;
	TArray<float> FlowScratch;
	PressureScratch.SetNumUninitialized(static_cast<int32>(TotalCellsGlobal));
	FlowScratch.SetNumUninitialized(static_cast<int32>(TotalCellsGlobal));

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		const int32 CellCount = SegmentState.CellStates.Num();
		const uint32 BaseIndex = SegmentCellBaseCpu[SegmentIndex];
		for (int32 LocalIndex = 0; LocalIndex < CellCount; ++LocalIndex)
		{
			const int32 GlobalIndex = static_cast<int32>(BaseIndex) + LocalIndex;
			PressureScratch[GlobalIndex] = SegmentState.CellStates[LocalIndex].Pressure;
			FlowScratch[GlobalIndex] = SegmentState.CellStates[LocalIndex].FlowRate;
		}

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
		const float CrossSectionArea = FluidSegment1DComputeCrossSectionArea(SegmentState);
		const float GravitySourceTerm = -CrossSectionArea * GravityAccelerationAlongAxis;
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::GravitySourceTerm, GravitySourceTerm);
	}

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}
		if (!SegmentPipeActors.IsValidIndex(SegmentIndex))
		{
			continue;
		}
		APipeFluidPipeActor* PipeActor = SegmentPipeActors[SegmentIndex].Get();
		if (!PipeActor)
		{
			continue;
		}
		if (SegmentState.LeftBoundaryConditionType == EFluidBoundaryConditionTypeOneD::FixedFlow)
		{
			if (APipeFluidBasePointActor* FirstEndpoint = PipeActor->PipeEndpointFirst)
			{
				if (FirstEndpoint->SceneNodeKey == SegmentState.LeftSceneNodeKey)
				{
					const float BoundaryAdjacentCellGaugePressure = SegmentState.CellStates[0].Pressure;
					SegmentState.LeftBoundaryFlow = FirstEndpoint->ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(true, BoundaryAdjacentCellGaugePressure);
				}
			}
		}
		if (SegmentState.RightBoundaryConditionType == EFluidBoundaryConditionTypeOneD::FixedFlow)
		{
			const int32 LastBoundaryCellIndex = SegmentState.CellStates.Num() - 1;
			if (APipeFluidBasePointActor* SecondEndpoint = PipeActor->PipeEndpointSecond)
			{
				if (SecondEndpoint->SceneNodeKey == SegmentState.RightSceneNodeKey)
				{
					const float BoundaryAdjacentCellGaugePressure = SegmentState.CellStates[LastBoundaryCellIndex].Pressure;
					SegmentState.RightBoundaryFlow = SecondEndpoint->ComputeRuntimeSignedVolumeFlowRateForOneDimensionPipeBoundary(false, BoundaryAdjacentCellGaugePressure);
				}
			}
		}
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftBoundaryFlowUpload, SegmentState.LeftBoundaryFlow);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightBoundaryFlowUpload, SegmentState.RightBoundaryFlow);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::LeftBoundaryPressure, SegmentState.LeftBoundaryPressure);
		FluidSegment1DWriteFloatToUintTable(SegmentUintTableCpu, SegmentIndex, FluidSegment1DGpuFieldIndex::RightBoundaryPressure, SegmentState.RightBoundaryPressure);
	}

	if (!GpuPressureReadback || !GpuFlowReadback)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(FluidSegment1DGpuStep)(
		[this, PressureScratch, FlowScratch, SimulationStepTime](FRHICommandListImmediate& ImmediateCommands)
		{
			const uint32 SegmentTableBytes = SegmentUintTableCpu.Num() * sizeof(uint32);
			void* SegmentLock = ImmediateCommands.LockBuffer(SegmentUintGpuBuffer, 0, SegmentTableBytes, RLM_WriteOnly);
			FMemory::Memcpy(SegmentLock, SegmentUintTableCpu.GetData(), SegmentTableBytes);
			ImmediateCommands.UnlockBuffer(SegmentUintGpuBuffer);

			const uint32 PressureBytes = TotalCellsGlobal * sizeof(float);
			void* PressureLockA = ImmediateCommands.LockBuffer(PressureGpuBufferA, 0, PressureBytes, RLM_WriteOnly);
			FMemory::Memcpy(PressureLockA, PressureScratch.GetData(), PressureBytes);
			ImmediateCommands.UnlockBuffer(PressureGpuBufferA);

			void* FlowLockA = ImmediateCommands.LockBuffer(FlowGpuBufferA, 0, PressureBytes, RLM_WriteOnly);
			FMemory::Memcpy(FlowLockA, FlowScratch.GetData(), PressureBytes);
			ImmediateCommands.UnlockBuffer(FlowGpuBufferA);

			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			{
				FFluidSegment1DCopyCS::FParameters PassParameters;
				PassParameters.TotalCells = TotalCellsGlobal;
				PassParameters.CurrentPressure = PressureGpuBufferASrv;
				PassParameters.CurrentFlow = FlowGpuBufferASrv;
				PassParameters.NextPressure = PressureGpuBufferBUav;
				PassParameters.NextFlow = FlowGpuBufferBUav;
				TShaderMapRef<FFluidSegment1DCopyCS> ComputeShader(GlobalShaderMap);
				FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(TotalCellsGlobal), static_cast<int32>(FFluidSegment1DCopyCS::ThreadGroupSize)), 1, 1));
			}

			{
				FFluidSegment1DInteriorCS::FParameters PassParameters;
				PassParameters.TotalInteriorCells = TotalInteriorCells;
				PassParameters.SimulationStepTime = SimulationStepTime;
				PassParameters.SegmentUintTable = SegmentUintGpuSrv;
				PassParameters.InteriorWorkPacked = InteriorWorkGpuSrv;
				PassParameters.CurrentPressure = PressureGpuBufferASrv;
				PassParameters.CurrentFlow = FlowGpuBufferASrv;
				PassParameters.NextPressure = PressureGpuBufferBUav;
				PassParameters.NextFlow = FlowGpuBufferBUav;
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
				PassParameters.NextPressure = PressureGpuBufferBUav;
				PassParameters.NextFlow = FlowGpuBufferBUav;
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
				PassParameters.NextPressure = PressureGpuBufferBUav;
				PassParameters.NextFlow = FlowGpuBufferBUav;
				TShaderMapRef<FFluidSegment1DBoundarySourceCS> ComputeShader(GlobalShaderMap);
				FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(SourceBoundaryCount), static_cast<int32>(FFluidSegment1DBoundarySourceCS::ThreadGroupSize)), 1, 1));
			}

			if (PressureConsumerBoundaryCount > 0u)
			{
				FFluidSegment1DBoundaryPressureConsumerCS::FParameters PassParameters;
				PassParameters.PressureConsumerBoundaryCount = PressureConsumerBoundaryCount;
				PassParameters.SegmentUintTable = SegmentUintGpuSrv;
				PassParameters.BoundaryPressureConsumerWorkPacked = PressureConsumerBoundaryWorkGpuSrv;
				PassParameters.NextPressure = PressureGpuBufferBUav;
				PassParameters.NextFlow = FlowGpuBufferBUav;
				TShaderMapRef<FFluidSegment1DBoundaryPressureConsumerCS> ComputeShader(GlobalShaderMap);
				FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(PressureConsumerBoundaryCount), static_cast<int32>(FFluidSegment1DBoundaryPressureConsumerCS::ThreadGroupSize)), 1, 1));
			}

			GpuPressureReadback->EnqueueCopy(ImmediateCommands, PressureGpuBufferB, PressureBytes);
			GpuFlowReadback->EnqueueCopy(ImmediateCommands, FlowGpuBufferB, PressureBytes);
		});
	FlushRenderingCommands();
	const uint32 ReadBytes = TotalCellsGlobal * sizeof(float);
	ENQUEUE_RENDER_COMMAND(FluidSegment1DGpuReadbackLock)(
		[this, &PressureScratch, &FlowScratch, ReadBytes](FRHICommandListImmediate& ImmediateCommands)
		{
			if (!GpuPressureReadback->IsReady() || !GpuFlowReadback->IsReady())
			{
				return;
			}
			void* PressureRead = GpuPressureReadback->Lock(ReadBytes);
			void* FlowRead = GpuFlowReadback->Lock(ReadBytes);
			if (PressureRead && FlowRead)
			{
				FMemory::Memcpy(PressureScratch.GetData(), PressureRead, ReadBytes);
				FMemory::Memcpy(FlowScratch.GetData(), FlowRead, ReadBytes);
			}
			GpuPressureReadback->Unlock();
			GpuFlowReadback->Unlock();
		});
	FlushRenderingCommands();

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		const int32 CellCount = SegmentState.CellStates.Num();
		const uint32 BaseIndex = SegmentCellBaseCpu[SegmentIndex];
		for (int32 LocalIndex = 0; LocalIndex < CellCount; ++LocalIndex)
		{
			const int32 GlobalIndex = static_cast<int32>(BaseIndex) + LocalIndex;
			SegmentState.CellStates[LocalIndex].Pressure = PressureScratch[GlobalIndex];
			SegmentState.CellStates[LocalIndex].FlowRate = FlowScratch[GlobalIndex];
		}
	}
}
