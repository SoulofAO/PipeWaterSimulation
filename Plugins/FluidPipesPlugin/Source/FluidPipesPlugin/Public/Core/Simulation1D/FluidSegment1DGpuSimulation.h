#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "UObject/WeakObjectPtr.h"

class APipeFluidPipeActor;
class UWorld;

class FFluidSegment1DGpuSimulation
{
public:
	FFluidSegment1DGpuSimulation();
	~FFluidSegment1DGpuSimulation();

	bool IsComputeShaderPathAvailable() const;

	void Release();

	void RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors);

	void ExecuteSimulationStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float SimulationStepTime);

private:
	void ReleaseInternal();

	TArray<uint32> SegmentUintTableCpu;
	TArray<uint32> InteriorWorkPackedCpu;
	TArray<uint32> SourceBoundaryWorkPackedCpu;
	TArray<uint32> PressureConsumerBoundaryWorkPackedCpu;

	TArray<uint32> SegmentCellBaseCpu;
	TArray<uint32> SegmentCellCountCpu;

	uint32 TotalCellsGlobal = 0u;
	uint32 TotalInteriorCells = 0u;
	uint32 SegmentCount = 0u;
	uint32 SourceBoundaryCount = 0u;
	uint32 PressureConsumerBoundaryCount = 0u;

	TUniquePtr<FRHIGPUBufferReadback> GpuPressureReadback;
	TUniquePtr<FRHIGPUBufferReadback> GpuFlowReadback;

	FBufferRHIRef SegmentUintGpuBuffer;
	FShaderResourceViewRHIRef SegmentUintGpuSrv;

	FBufferRHIRef InteriorWorkGpuBuffer;
	FShaderResourceViewRHIRef InteriorWorkGpuSrv;

	FBufferRHIRef SourceBoundaryWorkGpuBuffer;
	FShaderResourceViewRHIRef SourceBoundaryWorkGpuSrv;

	FBufferRHIRef PressureConsumerBoundaryWorkGpuBuffer;
	FShaderResourceViewRHIRef PressureConsumerBoundaryWorkGpuSrv;

	FBufferRHIRef PressureGpuBufferA;
	FBufferRHIRef PressureGpuBufferB;
	FBufferRHIRef FlowGpuBufferA;
	FBufferRHIRef FlowGpuBufferB;

	FShaderResourceViewRHIRef PressureGpuBufferASrv;
	FShaderResourceViewRHIRef PressureGpuBufferBSrv;
	FShaderResourceViewRHIRef FlowGpuBufferASrv;
	FShaderResourceViewRHIRef FlowGpuBufferBSrv;

	FUnorderedAccessViewRHIRef PressureGpuBufferAUav;
	FUnorderedAccessViewRHIRef PressureGpuBufferBUav;
	FUnorderedAccessViewRHIRef FlowGpuBufferAUav;
	FUnorderedAccessViewRHIRef FlowGpuBufferBUav;

	bool bResourcesAllocated = false;
};
