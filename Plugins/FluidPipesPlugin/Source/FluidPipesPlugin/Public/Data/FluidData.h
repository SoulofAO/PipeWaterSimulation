// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FluidData.generated.h"

UENUM(BlueprintType)
enum class EFluidBoundaryConditionTypeOneD : uint8
{
	Reflective,
	FixedPressure,
	FixedFlow
};

UENUM(BlueprintType)
enum class EFluidSceneEndpointKind : uint8
{
	Face,
	Source,
	Consumer
};

UENUM(BlueprintType)
enum class EFluidOneDJunctionPressurePolicy : uint8
{
	AverageNeighborPressure,
	FixedPressure
};

USTRUCT(BlueprintType)
struct FFluidNetworkNodeStateZeroD
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	FName NodeName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	float Pressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	float StoredVolume = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD", meta = (ClampMin = "0.000001", UIMin = "0.000001"))
	float Compliance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	float ReferenceVolume = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	float ReferencePressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	float SourceFlow = 0.0f;
};

USTRUCT(BlueprintType)
struct FFluidNetworkEdgeStateZeroD
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	int32 FromNodeIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	int32 ToNodeIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	float Resistance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Inertance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	float FlowRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FromNodeFluidComplianceContribution = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ToNodeFluidComplianceContribution = 0.0f;
};

USTRUCT(BlueprintType)
struct FFluidSegmentCellStateOneD
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float Pressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float Velocity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float FillRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float FlowRate = 0.0f;
};

USTRUCT(BlueprintType)
struct FFluidSegmentStateOneD
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	FName SegmentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float SegmentLength = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float CellLength = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float WaveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float PipeDiameter = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FrictionFactor = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float Density = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	EFluidBoundaryConditionTypeOneD LeftBoundaryConditionType = EFluidBoundaryConditionTypeOneD::Reflective;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float LeftBoundaryPressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float LeftBoundaryFlow = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	EFluidBoundaryConditionTypeOneD RightBoundaryConditionType = EFluidBoundaryConditionTypeOneD::Reflective;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float RightBoundaryPressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	float RightBoundaryFlow = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	int32 LeftSceneNodeKey = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	int32 RightSceneNodeKey = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	TArray<FFluidSegmentCellStateOneD> CellStates;
};

USTRUCT(BlueprintType)
struct FFluidOneDJunctionIncidentBranchOneD
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	int32 BranchIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	bool IncidentAtBranchStart = false;
};

USTRUCT(BlueprintType)
struct FFluidOneDJunctionTopologyOneD
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	int32 SceneNodeKey = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	EFluidSceneEndpointKind SceneEndpointKind = EFluidSceneEndpointKind::Face;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	TArray<FFluidOneDJunctionIncidentBranchOneD> IncidentBranches;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	EFluidOneDJunctionPressurePolicy JunctionPressurePolicy = EFluidOneDJunctionPressurePolicy::AverageNeighborPressure;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	float FixedJunctionPressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneDNetwork")
	float ExternalVolumeFlowRate = 0.0f;
};

UCLASS()
class FLUIDPIPESPLUGIN_API UFluidData : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	TArray<FFluidNetworkNodeStateZeroD> NetworkNodesZeroD;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidZeroD")
	TArray<FFluidNetworkEdgeStateZeroD> NetworkEdgesZeroD;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidOneD")
	TArray<FFluidSegmentStateOneD> SegmentStatesOneD;
};
