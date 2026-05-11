#include "Core/Simulation1D/FluidSegment1DSubsystem.h"

#include "Core/Actors/PipeFluidPipeActor.h"
#include "DrawDebugHelpers.h"
#include "FluidPipesDrawDebug.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Other/LazyFluidPipesDeveloperSettings.h"

void UFluidSegment1DSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResetSimulationState();
}

void UFluidSegment1DSubsystem::Deinitialize()
{
	ResetSimulationState();
	Super::Deinitialize();
}

void UFluidSegment1DSubsystem::Tick(float DeltaTime)
{
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	const bool bEnableFluidSegmentSimulationOneD = Settings->EnableFluidSegmentSimulationOneD;
	if (bEnableFluidSegmentSimulationOneD)
	{
		AccumulatedTime += DeltaTime;
		while (AccumulatedTime >= Settings->SimulationStepTimeOneD)
		{
			SimulateStep(Settings->SimulationStepTimeOneD);
			AccumulatedTime -= Settings->SimulationStepTimeOneD;
		}

		if (FluidPipesShouldEmitScreenDebugMessages())
		{
			UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("1D Tick: Segments={0}"), { FString::FromInt(SegmentStates.Num()) }), true, false, FLinearColor(0.0f, 1.0f, 1.0f), 0.0f);
		}

		const int32 OneDWorldDebugDetailLevel = FluidPipesGetOneDWorldDebugDetailLevel();
		if (OneDWorldDebugDetailLevel > 0)
		{
			DrawDebugOneDSegments(OneDWorldDebugDetailLevel);
		}
	}
}

TStatId UFluidSegment1DSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFluidSegment1DSubsystem, STATGROUP_Tickables);
}

void UFluidSegment1DSubsystem::ResetSimulationState()
{
	SegmentStates.Reset();
	SegmentPipeActors.Reset();
	AccumulatedTime = 0.0f;
}

const TArray<FFluidSegmentStateOneD>& UFluidSegment1DSubsystem::GetSegmentStates() const
{
	return SegmentStates;
}

void UFluidSegment1DSubsystem::ApplyImportedOneDSegments(const TArray<FFluidSegmentStateOneD>& Segments)
{
	TArray<APipeFluidPipeActor*> EmptyPipeActors;
	ApplyImportedOneDSegments(Segments, EmptyPipeActors);
}

void UFluidSegment1DSubsystem::ApplyImportedOneDSegments(const TArray<FFluidSegmentStateOneD>& Segments, const TArray<APipeFluidPipeActor*>& IncomingPipeActors)
{
	SegmentStates = Segments;
	SegmentPipeActors.Reset();
	if (IncomingPipeActors.Num() == SegmentStates.Num())
	{
		SegmentPipeActors.Reserve(SegmentStates.Num());
		for (APipeFluidPipeActor* IncomingPipeActor : IncomingPipeActors)
		{
			SegmentPipeActors.Add(IncomingPipeActor);
		}
	}

	for (FFluidSegmentStateOneD& SegmentState : SegmentStates)
	{
		UpdateDerivedCellValues(SegmentState);
	}
	AccumulatedTime = 0.0f;
}

void UFluidSegment1DSubsystem::SimulateStep(float SimulationStepTime)
{
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	for (FFluidSegmentStateOneD& SegmentState : SegmentStates)
	{
		if (SegmentState.CellStates.Num() < 2)
		{
			continue;
		}

		const float StableStepTime = ComputeStableStepTime(SegmentState);
		const float EffectiveStepTime = FMath::Min(SimulationStepTime, StableStepTime);
		if (FluidPipesShouldEmitScreenDebugMessages() && SimulationStepTime > StableStepTime + KINDA_SMALL_NUMBER)
		{
			UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("1D CFL Limited: Requested={0}, Stable={1}"), { FString::SanitizeFloat(SimulationStepTime), FString::SanitizeFloat(StableStepTime) }), true, true, FLinearColor::Yellow, 0.0f);
		}

		FFluidSegmentStateOneD NextSegmentState = SegmentState;
		SolveSegmentWaterHammerStep(SegmentState, EffectiveStepTime, NextSegmentState);
		ApplyBoundaryConditions(SegmentState, NextSegmentState);
		UpdateDerivedCellValues(NextSegmentState);
		if (!IsSegmentStateFinite(NextSegmentState))
		{
			continue;
		}
		SegmentState = MoveTemp(NextSegmentState);
	}
}

