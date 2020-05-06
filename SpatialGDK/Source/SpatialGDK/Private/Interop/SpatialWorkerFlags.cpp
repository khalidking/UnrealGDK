// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/SpatialWorkerFlags.h"

bool USpatialWorkerFlags::GetWorkerFlag(const FString& InFlagName, FString& OutFlagValue) const
{
	if (const FString* ValuePtr = WorkerFlags.Find(InFlagName))
	{
		OutFlagValue = *ValuePtr;
		return true;
	}

	return false;
}

void USpatialWorkerFlags::ApplyWorkerFlagUpdate(const Worker_FlagUpdateOp& Op)
{
	FString NewName = FString(UTF8_TO_TCHAR(Op.name));

	if (Op.value != nullptr)
	{
		FString NewValue = FString(UTF8_TO_TCHAR(Op.value));
		FString& ValueFlag = WorkerFlags.FindOrAdd(NewName);
		ValueFlag = NewValue;
		OnWorkerFlagsUpdated.Broadcast(NewName, NewValue);
	}
	else
	{
		WorkerFlags.Remove(NewName);
	}
}

void USpatialWorkerFlags::BindToOnWorkerFlagsUpdated(const FOnWorkerFlagsUpdatedBP& InDelegate)
{
	OnWorkerFlagsUpdated.Add(InDelegate);
}

void USpatialWorkerFlags::UnbindFromOnWorkerFlagsUpdated(const FOnWorkerFlagsUpdatedBP& InDelegate)
{
	OnWorkerFlagsUpdated.Remove(InDelegate);
}
