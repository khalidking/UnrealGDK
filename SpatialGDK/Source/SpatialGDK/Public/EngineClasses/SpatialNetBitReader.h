// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreNet.h"

#include "Schema/UnrealObjectRef.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialNetBitReader, All, All);

class USpatialPackageMapClient;

class SPATIALGDK_API FSpatialNetBitReader : public FNetBitReader
{
public:
	FSpatialNetBitReader(USpatialPackageMapClient* InPackageMap, uint8* Source, int64 CountBits, TSet<FUnrealObjectRef>& InDynamicRefs, TSet<FUnrealObjectRef>& InUnresolvedRefs);

	using FArchive::operator<<; // For visibility of the overloads we don't override

	virtual FArchive& operator<<(UObject*& Value) override;

	virtual FArchive& operator<<(struct FWeakObjectPtr& Value) override;

	UObject* ReadObject(bool& bUnresolved);

protected:
	void DeserializeObjectRef(FUnrealObjectRef& ObjectRef);

	TSet<FUnrealObjectRef>& DynamicRefs;
	TSet<FUnrealObjectRef>& UnresolvedRefs;
};
