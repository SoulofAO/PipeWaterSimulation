#include "Core/Simulation0D/FluidNetwork0DGpuSimulation.h"

#include "GlobalShader.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Other/FluidPipesSimulationSettingsLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "ShaderParameterStruct.h"

class FFluidNetwork0DEdgeFlowCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidNetwork0DEdgeFlowCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidNetwork0DEdgeFlowCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, EdgeCount)
		SHADER_PARAMETER(float, SimulationStepTime)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, EdgeFromNodeIndex)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, EdgeToNodeIndex)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, EdgeResistance)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, EdgeInertance)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, NodePressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, EdgeFlowRate)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidNetwork0DEdgeFlowCS, "/Plugin/FluidPipesPlugin/Private/FluidNetwork0DEdgeFlow.usf", "FluidNetwork0DEdgeFlowMain", SF_Compute);

class FFluidNetwork0DIntegrateVolumesCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidNetwork0DIntegrateVolumesCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidNetwork0DIntegrateVolumesCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NodeCount)
		SHADER_PARAMETER(float, SimulationStepTime)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, NodeIncidentEdgeOffset)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, NodeIncidentEdgeIndex)
		SHADER_PARAMETER_SRV(StructuredBuffer<int>, NodeIncidentEdgeSign)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, EdgeFlowRate)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, NodeSourceFlow)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NodeStoredVolume)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidNetwork0DIntegrateVolumesCS, "/Plugin/FluidPipesPlugin/Private/FluidNetwork0DIntegrateVolumes.usf", "FluidNetwork0DIntegrateVolumesMain", SF_Compute);

class FFluidNetwork0DUpdatePressuresCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidNetwork0DUpdatePressuresCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidNetwork0DUpdatePressuresCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NodeCount)
		SHADER_PARAMETER(float, ZeroDPressureScale)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, NodeStoredVolume)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, NodeCompliance)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, NodeReferenceVolume)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, NodeReferencePressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NodePressure)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidNetwork0DUpdatePressuresCS, "/Plugin/FluidPipesPlugin/Private/FluidNetwork0DUpdatePressures.usf", "FluidNetwork0DUpdatePressuresMain", SF_Compute);

class FFluidNetwork0DStateClampCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFluidNetwork0DStateClampCS);
	SHADER_USE_PARAMETER_STRUCT(FFluidNetwork0DStateClampCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize = 64u;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NodeCount)
		SHADER_PARAMETER(uint32, EdgeCount)
		SHADER_PARAMETER(uint32, bEnableClamping)
		SHADER_PARAMETER(float, MinimumPressure)
		SHADER_PARAMETER(float, MaximumPressure)
		SHADER_PARAMETER(float, MinimumVolumeFlowRate)
		SHADER_PARAMETER(float, MaximumVolumeFlowRate)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NodePressure)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, NodeSourceFlow)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, EdgeFlowRate)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFluidNetwork0DStateClampCS, "/Plugin/FluidPipesPlugin/Private/FluidNetwork0DStateClamp.usf", "FluidNetwork0DStateClampMain", SF_Compute);

FFluidNetwork0DGpuSimulation::FFluidNetwork0DGpuSimulation() = default;

FFluidNetwork0DGpuSimulation::~FFluidNetwork0DGpuSimulation()
{
	Release();
}

bool FFluidNetwork0DGpuSimulation::IsAvailable() const
{
	return GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
}

void FFluidNetwork0DGpuSimulation::Release()
{
	ReleaseInternal();
}

