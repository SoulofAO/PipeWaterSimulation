#pragma once

#include "CoreMinimal.h"

class UWorld;

struct FFluidPipeZeroDOneDPhysicsComparisonSummary
{
	int32 ComparedNodeCount = 0;
	int32 ComparedFlowCount = 0;
	float MeanAbsolutePressureDifference = 0.0f;
	float MeanAbsoluteVolumeFlowRateDifference = 0.0f;
	float MaximumPressureOrderOfMagnitudeDifference = 0.0f;
	bool bComparisonSucceeded = false;
};

struct FFluidPipeZeroDOneDPhysicsComparison
{
	static FFluidPipeZeroDOneDPhysicsComparisonSummary CompareCurrentWorldState(UWorld* World);

	static void LogComparisonReport(UWorld* World);
};
