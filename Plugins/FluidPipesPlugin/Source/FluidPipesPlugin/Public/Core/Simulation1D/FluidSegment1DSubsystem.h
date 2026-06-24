#pragma once

#include "CoreMinimal.h"
#include "Core/Simulation1D/BaseFluidSegment1DSimulation.h"
#include "Data/FluidData.h"
#include "Other/FluidPipesSimulationSettingsTypes.h"

class ULazyFluidPipesDeveloperSettings;
#include "Subsystems/WorldSubsystem.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtr.h"
#include "FluidSegment1DSubsystem.generated.h"

class APipeFluidPipeActor;

UCLASS()
class FLUIDPIPESPLUGIN_API UFluidSegment1DSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "FluidOneD")
	void ResetSimulationState();

	const TArray<FFluidSegmentStateOneD>& GetSegmentStates() const;

	UFUNCTION(BlueprintCallable, Category = "FluidOneD")
	void ApplyImportedOneDSegments(const TArray<FFluidSegmentStateOneD>& Segments);

	void ApplyImportedOneDSegments(const TArray<FFluidSegmentStateOneD>& Segments, const TArray<APipeFluidPipeActor*>& IncomingPipeActors);

	UFUNCTION(BlueprintCallable, Category = "FluidOneD")
	void RebuildActiveSimulationForCurrentSettings();

	const ULazyFluidPipesDeveloperSettings& GetSimulationSettings() const;

	void RunCpuGameThreadHybridSimulationStep(float SimulationStepTime, const TArray<bool>& SegmentDetailActiveMask);

	TArray<FFluidSegmentStateOneD>& GetMutableSegmentStates();

	const TArray<TWeakObjectPtr<APipeFluidPipeActor>>& GetSegmentPipeActors() const;

private:
	struct FFluidOneDJunctionEndpointIncident
	{
		int32 SegmentIndex = INDEX_NONE;
		bool bLeftEndpoint = false;
	};

	void SimulateStep(float SimulationStepTime);
	void UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors();
	void UpdateOneDimensionBoundaryFlowsFromAttachedPipePointActors(TArray<FFluidSegmentStateOneD>& TargetSegmentStates);
	void RebuildJunctionSceneNodeKeyTopology(const TArray<FFluidSegmentStateOneD>& SourceSegmentStates);
	void ApplyJunctionCouplingToNextSegmentStates(const TArray<FFluidSegmentStateOneD>& CurrentSegmentStates, TArray<FFluidSegmentStateOneD>& NextSegmentStates) const;
	void SolveSegmentWaterHammerStep(const FFluidSegmentStateOneD& CurrentSegmentState, float SimulationStepTime, float GravityAccelerationAlongAxis, FFluidSegmentStateOneD& NextSegmentState) const;
	void ApplyBoundaryConditions(const FFluidSegmentStateOneD& CurrentSegmentState, FFluidSegmentStateOneD& NextSegmentState) const;
	void UpdateDerivedCellValues(FFluidSegmentStateOneD& SegmentState) const;
	float ComputeStableStepTime(const FFluidSegmentStateOneD& SegmentState) const;
	float GetCrossSectionArea(const FFluidSegmentStateOneD& SegmentState) const;
	bool IsSegmentStateFinite(const FFluidSegmentStateOneD& SegmentState) const;
	void DrawDebugOneDSegments(int32 DebugLevel) const;
	void CollectSegmentIndicesWithinDebugDrawDistance(TArray<int32>& OutSegmentIndices) const;
	void ReadbackAndDrawOffGameThreadOneDDebug(int32 OneDWorldDebugDetailLevel);
	void DrawOneDDebugForSegmentIndices(int32 OneDWorldDebugDetailLevel, const TArray<int32>& SegmentIndicesWithinDebugDrawDistance);
	void EnsureActiveOneDSimulationMatchesSettings(const ULazyFluidPipesDeveloperSettings& Settings);
	bool UsesOffGameThreadOneDSimulationState() const;
	static FString BuildOneDSimulationBackendDisplayName(EFluidSegmentSimulationOneDBackend Backend);

	UPROPERTY(EditAnywhere, Category = "FluidOneD")
	TArray<FFluidSegmentStateOneD> SegmentStates;

	TArray<TWeakObjectPtr<APipeFluidPipeActor>> SegmentPipeActors;

	TMap<int32, TArray<FFluidOneDJunctionEndpointIncident>> JunctionSceneNodeKeyToIncidentEndpoints;

	float AccumulatedTime = 0.0f;

	TUniquePtr<FBaseFluidSegment1DSimulation> ActiveSimulation;
	EFluidSegmentSimulationOneDBackend ActiveOneDSimulationBackend = EFluidSegmentSimulationOneDBackend::CpuGameThread;
};
