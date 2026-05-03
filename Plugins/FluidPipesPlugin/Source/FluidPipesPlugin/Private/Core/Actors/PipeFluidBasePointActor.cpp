#include "Core/Actors/PipeFluidBasePointActor.h"

#include "Core/Actors/PipeFluidPipeActor.h"
#include "EngineUtils.h"

void APipeFluidBasePointActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<APipeFluidPipeActor> Iterator(World); Iterator; ++Iterator)
	{
		APipeFluidPipeActor* FluidPipeActor = *Iterator;
		if (!FluidPipeActor)
		{
			continue;
		}

		if (FluidPipeActor->PipeEndpointFirst == this || FluidPipeActor->PipeEndpointSecond == this)
		{
			FluidPipeActor->RerunConstructionScripts();
		}
	}
#endif
}

EFluidSceneEndpointKind APipeFluidBasePointActor::GetSceneEndpointKind() const
{
	return SceneEndpointKind;
}

void APipeFluidBasePointActor::FillOneDJunctionTopologyDefaults(FFluidOneDJunctionTopologyOneD& JunctionTopology) const
{
	JunctionTopology.SceneNodeKey = SceneNodeKey;
	JunctionTopology.SceneEndpointKind = SceneEndpointKind;
	JunctionTopology.IncidentBranches.Reset();
	JunctionTopology.JunctionPressurePolicy = OneDJunctionPressurePolicy;
	JunctionTopology.FixedJunctionPressure = OneDFixedJunctionPressure;
	JunctionTopology.ExternalVolumeFlowRate = OneDExternalVolumeFlowRate;
}
