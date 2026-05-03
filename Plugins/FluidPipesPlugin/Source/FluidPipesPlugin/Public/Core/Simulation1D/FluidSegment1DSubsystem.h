#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"
#include "Subsystems/WorldSubsystem.h"
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

private:
	void SimulateStep(float SimulationStepTime);
	void SolveSegmentWaterHammerStep(const FFluidSegmentStateOneD& CurrentSegmentState, float SimulationStepTime, FFluidSegmentStateOneD& NextSegmentState) const;
	void ApplyBoundaryConditions(const FFluidSegmentStateOneD& CurrentSegmentState, FFluidSegmentStateOneD& NextSegmentState) const;
	void UpdateDerivedCellValues(FFluidSegmentStateOneD& SegmentState) const;
	float ComputeStableStepTime(const FFluidSegmentStateOneD& SegmentState) const;
	float GetCrossSectionArea(const FFluidSegmentStateOneD& SegmentState) const;
	bool IsSegmentStateFinite(const FFluidSegmentStateOneD& SegmentState) const;
	void DrawDebugOneDSegments(int32 DebugLevel) const;

	UPROPERTY(EditAnywhere, Category = "FluidOneD")
	TArray<FFluidSegmentStateOneD> SegmentStates;

	TArray<TWeakObjectPtr<APipeFluidPipeActor>> SegmentPipeActors;

	float AccumulatedTime = 0.0f;
};