void UFluidSegment1DSubsystem::SolveSegmentWaterHammerStep(const FFluidSegmentStateOneD& CurrentSegmentState, float SimulationStepTime, FFluidSegmentStateOneD& NextSegmentState) const
{
	const float CrossSectionArea = GetCrossSectionArea(CurrentSegmentState);
	const float SafeDensity = FMath::Max(CurrentSegmentState.Density, KINDA_SMALL_NUMBER);
	const float SafeWaveSpeed = FMath::Max(CurrentSegmentState.WaveSpeed, 1.0f);
	const float SafeCellLength = FMath::Max(CurrentSegmentState.CellLength, 0.01f);
	const float FrictionResistance = CurrentSegmentState.FrictionFactor / (2.0f * FMath::Max(CurrentSegmentState.PipeDiameter, 0.001f) * FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER));
	const float PressureCoefficient = SafeDensity * SafeWaveSpeed * SafeWaveSpeed / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);

	NextSegmentState.CellStates = CurrentSegmentState.CellStates;
	for (int32 CellIndex = 1; CellIndex < CurrentSegmentState.CellStates.Num() - 1; ++CellIndex)
	{
		const float LeftFlow = CurrentSegmentState.CellStates[CellIndex - 1].FlowRate;
		const float CenterFlow = CurrentSegmentState.CellStates[CellIndex].FlowRate;
		const float RightFlow = CurrentSegmentState.CellStates[CellIndex + 1].FlowRate;
		const float LeftPressure = CurrentSegmentState.CellStates[CellIndex - 1].Pressure;
		const float CenterPressure = CurrentSegmentState.CellStates[CellIndex].Pressure;
		const float RightPressure = CurrentSegmentState.CellStates[CellIndex + 1].Pressure;
		const float FlowGradient = (RightFlow - LeftFlow) / (2.0f * SafeCellLength);
		const float PressureGradient = (RightPressure - LeftPressure) / (2.0f * SafeCellLength);
		const float FlowDerivative = -(CrossSectionArea / SafeDensity) * PressureGradient - FrictionResistance * CenterFlow * FMath::Abs(CenterFlow);
		const float PressureDerivative = -PressureCoefficient * FlowGradient;

		NextSegmentState.CellStates[CellIndex].FlowRate = CenterFlow + FlowDerivative * SimulationStepTime;
		NextSegmentState.CellStates[CellIndex].Pressure = CenterPressure + PressureDerivative * SimulationStepTime;
	}
}

void UFluidSegment1DSubsystem::ApplyBoundaryConditions(const FFluidSegmentStateOneD& CurrentSegmentState, FFluidSegmentStateOneD& NextSegmentState) const
{
	if (NextSegmentState.CellStates.Num() < 2)
	{
		return;
	}

	const int32 LastCellIndex = NextSegmentState.CellStates.Num() - 1;

	switch (CurrentSegmentState.LeftBoundaryConditionType)
	{
	case EFluidBoundaryConditionTypeOneD::FixedPressure:
		NextSegmentState.CellStates[0].Pressure = CurrentSegmentState.LeftBoundaryPressure;
		NextSegmentState.CellStates[0].FlowRate = NextSegmentState.CellStates[1].FlowRate;
		break;
	case EFluidBoundaryConditionTypeOneD::FixedFlow:
		NextSegmentState.CellStates[0].FlowRate = CurrentSegmentState.LeftBoundaryFlow;
		NextSegmentState.CellStates[0].Pressure = NextSegmentState.CellStates[1].Pressure;
		break;
	default:
		NextSegmentState.CellStates[0].FlowRate = -NextSegmentState.CellStates[1].FlowRate;
		NextSegmentState.CellStates[0].Pressure = NextSegmentState.CellStates[1].Pressure;
		break;
	}

	switch (CurrentSegmentState.RightBoundaryConditionType)
	{
	case EFluidBoundaryConditionTypeOneD::FixedPressure:
		NextSegmentState.CellStates[LastCellIndex].Pressure = CurrentSegmentState.RightBoundaryPressure;
		NextSegmentState.CellStates[LastCellIndex].FlowRate = NextSegmentState.CellStates[LastCellIndex - 1].FlowRate;
		break;
	case EFluidBoundaryConditionTypeOneD::FixedFlow:
		NextSegmentState.CellStates[LastCellIndex].FlowRate = CurrentSegmentState.RightBoundaryFlow;
		NextSegmentState.CellStates[LastCellIndex].Pressure = NextSegmentState.CellStates[LastCellIndex - 1].Pressure;
		break;
	default:
		NextSegmentState.CellStates[LastCellIndex].FlowRate = -NextSegmentState.CellStates[LastCellIndex - 1].FlowRate;
		NextSegmentState.CellStates[LastCellIndex].Pressure = NextSegmentState.CellStates[LastCellIndex - 1].Pressure;
		break;
	}
}