void FFluidNetwork0DGpuSimulation::ReleaseInternal()
{
	WaitForGpuStepCompletion();
	GpuStepCompletionFence.SafeRelease();
	bGpuStepCompletionFenceWritten = false;
	bGpuStateResident = false;
	bResourcesAllocated = false;
	ENQUEUE_RENDER_COMMAND(FluidNetwork0DGpuRelease)(
		[this](FRHICommandListImmediate& ImmediateCommands)
		{
			EdgeFromNodeIndexGpuBuffer.SafeRelease();
			EdgeFromNodeIndexGpuSrv.SafeRelease();
			EdgeToNodeIndexGpuBuffer.SafeRelease();
			EdgeToNodeIndexGpuSrv.SafeRelease();
			EdgeResistanceGpuBuffer.SafeRelease();
			EdgeResistanceGpuSrv.SafeRelease();
			EdgeInertanceGpuBuffer.SafeRelease();
			EdgeInertanceGpuSrv.SafeRelease();
			EdgeFlowRateGpuBuffer.SafeRelease();
			EdgeFlowRateGpuSrv.SafeRelease();
			EdgeFlowRateGpuUav.SafeRelease();
			NodePressureGpuBuffer.SafeRelease();
			NodePressureGpuSrv.SafeRelease();
			NodePressureGpuUav.SafeRelease();
			NodeStoredVolumeGpuBuffer.SafeRelease();
			NodeStoredVolumeGpuSrv.SafeRelease();
			NodeStoredVolumeGpuUav.SafeRelease();
			NodeSourceFlowGpuBuffer.SafeRelease();
			NodeSourceFlowGpuSrv.SafeRelease();
			NodeSourceFlowGpuUav.SafeRelease();
			NodeComplianceGpuBuffer.SafeRelease();
			NodeComplianceGpuSrv.SafeRelease();
			NodeReferenceVolumeGpuBuffer.SafeRelease();
			NodeReferenceVolumeGpuSrv.SafeRelease();
			NodeReferencePressureGpuBuffer.SafeRelease();
			NodeReferencePressureGpuSrv.SafeRelease();
			NodeIncidentEdgeOffsetGpuBuffer.SafeRelease();
			NodeIncidentEdgeOffsetGpuSrv.SafeRelease();
			NodeIncidentEdgeIndexGpuBuffer.SafeRelease();
			NodeIncidentEdgeIndexGpuSrv.SafeRelease();
			NodeIncidentEdgeSignGpuBuffer.SafeRelease();
			NodeIncidentEdgeSignGpuSrv.SafeRelease();
		});
	FlushRenderingCommands();
}

void FFluidNetwork0DGpuSimulation::BuildNodeIncidentEdgeLists(const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, int32 InNodeCount)
{
	TArray<TArray<TPair<int32, int32>>> NodeIncidentLists;
	NodeIncidentLists.SetNum(InNodeCount);

	for (int32 EdgeIndex = 0; EdgeIndex < EdgeStates.Num(); ++EdgeIndex)
	{
		const FFluidNetworkEdgeStateZeroD& EdgeState = EdgeStates[EdgeIndex];
		if (NodeIncidentLists.IsValidIndex(EdgeState.FromNodeIndex))
		{
			NodeIncidentLists[EdgeState.FromNodeIndex].Emplace(EdgeIndex, -1);
		}
		if (NodeIncidentLists.IsValidIndex(EdgeState.ToNodeIndex))
		{
			NodeIncidentLists[EdgeState.ToNodeIndex].Emplace(EdgeIndex, 1);
		}
	}

	NodeIncidentEdgeOffsetCpu.SetNum(InNodeCount + 1);
	NodeIncidentEdgeIndexCpu.Reset();
	NodeIncidentEdgeSignCpu.Reset();
	uint32 RunningOffset = 0u;
	for (int32 NodeIndex = 0; NodeIndex < InNodeCount; ++NodeIndex)
	{
		NodeIncidentEdgeOffsetCpu[NodeIndex] = RunningOffset;
		for (const TPair<int32, int32>& IncidentEntry : NodeIncidentLists[NodeIndex])
		{
			NodeIncidentEdgeIndexCpu.Add(static_cast<uint32>(IncidentEntry.Key));
			NodeIncidentEdgeSignCpu.Add(IncidentEntry.Value);
			++RunningOffset;
		}
	}
	NodeIncidentEdgeOffsetCpu[InNodeCount] = RunningOffset;
	TotalNodeIncidents = RunningOffset;
}

