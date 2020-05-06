// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Utils/RPCContainer.h"

#include "Schema/UnrealObjectRef.h"
#include "SpatialGDKSettings.h"

DEFINE_LOG_CATEGORY(LogRPCContainer);

using namespace SpatialGDK;

namespace
{
	FString ERPCResultToString(ERPCResult Result)
	{
		switch (Result)
		{
		case ERPCResult::Success:
			return TEXT("");

		case ERPCResult::UnresolvedTargetObject:
			return TEXT("Unresolved Target Object");

		case ERPCResult::MissingFunctionInfo:
			return TEXT("Missing UFunction info");

		case ERPCResult::UnresolvedParameters:
			return TEXT("Unresolved Parameters");

		case ERPCResult::ActorPendingKill:
			return TEXT("Actor Pending Kill");

		case ERPCResult::TimedOut:
			return TEXT("Timed Out");

		case ERPCResult::NoActorChannel:
			return TEXT("No Actor Channel");

		case ERPCResult::SpatialActorChannelNotListening:
			return TEXT("Spatial Actor Channel Not Listening");

		case ERPCResult::NoNetConnection:
			return TEXT("No Net Connection");

		case ERPCResult::NoAuthority:
			return TEXT("No Authority");

		case ERPCResult::InvalidRPCType:
			return TEXT("Invalid RPC Type");

		case ERPCResult::NoOwningController:
			return TEXT("No Owning Controller");

		case ERPCResult::NoControllerChannel:
			return TEXT("No Controller Channel");

		case ERPCResult::ControllerChannelNotListening:
			return TEXT("Controller Channel Not Listening");

		default:
			return TEXT("Unknown");
		}
	}

	void LogRPCError(const FRPCErrorInfo& ErrorInfo, ERPCQueueType QueueType, const FPendingRPCParams& Params)
	{
		const FTimespan TimeDiff = FDateTime::Now() - Params.Timestamp;

		// The format is expected to be:
		// Function <objectName>::<functionName> sending/execution dropped/queued for <duration>. Reason: <reason>
		FString OutputLog = FString::Printf(TEXT("Function %s::%s %s %s for %s. Reason: %s"),
			ErrorInfo.TargetObject.IsValid() ? *ErrorInfo.TargetObject->GetName() : TEXT("UNKNOWN"),
			ErrorInfo.Function.IsValid() ? *ErrorInfo.Function->GetName() : TEXT("UNKNOWN"),
			QueueType == ERPCQueueType::Send ? TEXT("sending") : QueueType == ERPCQueueType::Receive ? TEXT("execution") : TEXT("UNKNOWN"),
			ErrorInfo.bShouldDrop ? TEXT("dropped") : TEXT("queued"),
			*TimeDiff.ToString(),
			*ERPCResultToString(ErrorInfo.ErrorCode));

		const USpatialGDKSettings* SpatialGDKSettings = GetDefault<USpatialGDKSettings>();
		check(SpatialGDKSettings != nullptr);

		if (TimeDiff.GetTotalSeconds() > SpatialGDKSettings->GetSecondsBeforeWarning(ErrorInfo.ErrorCode))
		{
			UE_LOG(LogRPCContainer, Warning, TEXT("%s"), *OutputLog);
		}
		else
		{
			UE_LOG(LogRPCContainer, Verbose, TEXT("%s"), *OutputLog);
		}
	}
}

FPendingRPCParams::FPendingRPCParams(const FUnrealObjectRef& InTargetObjectRef, ERPCType InType, RPCPayload&& InPayload)
	: ObjectRef(InTargetObjectRef)
	, Payload(MoveTemp(InPayload))
	, Timestamp(FDateTime::Now())
	, Type(InType)
{
}

void FRPCContainer::ProcessOrQueueRPC(const FUnrealObjectRef& TargetObjectRef, ERPCType Type, RPCPayload&& Payload)
{
	FPendingRPCParams Params {TargetObjectRef, Type, MoveTemp(Payload)};

	if (!ObjectHasRPCsQueuedOfType(Params.ObjectRef.Entity, Params.Type))
	{
		if (ApplyFunction(Params))
		{
			return;
		}
	}

	FArrayOfParams& ArrayOfParams = QueuedRPCs.FindOrAdd(Params.Type).FindOrAdd(Params.ObjectRef.Entity);
	ArrayOfParams.Push(MoveTemp(Params));
}

void FRPCContainer::ProcessRPCs(FArrayOfParams& RPCList)
{
	// TODO: UNR-1651 Find a way to drop queued RPCs
	int NumProcessedParams = 0;
	for (auto& Params : RPCList)
	{
		if (ApplyFunction(Params))
		{
			NumProcessedParams++;
		}
		else
		{
			break;
		}
	}
	RPCList.RemoveAt(0, NumProcessedParams);
}

void FRPCContainer::ProcessRPCs()
{
	if (bAlreadyProcessingRPCs)
	{
		UE_LOG(LogRPCContainer, Log, TEXT("Calling ProcessRPCs recursively, ignoring the call"));
		return;
	}

	bAlreadyProcessingRPCs = true;

	for (auto& RPCs : QueuedRPCs)
	{
		FRPCMap& MapOfQueues = RPCs.Value;
		for (auto It = MapOfQueues.CreateIterator(); It; ++It)
		{
			FArrayOfParams& RPCList = It.Value();
			ProcessRPCs(RPCList);
			if (RPCList.Num() == 0)
			{
				It.RemoveCurrent();
			}
		}
	}

	bAlreadyProcessingRPCs = false;
}

void FRPCContainer::DropForEntity(const Worker_EntityId& EntityId)
{
	for (auto& RpcMap : QueuedRPCs)
	{
		RpcMap.Value.Remove(EntityId);
	}
}

bool FRPCContainer::ObjectHasRPCsQueuedOfType(const Worker_EntityId& EntityId, ERPCType Type) const
{
	if(const FRPCMap* MapOfQueues = QueuedRPCs.Find(Type))
	{
		if(const FArrayOfParams* RPCList = MapOfQueues->Find(EntityId))
		{
			return (RPCList->Num() > 0);
		}
	}

	return false;
}
 
FRPCContainer::FRPCContainer(ERPCQueueType InQueueType)
	: QueueType(InQueueType)
{
}

void FRPCContainer::BindProcessingFunction(const FProcessRPCDelegate& Function)
{
	ProcessingFunction = Function;
}

bool FRPCContainer::ApplyFunction(FPendingRPCParams& Params)
{
	ensure(ProcessingFunction.IsBound());
	FRPCErrorInfo ErrorInfo = ProcessingFunction.Execute(Params);

	if (ErrorInfo.Success())
	{
		return true;
	}
	else
	{
#if !UE_BUILD_SHIPPING
		LogRPCError(ErrorInfo, QueueType, Params);
#endif
		return ErrorInfo.bShouldDrop;
	}
}
