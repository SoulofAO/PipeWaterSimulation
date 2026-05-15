#pragma once

#include "Core/Simulation1D/BaseFluidSegment1DSimulation.h"
#include "RHI.h"
#include "RHIGPUReadback.h"

class APipeFluidPipeActor;
class UWorld;

class FFluidSegment1DGpuSimulation : public FBaseFluidSegment1DSimulation
{
public:
	FFluidSegment1DGpuSimulation();
	~FFluidSegment1DGpuSimulation();

	virtual bool IsAvailable() const override;

	virtual void Release() override;

	virtual void RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors) override;

	virtual void RebuildFromSegments(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, UWorld* SimulationWorld);

	virtual void SimulateStep(UWorld* World, TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float SimulationStepTime, bool bWaitForReadbackBeforeLock) override;

	void SimulateStepGpuOnly(float SimulationStepTime);

	void ReadbackToSegmentStates(TArray<FFluidSegmentStateOneD>& SegmentStates, bool bWaitForCompletion);

	bool IsGpuStateResident() const;

private:
	void ReleaseInternal();

	void BuildJunctionGpuWorkLists(const TArray<FFluidSegmentStateOneD>& SegmentStates);

	void BakeSegmentUintTableAtImport(const TArray<FFluidSegmentStateOneD>& SegmentStates, const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& SegmentPipeActors, float GravityAcceleration);

	void DispatchGpuSimulationStep(FRHICommandListImmediate& ImmediateCommands, float SimulationStepTime, bool bReadFromA);

	TArray<uint32> SegmentUintTableCpu;
	TArray<uint32> InteriorWorkPackedCpu;
	TArray<uint32> SourceBoundaryWorkPackedCpu;
	TArray<uint32> PressureConsumerBoundaryWorkPackedCpu;
	TArray<uint32> JunctionHeadersPackedCpu;
	TArray<uint32> JunctionIncidentsPackedCpu;

	TArray<uint32> SegmentCellBaseCpu;
	TArray<uint32> SegmentCellCountCpu;

	uint32 TotalCellsGlobal = 0u;
	uint32 TotalInteriorCells = 0u;
	uint32 SegmentCount = 0u;
	uint32 SourceBoundaryCount = 0u;
	uint32 PressureConsumerBoundaryCount = 0u;
	uint32 JunctionCount = 0u;
	uint32 TotalJunctionIncidents = 0u;

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

	FBufferRHIRef JunctionHeadersGpuBuffer;
	FShaderResourceViewRHIRef JunctionHeadersGpuSrv;

	FBufferRHIRef JunctionIncidentsGpuBuffer;
	FShaderResourceViewRHIRef JunctionIncidentsGpuSrv;

	FBufferRHIRef JunctionPressureGpuBuffer;
	FShaderResourceViewRHIRef JunctionPressureGpuSrv;
	FUnorderedAccessViewRHIRef JunctionPressureGpuUav;

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
	bool bGpuStateResident = false;
	bool bReadFromBufferA = true;
};