void FFluidNetwork0DGpuSimulation::RebuildFromNetwork(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, UWorld* SimulationWorld)
{
	ReleaseInternal();

	const ULazyFluidPipesDeveloperSettings& Settings = FFluidPipesSimulationSettingsLibrary::ResolveSimulationSettings(SimulationWorld);
	bEnableZeroDStateVariableClamping = Settings.EnableZeroDSimulationStateVariableClamping;
	ZeroDMinimumPressure = Settings.ZeroDMinimumPressure;
	ZeroDMaximumPressure = Settings.ZeroDMaximumPressure;
	ZeroDMinimumVolumeFlowRate = Settings.ZeroDMinimumVolumeFlowRate;
	ZeroDMaximumVolumeFlowRate = Settings.ZeroDMaximumVolumeFlowRate;
	ZeroDPressureScale = Settings.ZeroDPressureScale;

	NodeCount = static_cast<uint32>(NodeStates.Num());
	EdgeCount = static_cast<uint32>(EdgeStates.Num());
	if (NodeCount == 0u || EdgeCount == 0u)
	{
		return;
	}

	EdgeFromNodeIndexCpu.SetNum(EdgeStates.Num());
	EdgeToNodeIndexCpu.SetNum(EdgeStates.Num());
	EdgeResistanceCpu.SetNum(EdgeStates.Num());
	EdgeInertanceCpu.SetNum(EdgeStates.Num());
	EdgeFlowRateCpu.SetNum(EdgeStates.Num());
	NodePressureCpu.SetNum(NodeStates.Num());
	NodeStoredVolumeCpu.SetNum(NodeStates.Num());
	NodeSourceFlowCpu.SetNum(NodeStates.Num());
	NodeComplianceCpu.SetNum(NodeStates.Num());
	NodeReferenceVolumeCpu.SetNum(NodeStates.Num());
	NodeReferencePressureCpu.SetNum(NodeStates.Num());

	for (int32 EdgeIndex = 0; EdgeIndex < EdgeStates.Num(); ++EdgeIndex)
	{
		const FFluidNetworkEdgeStateZeroD& EdgeState = EdgeStates[EdgeIndex];
		EdgeFromNodeIndexCpu[EdgeIndex] = static_cast<uint32>(FMath::Max(EdgeState.FromNodeIndex, 0));
		EdgeToNodeIndexCpu[EdgeIndex] = static_cast<uint32>(FMath::Max(EdgeState.ToNodeIndex, 0));
		EdgeResistanceCpu[EdgeIndex] = EdgeState.Resistance;
		EdgeInertanceCpu[EdgeIndex] = EdgeState.Inertance;
		EdgeFlowRateCpu[EdgeIndex] = EdgeState.FlowRate;
	}

	for (int32 NodeIndex = 0; NodeIndex < NodeStates.Num(); ++NodeIndex)
	{
		const FFluidNetworkNodeStateZeroD& NodeState = NodeStates[NodeIndex];
		NodePressureCpu[NodeIndex] = NodeState.Pressure;
		NodeStoredVolumeCpu[NodeIndex] = NodeState.StoredVolume;
		NodeSourceFlowCpu[NodeIndex] = NodeState.SourceFlow;
		NodeComplianceCpu[NodeIndex] = NodeState.Compliance;
		NodeReferenceVolumeCpu[NodeIndex] = NodeState.ReferenceVolume;
		NodeReferencePressureCpu[NodeIndex] = NodeState.ReferencePressure;
	}

	BuildNodeIncidentEdgeLists(EdgeStates, NodeStates.Num());

	const uint32 SafeNodeCount = FMath::Max(NodeCount, 1u);
	const uint32 SafeEdgeCount = FMath::Max(EdgeCount, 1u);
	const uint32 SafeIncidentCount = FMath::Max(TotalNodeIncidents, 1u);
	const uint32 SafeIncidentOffsetCount = FMath::Max(static_cast<uint32>(NodeIncidentEdgeOffsetCpu.Num()), 1u);

	ENQUEUE_RENDER_COMMAND(FluidNetwork0DGpuAlloc)(
		[this, SafeNodeCount, SafeEdgeCount, SafeIncidentCount, SafeIncidentOffsetCount](FRHICommandListImmediate& ImmediateCommands)
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

			const uint32 UintStride = sizeof(uint32);
			const uint32 FloatStride = sizeof(float);
			const uint32 IntStride = sizeof(int32);
			CreateStructuredSrvOnly(EdgeFromNodeIndexGpuBuffer, EdgeFromNodeIndexGpuSrv, TEXT("FluidNetwork0DEdgeFromNode"), UintStride, SafeEdgeCount, EdgeFromNodeIndexCpu.GetData());
			CreateStructuredSrvOnly(EdgeToNodeIndexGpuBuffer, EdgeToNodeIndexGpuSrv, TEXT("FluidNetwork0DEdgeToNode"), UintStride, SafeEdgeCount, EdgeToNodeIndexCpu.GetData());
			CreateStructuredSrvOnly(EdgeResistanceGpuBuffer, EdgeResistanceGpuSrv, TEXT("FluidNetwork0DEdgeResistance"), FloatStride, SafeEdgeCount, EdgeResistanceCpu.GetData());
			CreateStructuredSrvOnly(EdgeInertanceGpuBuffer, EdgeInertanceGpuSrv, TEXT("FluidNetwork0DEdgeInertance"), FloatStride, SafeEdgeCount, EdgeInertanceCpu.GetData());
			CreateStructuredSrvUav(EdgeFlowRateGpuBuffer, EdgeFlowRateGpuSrv, EdgeFlowRateGpuUav, TEXT("FluidNetwork0DEdgeFlowRate"), FloatStride, SafeEdgeCount, EdgeFlowRateCpu.GetData());
			CreateStructuredSrvUav(NodePressureGpuBuffer, NodePressureGpuSrv, NodePressureGpuUav, TEXT("FluidNetwork0DNodePressure"), FloatStride, SafeNodeCount, NodePressureCpu.GetData());
			CreateStructuredSrvUav(NodeStoredVolumeGpuBuffer, NodeStoredVolumeGpuSrv, NodeStoredVolumeGpuUav, TEXT("FluidNetwork0DNodeStoredVolume"), FloatStride, SafeNodeCount, NodeStoredVolumeCpu.GetData());
			CreateStructuredSrvUav(NodeSourceFlowGpuBuffer, NodeSourceFlowGpuSrv, NodeSourceFlowGpuUav, TEXT("FluidNetwork0DNodeSourceFlow"), FloatStride, SafeNodeCount, NodeSourceFlowCpu.GetData());
			CreateStructuredSrvOnly(NodeComplianceGpuBuffer, NodeComplianceGpuSrv, TEXT("FluidNetwork0DNodeCompliance"), FloatStride, SafeNodeCount, NodeComplianceCpu.GetData());
			CreateStructuredSrvOnly(NodeReferenceVolumeGpuBuffer, NodeReferenceVolumeGpuSrv, TEXT("FluidNetwork0DNodeReferenceVolume"), FloatStride, SafeNodeCount, NodeReferenceVolumeCpu.GetData());
			CreateStructuredSrvOnly(NodeReferencePressureGpuBuffer, NodeReferencePressureGpuSrv, TEXT("FluidNetwork0DNodeReferencePressure"), FloatStride, SafeNodeCount, NodeReferencePressureCpu.GetData());
			CreateStructuredSrvOnly(NodeIncidentEdgeOffsetGpuBuffer, NodeIncidentEdgeOffsetGpuSrv, TEXT("FluidNetwork0DNodeIncidentEdgeOffset"), UintStride, SafeIncidentOffsetCount, NodeIncidentEdgeOffsetCpu.GetData());
			CreateStructuredSrvOnly(NodeIncidentEdgeIndexGpuBuffer, NodeIncidentEdgeIndexGpuSrv, TEXT("FluidNetwork0DNodeIncidentEdgeIndex"), UintStride, SafeIncidentCount, NodeIncidentEdgeIndexCpu.GetData());
			CreateStructuredSrvOnly(NodeIncidentEdgeSignGpuBuffer, NodeIncidentEdgeSignGpuSrv, TEXT("FluidNetwork0DNodeIncidentEdgeSign"), IntStride, SafeIncidentCount, NodeIncidentEdgeSignCpu.GetData());
		});
	FlushRenderingCommands();
	bResourcesAllocated = true;
	bGpuStateResident = true;
}