void UFluidSegment1DSubsystem::UpdateDerivedCellValues(FFluidSegmentStateOneD& SegmentState) const
{
	const float CrossSectionArea = GetCrossSectionArea(SegmentState);
	const float SafeDensity = FMath::Max(SegmentState.Density, KINDA_SMALL_NUMBER);
	const float ReferencePressure = SegmentState.CellStates.Num() > 0 ? SegmentState.CellStates[0].Pressure : 0.0f;
	const float PressureScale = SafeDensity * FMath::Max(SegmentState.WaveSpeed * SegmentState.WaveSpeed, 1.0f);
	for (FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		CellState.Velocity = CellState.FlowRate / FMath::Max(CrossSectionArea, KINDA_SMALL_NUMBER);
		CellState.FillRatio = FMath::Clamp(0.5f + (CellState.Pressure - ReferencePressure) / PressureScale, 0.0f, 1.0f);
	}
}

float UFluidSegment1DSubsystem::ComputeStableStepTime(const FFluidSegmentStateOneD& SegmentState) const
{
	const ULazyFluidPipesDeveloperSettings* Settings = GetDefault<ULazyFluidPipesDeveloperSettings>();
	const float SafeWaveSpeed = FMath::Max(SegmentState.WaveSpeed, 1.0f);
	const float SafeCellLength = FMath::Max(SegmentState.CellLength, 0.01f);
	return Settings->OneDSolverCflFactor * SafeCellLength / SafeWaveSpeed;
}

float UFluidSegment1DSubsystem::GetCrossSectionArea(const FFluidSegmentStateOneD& SegmentState) const
{
	const float SafePipeDiameter = FMath::Max(SegmentState.PipeDiameter, 0.001f);
	return PI * 0.25f * SafePipeDiameter * SafePipeDiameter;
}

