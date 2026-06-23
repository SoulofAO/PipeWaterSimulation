#pragma once

#include "Core/Simulation0D/BaseFluidNetwork0DSimulation.h"
#include "RHI.h"

class FFluidNetwork0DGpuSimulation : public FBaseFluidNetwork0DSimulation
{
public:
	FFluidNetwork0DGpuSimulation();
	~FFluidNetwork0DGpuSimulation();

	virtual bool IsAvailable() const override;
	virtual void Release() override;
	virtual void RebuildFromNetwork(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates, const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, UWorld* SimulationWorld) override;
	virtual void SimulateStep(UWorld* World, TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, float SimulationStepTime) override;

	void SimulateStepGpuOnly(float SimulationStepTime);
	void UploadNodeSourceFlows(const TArray<FFluidNetworkNodeStateZeroD>& NodeStates);
	void WaitForGpuStepCompletion();
	void ReadbackToNetworkStates(TArray<FFluidNetworkNodeStateZeroD>& NodeStates, TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates);
	bool IsGpuStateResident() const;

private:
	void ReleaseInternal();
	void BuildNodeIncidentEdgeLists(const TArray<FFluidNetworkEdgeStateZeroD>& EdgeStates, int32 InNodeCount);
	void DispatchGpuSimulationStep(FRHICommandListImmediate& ImmediateCommands, float SimulationStepTime);

	uint32 NodeCount = 0u;
	uint32 EdgeCount = 0u;
	uint32 TotalNodeIncidents = 0u;

	TArray<uint32> EdgeFromNodeIndexCpu;
	TArray<uint32> EdgeToNodeIndexCpu;
	TArray<float> EdgeResistanceCpu;
	TArray<float> EdgeInertanceCpu;
	TArray<float> EdgeFlowRateCpu;
	TArray<float> NodePressureCpu;
	TArray<float> NodeStoredVolumeCpu;
	TArray<float> NodeSourceFlowCpu;
	TArray<float> NodeComplianceCpu;
	TArray<float> NodeReferenceVolumeCpu;
	TArray<float> NodeReferencePressureCpu;
	TArray<uint32> NodeIncidentEdgeOffsetCpu;
	TArray<uint32> NodeIncidentEdgeIndexCpu;
	TArray<int32> NodeIncidentEdgeSignCpu;

	FBufferRHIRef EdgeFromNodeIndexGpuBuffer;
	FShaderResourceViewRHIRef EdgeFromNodeIndexGpuSrv;
	FBufferRHIRef EdgeToNodeIndexGpuBuffer;
	FShaderResourceViewRHIRef EdgeToNodeIndexGpuSrv;
	FBufferRHIRef EdgeResistanceGpuBuffer;
	FShaderResourceViewRHIRef EdgeResistanceGpuSrv;
	FBufferRHIRef EdgeInertanceGpuBuffer;
	FShaderResourceViewRHIRef EdgeInertanceGpuSrv;
	FBufferRHIRef EdgeFlowRateGpuBuffer;
	FShaderResourceViewRHIRef EdgeFlowRateGpuSrv;
	FUnorderedAccessViewRHIRef EdgeFlowRateGpuUav;
	FBufferRHIRef NodePressureGpuBuffer;
	FShaderResourceViewRHIRef NodePressureGpuSrv;
	FUnorderedAccessViewRHIRef NodePressureGpuUav;
	FBufferRHIRef NodeStoredVolumeGpuBuffer;
	FShaderResourceViewRHIRef NodeStoredVolumeGpuSrv;
	FUnorderedAccessViewRHIRef NodeStoredVolumeGpuUav;
	FBufferRHIRef NodeSourceFlowGpuBuffer;
	FShaderResourceViewRHIRef NodeSourceFlowGpuSrv;
	FUnorderedAccessViewRHIRef NodeSourceFlowGpuUav;
	FBufferRHIRef NodeComplianceGpuBuffer;
	FShaderResourceViewRHIRef NodeComplianceGpuSrv;
	FBufferRHIRef NodeReferenceVolumeGpuBuffer;
	FShaderResourceViewRHIRef NodeReferenceVolumeGpuSrv;
	FBufferRHIRef NodeReferencePressureGpuBuffer;
	FShaderResourceViewRHIRef NodeReferencePressureGpuSrv;
	FBufferRHIRef NodeIncidentEdgeOffsetGpuBuffer;
	FShaderResourceViewRHIRef NodeIncidentEdgeOffsetGpuSrv;
	FBufferRHIRef NodeIncidentEdgeIndexGpuBuffer;
	FShaderResourceViewRHIRef NodeIncidentEdgeIndexGpuSrv;
	FBufferRHIRef NodeIncidentEdgeSignGpuBuffer;
	FShaderResourceViewRHIRef NodeIncidentEdgeSignGpuSrv;

	FGPUFenceRHIRef GpuStepCompletionFence;

	bool bResourcesAllocated = false;
	bool bGpuStateResident = false;
	bool bGpuStepCompletionFenceWritten = false;

	bool bEnableZeroDStateVariableClamping = false;
	float ZeroDMinimumPressure = 0.0f;
	float ZeroDMaximumPressure = 0.0f;
	float ZeroDMinimumVolumeFlowRate = 0.0f;
	float ZeroDMaximumVolumeFlowRate = 0.0f;
	float ZeroDPressureScale = 1.0f;
};