void FFluidNetwork0DGpuSimulation::DispatchGpuSimulationStep(FRHICommandListImmediate& ImmediateCommands, float SimulationStepTime)
{
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	{
		FFluidNetwork0DEdgeFlowCS::FParameters PassParameters;
		PassParameters.EdgeCount = EdgeCount;
		PassParameters.SimulationStepTime = SimulationStepTime;
		PassParameters.EdgeFromNodeIndex = EdgeFromNodeIndexGpuSrv;
		PassParameters.EdgeToNodeIndex = EdgeToNodeIndexGpuSrv;
		PassParameters.EdgeResistance = EdgeResistanceGpuSrv;
		PassParameters.EdgeInertance = EdgeInertanceGpuSrv;
		PassParameters.NodePressure = NodePressureGpuSrv;
		PassParameters.EdgeFlowRate = EdgeFlowRateGpuUav;
		TShaderMapRef<FFluidNetwork0DEdgeFlowCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(EdgeCount), static_cast<int32>(FFluidNetwork0DEdgeFlowCS::ThreadGroupSize)), 1, 1));
	}

	{
		FFluidNetwork0DIntegrateVolumesCS::FParameters PassParameters;
		PassParameters.NodeCount = NodeCount;
		PassParameters.SimulationStepTime = SimulationStepTime;
		PassParameters.NodeIncidentEdgeOffset = NodeIncidentEdgeOffsetGpuSrv;
		PassParameters.NodeIncidentEdgeIndex = NodeIncidentEdgeIndexGpuSrv;
		PassParameters.NodeIncidentEdgeSign = NodeIncidentEdgeSignGpuSrv;
		PassParameters.EdgeFlowRate = EdgeFlowRateGpuSrv;
		PassParameters.NodeSourceFlow = NodeSourceFlowGpuSrv;
		PassParameters.NodeStoredVolume = NodeStoredVolumeGpuUav;
		TShaderMapRef<FFluidNetwork0DIntegrateVolumesCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(NodeCount), static_cast<int32>(FFluidNetwork0DIntegrateVolumesCS::ThreadGroupSize)), 1, 1));
	}

	{
		FFluidNetwork0DUpdatePressuresCS::FParameters PassParameters;
		PassParameters.NodeCount = NodeCount;
		PassParameters.ZeroDPressureScale = ZeroDPressureScale;
		PassParameters.NodeStoredVolume = NodeStoredVolumeGpuSrv;
		PassParameters.NodeCompliance = NodeComplianceGpuSrv;
		PassParameters.NodeReferenceVolume = NodeReferenceVolumeGpuSrv;
		PassParameters.NodeReferencePressure = NodeReferencePressureGpuSrv;
		PassParameters.NodePressure = NodePressureGpuUav;
		TShaderMapRef<FFluidNetwork0DUpdatePressuresCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::Dispatch(ImmediateCommands, ComputeShader, PassParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(NodeCount), static_cast<int32>(FFluidNetwork0DUpdatePressuresCS::ThreadGroupSize)), 1, 1));
	}

	if (bEnableZeroDStateVariableClamping && (NodeCount > 0u || EdgeCount > 0u))
	{
		FFluidNetwork0DStateClampCS::FParameters ClampParameters;
		ClampParameters.NodeCount = NodeCount;
		ClampParameters.EdgeCount = EdgeCount;
		ClampParameters.bEnableClamping = 1u;
		ClampParameters.MinimumPressure = ZeroDMinimumPressure;
		ClampParameters.MaximumPressure = ZeroDMaximumPressure;
		ClampParameters.MinimumVolumeFlowRate = ZeroDMinimumVolumeFlowRate;
		ClampParameters.MaximumVolumeFlowRate = ZeroDMaximumVolumeFlowRate;
		ClampParameters.NodePressure = NodePressureGpuUav;
		ClampParameters.NodeSourceFlow = NodeSourceFlowGpuUav;
		ClampParameters.EdgeFlowRate = EdgeFlowRateGpuUav;
		TShaderMapRef<FFluidNetwork0DStateClampCS> ClampShader(GlobalShaderMap);
		const uint32 TotalClampThreads = NodeCount + EdgeCount;
		FComputeShaderUtils::Dispatch(ImmediateCommands, ClampShader, ClampParameters, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(TotalClampThreads), static_cast<int32>(FFluidNetwork0DStateClampCS::ThreadGroupSize)), 1, 1));
	}
}

