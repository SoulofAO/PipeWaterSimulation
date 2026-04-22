#pragma once

#include "CoreMinimal.h"
#include "Data/FluidData.h"
#include "Subsystems/WorldSubsystem.h"
#include "FluidSegment1DSubsystem.generated.h"

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

private:
	void SimulateStep(float SimulationStepTime);
	void SolveSegmentWaterHammerStep(const FFluidSegmentStateOneD& CurrentSegmentState, float SimulationStepTime, FFluidSegmentStateOneD& NextSegmentState) const;
	void ApplyBoundaryConditions(const FFluidSegmentStateOneD& CurrentSegmentState, FFluidSegmentStateOneD& NextSegmentState) const;
	void UpdateDerivedCellValues(FFluidSegmentStateOneD& SegmentState) const;
	float ComputeStableStepTime(const FFluidSegmentStateOneD& SegmentState) const;
	float GetCrossSectionArea(const FFluidSegmentStateOneD& SegmentState) const;
	bool IsSegmentStateFinite(const FFluidSegmentStateOneD& SegmentState) const;

	UPROPERTY(EditAnywhere, Category = "FluidOneD")
	TArray<FFluidSegmentStateOneD> SegmentStates;

	float AccumulatedTime = 0.0f;
};