bool UFluidSegment1DSubsystem::IsSegmentStateFinite(const FFluidSegmentStateOneD& SegmentState) const
{
	for (const FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
	{
		if (!FMath::IsFinite(CellState.Pressure) || !FMath::IsFinite(CellState.FlowRate) || !FMath::IsFinite(CellState.Velocity) || !FMath::IsFinite(CellState.FillRatio))
		{
			return false;
		}
	}

	return true;
}

static FString FluidOneDBoundaryStateDisplayString(EFluidBoundaryConditionTypeOneD BoundaryType, float BoundaryPressure, float BoundaryFlow)
{
	switch (BoundaryType)
	{
	case EFluidBoundaryConditionTypeOneD::FixedPressure:
		return FString::Format(TEXT("FixedPressure p={0}"), { FString::SanitizeFloat(BoundaryPressure) });
	case EFluidBoundaryConditionTypeOneD::FixedFlow:
		return FString::Format(TEXT("FixedFlow Q={0}"), { FString::SanitizeFloat(BoundaryFlow) });
	default:
		return FString(TEXT("Reflective"));
	}
}

void UFluidSegment1DSubsystem::DrawDebugOneDSegments(int32 DebugLevel) const
{
	UWorld* World = GetWorld();
	if (!World || DebugLevel <= 0)
	{
		return;
	}

	const bool DrawSegmentAndEndpointText = DebugLevel >= 1;
	const bool DrawPerCellText = DebugLevel >= 2;
	const bool DrawEveryCellText = DebugLevel >= 3;

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentStates.Num(); ++SegmentIndex)
	{
		const FFluidSegmentStateOneD& SegmentState = SegmentStates[SegmentIndex];
		if (SegmentState.CellStates.Num() < 1)
		{
			continue;
		}

		const APipeFluidPipeActor* PipeActor = SegmentPipeActors.IsValidIndex(SegmentIndex) ? SegmentPipeActors[SegmentIndex].Get() : nullptr;
		if (!PipeActor)
		{
			continue;
		}

		const FVector AxisDirectionWorld = PipeActor->GetActorForwardVector().GetSafeNormal();
		const FVector CenterWorld = PipeActor->GetActorLocation();
		const float HalfLength = SegmentState.SegmentLength * 0.5f;
		const FVector AxisStartWorld = CenterWorld - AxisDirectionWorld * HalfLength;
		const FVector AxisEndWorld = CenterWorld + AxisDirectionWorld * HalfLength;
		const int32 CellCount = SegmentState.CellStates.Num();

		float MinPressure = SegmentState.CellStates[0].Pressure;
		float MaxPressure = MinPressure;
		for (const FFluidSegmentCellStateOneD& CellState : SegmentState.CellStates)
		{
			MinPressure = FMath::Min(MinPressure, CellState.Pressure);
			MaxPressure = FMath::Max(MaxPressure, CellState.Pressure);
		}
		const float PressureRange = FMath::Max(MaxPressure - MinPressure, KINDA_SMALL_NUMBER);

		FVector LateralWorld = FVector::CrossProduct(AxisDirectionWorld, FVector::UpVector);
		if (LateralWorld.SizeSquared() < 1.0e-8f)
		{
			LateralWorld = FVector::CrossProduct(AxisDirectionWorld, FVector::ForwardVector);
		}
		LateralWorld = LateralWorld.GetSafeNormal();

		DrawDebugLine(World, AxisStartWorld, AxisEndWorld, FColor::White, false, 0.0f, 0, 1.5f);

		const float StableStepTime = ComputeStableStepTime(SegmentState);
		const float CrossSectionArea = GetCrossSectionArea(SegmentState);

		if (DrawSegmentAndEndpointText)
		{
			const FString SegmentSummaryLine = FString::Format(
				TEXT("{0} | cells={1} L={2} dx={3} | c={4} D={5} rho={6} f={7} | A={8} dtStable={9} | Pmin={10} Pmax={11}"),
				{
					SegmentState.SegmentName.ToString(),
					FString::FromInt(CellCount),
					FString::SanitizeFloat(SegmentState.SegmentLength),
					FString::SanitizeFloat(SegmentState.CellLength),
					FString::SanitizeFloat(SegmentState.WaveSpeed),
					FString::SanitizeFloat(SegmentState.PipeDiameter),
					FString::SanitizeFloat(SegmentState.Density),
					FString::SanitizeFloat(SegmentState.FrictionFactor),
					FString::SanitizeFloat(CrossSectionArea),
					FString::SanitizeFloat(StableStepTime),
					FString::SanitizeFloat(MinPressure),
					FString::SanitizeFloat(MaxPressure)
				});
			DrawDebugString(World, CenterWorld + FVector(0.0f, 0.0f, 48.0f), SegmentSummaryLine, nullptr, FColor::Yellow, 0.0f, true, 1.0f);

			const FString LeftEndpointLine = FString::Format(
				TEXT("Start | nodeKey={0} | {1} | cell0 P={2} Q={3}"),
				{
					FString::FromInt(SegmentState.LeftSceneNodeKey),
					FluidOneDBoundaryStateDisplayString(SegmentState.LeftBoundaryConditionType, SegmentState.LeftBoundaryPressure, SegmentState.LeftBoundaryFlow),
					FString::SanitizeFloat(SegmentState.CellStates[0].Pressure),
					FString::SanitizeFloat(SegmentState.CellStates[0].FlowRate)
				});
			DrawDebugString(World, AxisStartWorld + FVector(0.0f, 0.0f, 28.0f) + LateralWorld * 18.0f, LeftEndpointLine, nullptr, FColor::Cyan, 0.0f, true, 1.0f);

			const FString RightEndpointLine = FString::Format(
				TEXT("End | nodeKey={0} | {1} | cellLast P={2} Q={3}"),
				{
					FString::FromInt(SegmentState.RightSceneNodeKey),
					FluidOneDBoundaryStateDisplayString(SegmentState.RightBoundaryConditionType, SegmentState.RightBoundaryPressure, SegmentState.RightBoundaryFlow),
					FString::SanitizeFloat(SegmentState.CellStates[CellCount - 1].Pressure),
					FString::SanitizeFloat(SegmentState.CellStates[CellCount - 1].FlowRate)
				});
			DrawDebugString(World, AxisEndWorld + FVector(0.0f, 0.0f, 28.0f) - LateralWorld * 18.0f, RightEndpointLine, nullptr, FColor::Cyan, 0.0f, true, 1.0f);
		}

		const int32 LabelStride = DrawEveryCellText ? 1 : (CellCount <= 10 ? 1 : FMath::Max(1, CellCount / 8));

		for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			const float SampleParameter = (CellCount <= 1) ? 0.5f : (static_cast<float>(CellIndex) + 0.5f) / static_cast<float>(CellCount);
			const FVector CellPositionWorld = AxisStartWorld + AxisDirectionWorld * (SampleParameter * SegmentState.SegmentLength);
			const float NormalizedPressure = FMath::Clamp((SegmentState.CellStates[CellIndex].Pressure - MinPressure) / PressureRange, 0.0f, 1.0f);
			const FColor PressureColor = FLinearColor::LerpUsingHSV(FLinearColor::Blue, FLinearColor::Red, NormalizedPressure).ToFColor(true);

			DrawDebugSphere(World, CellPositionWorld, 8.0f, 8, PressureColor, false, 0.0f, 0, 1.0f);

			const float FlowRate = SegmentState.CellStates[CellIndex].FlowRate;
			if (FMath::Abs(FlowRate) > KINDA_SMALL_NUMBER)
			{
				const FVector FlowDirectionWorld = AxisDirectionWorld * FMath::Sign(FlowRate);
				const float ArrowHalfLength = FMath::Clamp(FMath::Abs(FlowRate) * 0.001f, 5.0f, 80.0f);
				const FVector ArrowStartWorld = CellPositionWorld - FlowDirectionWorld * ArrowHalfLength * 0.5f;
				const FVector ArrowEndWorld = CellPositionWorld + FlowDirectionWorld * ArrowHalfLength * 0.5f;
				DrawDebugDirectionalArrow(World, ArrowStartWorld, ArrowEndWorld, ArrowHalfLength * 0.35f, FColor(0, 255, 255), false, 0.0f, 0, 2.0f);
			}

			if (DrawPerCellText && (CellIndex % LabelStride == 0))
			{
				const FFluidSegmentCellStateOneD& CellState = SegmentState.CellStates[CellIndex];
				const FString CellLabel = FString::Format(
					TEXT("Cell {0} | P={1} Q={2} U={3} fill={4}"),
					{
						FString::FromInt(CellIndex),
						FString::SanitizeFloat(CellState.Pressure),
						FString::SanitizeFloat(CellState.FlowRate),
						FString::SanitizeFloat(CellState.Velocity),
						FString::SanitizeFloat(CellState.FillRatio)
					});
				const float StaggerScale = 14.0f;
				const FVector StaggerWorld = LateralWorld * StaggerScale * static_cast<float>((CellIndex % 5) - 2);
				DrawDebugString(World, CellPositionWorld + FVector(0.0f, 0.0f, 18.0f) + StaggerWorld, CellLabel, nullptr, FColor::White, 0.0f, true, 1.0f);
			}
		}
	}
}