void FFluidNetwork0DGpuSimulation::UploadNodeSourceFlows(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates)
{
	if (!bResourcesAllocated || !bGpuStateResident || NodeCount == 0u)
	{
		return;
	}

	NodeSourceFlowCpu.SetNum(NodeStates.Num());
	for (int32 NodeIndex = 0; NodeIndex < NodeStates.Num(); ++NodeIndex)
	{
		NodeSourceFlowCpu[NodeIndex] = NodeStates[NodeIndex].SourceFlow;
	}

	ENQUEUE_RENDER_COMMAND(FluidNetwork0DGpuUploadSourceFlows)(
		[this](FRHICommandListImmediate& ImmediateCommands)
		{
			if (!NodeSourceFlowGpuBuffer.IsValid())
			{
				return;
			}
			const uint32 UploadBytes = static_cast<uint32>(NodeSourceFlowCpu.Num()) * static_cast<uint32>(sizeof(float));
			void* DestinationData = ImmediateCommands.LockBuffer(NodeSourceFlowGpuBuffer, 0, UploadBytes, RLM_WriteOnly);
			FMemory::Memcpy(DestinationData, NodeSourceFlowCpu.GetData(), UploadBytes);
			ImmediateCommands.UnlockBuffer(NodeSourceFlowGpuBuffer);
		});
}

void FFluidNetwork0DGpuSimulation::SimulateStepGpuOnly(float SimulationStepTime)
{
	if (!bResourcesAllocated || !bGpuStateResident || NodeCount == 0u || EdgeCount == 0u || !IsAvailable())
	{
		bGpuStepCompletionFenceWritten = false;
		return;
	}

	ENQUEUE_RENDER_COMMAND(FluidNetwork0DGpuStepGpuOnly)(
		[this, SimulationStepTime](FRHICommandListImmediate& ImmediateCommands)
		{
			DispatchGpuSimulationStep(ImmediateCommands, SimulationStepTime);
			if (!GpuStepCompletionFence.IsValid())
			{
				GpuStepCompletionFence = RHICreateGPUFence(TEXT("FluidNetwork0DGpuStepCompletionFence"));
			}
			GpuStepCompletionFence->Clear();
			ImmediateCommands.WriteGPUFence(GpuStepCompletionFence);
		});
	bGpuStepCompletionFenceWritten = true;
}

void FFluidNetwork0DGpuSimulation::WaitForGpuStepCompletion()
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
}

bool FFluidNetwork0DGpuSimulation::IsGpuStateResident() const
{
	return bGpuStateResident;
}

void FFluidNetwork0DGpuSimulation::SimulateStep(UWorld* World, TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime)
{
	UploadNodeSourceFlows(NodeStates);
	SimulateStepGpuOnly(SimulationStepTime);
}

void FFluidNetwork0DGpuSimulation::ReadbackToNetworkStates(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates)
{
	if (!bResourcesAllocated || !bGpuStateResident || NodeCount == 0u)
	{
		return;
	}

	WaitForGpuStepCompletion();

	const TSharedPtr<TArray<float>, ESPMode::ThreadSafe> ReadbackPressure = MakeShared<TArray<float>, ESPMode::ThreadSafe>();
	const TSharedPtr<TArray<float>, ESPMode::ThreadSafe> ReadbackStoredVolume = MakeShared<TArray<float>, ESPMode::ThreadSafe>();
	const TSharedPtr<TArray<float>, ESPMode::ThreadSafe> ReadbackSourceFlow = MakeShared<TArray<float>, ESPMode::ThreadSafe>();
	const TSharedPtr<TArray<float>, ESPMode::ThreadSafe> ReadbackEdgeFlowRate = MakeShared<TArray<float>, ESPMode::ThreadSafe>();
	ReadbackPressure->SetNumUninitialized(static_cast<int32>(NodeCount));
	ReadbackStoredVolume->SetNumUninitialized(static_cast<int32>(NodeCount));
	ReadbackSourceFlow->SetNumUninitialized(static_cast<int32>(NodeCount));
	ReadbackEdgeFlowRate->SetNumUninitialized(static_cast<int32>(EdgeCount));

	ENQUEUE_RENDER_COMMAND(FluidNetwork0DGpuReadback)(
		[this, ReadbackPressure, ReadbackStoredVolume, ReadbackSourceFlow, ReadbackEdgeFlowRate](FRHICommandListImmediate& ImmediateCommands)
		{
			const uint32 NodeByteCount = NodeCount * static_cast<uint32>(sizeof(float));
			const uint32 EdgeByteCount = EdgeCount * static_cast<uint32>(sizeof(float));
			void* NodePressureData = ImmediateCommands.LockBuffer(NodePressureGpuBuffer, 0, NodeByteCount, RLM_ReadOnly);
			FMemory::Memcpy(ReadbackPressure->GetData(), NodePressureData, NodeByteCount);
			ImmediateCommands.UnlockBuffer(NodePressureGpuBuffer);
			void* NodeStoredVolumeData = ImmediateCommands.LockBuffer(NodeStoredVolumeGpuBuffer, 0, NodeByteCount, RLM_ReadOnly);
			FMemory::Memcpy(ReadbackStoredVolume->GetData(), NodeStoredVolumeData, NodeByteCount);
			ImmediateCommands.UnlockBuffer(NodeStoredVolumeGpuBuffer);
			void* NodeSourceFlowData = ImmediateCommands.LockBuffer(NodeSourceFlowGpuBuffer, 0, NodeByteCount, RLM_ReadOnly);
			FMemory::Memcpy(ReadbackSourceFlow->GetData(), NodeSourceFlowData, NodeByteCount);
			ImmediateCommands.UnlockBuffer(NodeSourceFlowGpuBuffer);
			void* EdgeFlowRateData = ImmediateCommands.LockBuffer(EdgeFlowRateGpuBuffer, 0, EdgeByteCount, RLM_ReadOnly);
			FMemory::Memcpy(ReadbackEdgeFlowRate->GetData(), EdgeFlowRateData, EdgeByteCount);
			ImmediateCommands.UnlockBuffer(EdgeFlowRateGpuBuffer);
		});
	FlushRenderingCommands();

	const int32 NodeUpdateCount = FMath::Min(NodeStates.Num(), static_cast<int32>(NodeCount));
	for (int32 NodeIndex = 0; NodeIndex < NodeUpdateCount; ++NodeIndex)
	{
		NodeStates[NodeIndex].Pressure = (*ReadbackPressure)[NodeIndex];
		NodeStates[NodeIndex].StoredVolume = (*ReadbackStoredVolume)[NodeIndex];
		NodeStates[NodeIndex].SourceFlow = (*ReadbackSourceFlow)[NodeIndex];
	}

	const int32 EdgeUpdateCount = FMath::Min(EdgeStates.Num(), static_cast<int32>(EdgeCount));
	for (int32 EdgeIndex = 0; EdgeIndex < EdgeUpdateCount; ++EdgeIndex)
	{
		EdgeStates[EdgeIndex].FlowRate = (*ReadbackEdgeFlowRate)[EdgeIndex];
	}
}
