// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditor.h"

#include "Async/Async.h"
#include "SpatialGDKEditorCloudLauncher.h"
#include "SpatialGDKEditorSchemaGenerator.h"
#include "SpatialGDKEditorSnapshotGenerator.h"

#include "Editor.h"
#include "FileHelpers.h"

#include "AssetDataTagMap.h"
#include "AssetRegistryModule.h"
#include "GeneralProjectSettings.h"
#include "Internationalization/Regex.h"
#include "Misc/ScopedSlowTask.h"
#include "Settings/ProjectPackagingSettings.h"
#include "SpatialGDKEditorSettings.h"
#include "SpatialGDKServicesConstants.h"
#include "UObject/StrongObjectPtr.h"

using namespace SpatialGDKEditor;

DEFINE_LOG_CATEGORY(LogSpatialGDKEditor);

#define LOCTEXT_NAMESPACE "FSpatialGDKEditor"

bool FSpatialGDKEditor::GenerateSchema(bool bFullScan)
{
	if (bSchemaGeneratorRunning)
	{
		UE_LOG(LogSpatialGDKEditor, Warning, TEXT("Schema generation is already running"));
		return false;
	}

	// If this has been run from an open editor then prompt the user to save dirty packages and maps.
	if (!IsRunningCommandlet())
	{
		const bool bPromptUserToSave = true;
		const bool bSaveMapPackages = true;
		const bool bSaveContentPackages = true;
		const bool bFastSave = false;
		const bool bNotifyNoPackagesSaved = false;
		const bool bCanBeDeclined = true;
		if (!FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined))
		{
			// User hit cancel don't generate schema.
			return false;
		}
	}

	bSchemaGeneratorRunning = true;

	// 80/10/10 load assets / gen schema / garbage collection.
	FScopedSlowTask Progress(100.f, LOCTEXT("GeneratingSchema", "Generating Schema..."));
	Progress.MakeDialog(true);

#if ENGINE_MINOR_VERSION <= 22
	// Force spatial networking so schema layouts are correct
	UGeneralProjectSettings* GeneralProjectSettings = GetMutableDefault<UGeneralProjectSettings>();
	bool bCachedSpatialNetworking = GeneralProjectSettings->UsesSpatialNetworking();
	GeneralProjectSettings->SetUsesSpatialNetworking(true);
#endif

	RemoveEditorAssetLoadedCallback();

	if (Schema::IsAssetReadOnly(SpatialConstants::SCHEMA_DATABASE_FILE_PATH))
	{
		bSchemaGeneratorRunning = false;
		return false;
	}

	if (!Schema::LoadGeneratorStateFromSchemaDatabase(SpatialConstants::SCHEMA_DATABASE_FILE_PATH))
	{
		Schema::ResetSchemaGeneratorStateAndCleanupFolders();
	}

	TArray<TStrongObjectPtr<UObject>> LoadedAssets;
	if (bFullScan)
	{
		Progress.EnterProgressFrame(80.f);
		if (!LoadPotentialAssets(LoadedAssets))
		{
			bSchemaGeneratorRunning = false;
			LoadedAssets.Empty();
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
			return false;
		}
	}

	// If running from an open editor then compile all dirty blueprints
	TArray<UBlueprint*> ErroredBlueprints;
	if (!IsRunningCommandlet())
	{
		const bool bPromptForCompilation = false;
		UEditorEngine::ResolveDirtyBlueprints(bPromptForCompilation, ErroredBlueprints);
	}

	if (bFullScan)
	{
		// UNR-1610 - This copy is a workaround to enable schema_compiler usage until FPL is ready. Without this prepare_for_run checks crash local launch and cloud upload.
		FString GDKSchemaCopyDir = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("schema/unreal/gdk"));
		FString CoreSDKSchemaCopyDir = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSDirectory, TEXT("build/dependencies/schema/standard_library"));
		Schema::CopyWellKnownSchemaFiles(GDKSchemaCopyDir, CoreSDKSchemaCopyDir);
		Schema::RefreshSchemaFiles(GetDefault<USpatialGDKEditorSettings>()->GetGeneratedSchemaOutputFolder());
	}

	Progress.EnterProgressFrame(bFullScan ? 10.f : 100.f);
	bool bResult = Schema::SpatialGDKGenerateSchema();
	
	// We delay printing this error until after the schema spam to make it have a higher chance of being noticed.
	if (ErroredBlueprints.Num() > 0)
	{
		UE_LOG(LogSpatialGDKEditor, Error, TEXT("Errors compiling blueprints during schema generation! The following blueprints did not have schema generated for them:"));
		for (const auto& Blueprint : ErroredBlueprints)
		{
			UE_LOG(LogSpatialGDKEditor, Error, TEXT("%s"), *GetPathNameSafe(Blueprint));
		}
	}

	if (bFullScan)
	{
		Progress.EnterProgressFrame(10.f);
		LoadedAssets.Empty();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
	}

#if ENGINE_MINOR_VERSION <= 22
	GetMutableDefault<UGeneralProjectSettings>()->SetUsesSpatialNetworking(bCachedSpatialNetworking);
#endif
	bSchemaGeneratorRunning = false;

	if (bResult)
	{
		UE_LOG(LogSpatialGDKEditor, Display, TEXT("Schema Generation succeeded!"));
	}
	else
	{
		UE_LOG(LogSpatialGDKEditor, Error, TEXT("Schema Generation failed. View earlier log messages for errors."));
	}

	return bResult;
}

bool FSpatialGDKEditor::LoadPotentialAssets(TArray<TStrongObjectPtr<UObject>>& OutAssets)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	// Search project for all assets. This is required as the Commandlet will not have any paths cached.
	AssetRegistryModule.Get().SearchAllAssets(true);

	TArray<FAssetData> FoundAssets;
	AssetRegistryModule.Get().GetAllAssets(FoundAssets, true);

	const TArray<FDirectoryPath>& DirectoriesToNeverCook = GetDefault<UProjectPackagingSettings>()->DirectoriesToNeverCook;

	// Filter assets to game blueprint classes that are not loaded and not inside DirectoriesToNeverCook.
	FoundAssets = FoundAssets.FilterByPredicate([&DirectoriesToNeverCook](const FAssetData& Data)
	{
		if (Data.IsAssetLoaded())
		{
			return false;
		}
		if (!Data.TagsAndValues.Contains("GeneratedClass"))
		{
			return false;
		}
		const FString PackagePath = Data.PackagePath.ToString();

		for (const auto& Directory : DirectoriesToNeverCook)
		{
			if (PackagePath.StartsWith(Directory.Path))
			{
				return false;
			}
		}
		return true;
	});

	FScopedSlowTask Progress(static_cast<float>(FoundAssets.Num()), FText::FromString(FString::Printf(TEXT("Loading %d Assets before generating schema"), FoundAssets.Num())));

	for (const FAssetData& Data : FoundAssets)
	{
		if (Progress.ShouldCancel())
		{
			return false;
		}
		Progress.EnterProgressFrame(1, FText::FromString(FString::Printf(TEXT("Loading %s"), *Data.AssetName.ToString())));

		const FString* GeneratedClassPathPtr = nullptr;

		FAssetDataTagMapSharedView::FFindTagResult GeneratedClassFindTagResult = Data.TagsAndValues.FindTag("GeneratedClass");
		if (GeneratedClassFindTagResult.IsSet())
		{
			GeneratedClassPathPtr = &GeneratedClassFindTagResult.GetValue();
		}

		if (GeneratedClassPathPtr != nullptr)
		{ 
			const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(*GeneratedClassPathPtr);
			const FString ClassName = FPackageName::ObjectPathToObjectName(ClassObjectPath);
			FSoftObjectPath SoftPath = FSoftObjectPath(ClassObjectPath);
			OutAssets.Add(TStrongObjectPtr<UObject>(SoftPath.TryLoad()));
		}
	}

	return true;
}

void FSpatialGDKEditor::GenerateSnapshot(UWorld* World, FString SnapshotFilename, FSimpleDelegate SuccessCallback, FSimpleDelegate FailureCallback, FSpatialGDKEditorErrorHandler ErrorCallback)
{
	const bool bSuccess = SpatialGDKGenerateSnapshot(World, SnapshotFilename);

	if (bSuccess)
	{
		SuccessCallback.ExecuteIfBound();
	}
	else
	{
		FailureCallback.ExecuteIfBound();
	}
}

void FSpatialGDKEditor::LaunchCloudDeployment(FSimpleDelegate SuccessCallback, FSimpleDelegate FailureCallback)
{
#if ENGINE_MINOR_VERSION <= 22
	LaunchCloudResult = Async<bool>(EAsyncExecution::Thread, SpatialGDKCloudLaunch,
#else
	LaunchCloudResult = Async(EAsyncExecution::Thread, SpatialGDKCloudLaunch,
#endif
		[this, SuccessCallback, FailureCallback]
		{
			if (!LaunchCloudResult.IsReady() || LaunchCloudResult.Get() != true)
			{
				FailureCallback.ExecuteIfBound();
			}
			else
			{
				SuccessCallback.ExecuteIfBound();
			}
		});
}

void FSpatialGDKEditor::StopCloudDeployment(FSimpleDelegate SuccessCallback, FSimpleDelegate FailureCallback)
{
#if ENGINE_MINOR_VERSION <= 22
	StopCloudResult = Async<bool>(EAsyncExecution::Thread, SpatialGDKCloudStop,
#else
	StopCloudResult = Async(EAsyncExecution::Thread, SpatialGDKCloudStop,
#endif
		[this, SuccessCallback, FailureCallback]
		{
			if (!StopCloudResult.IsReady() || StopCloudResult.Get() != true)
			{
				FailureCallback.ExecuteIfBound();
			}
			else
			{
				SuccessCallback.ExecuteIfBound();
			}
		});
}

bool FSpatialGDKEditor::FullScanRequired()
{
	return !Schema::GeneratedSchemaFolderExists() || !Schema::GeneratedSchemaDatabaseExists();
}

void FSpatialGDKEditor::RemoveEditorAssetLoadedCallback()
{
	if (OnAssetLoadedHandle.IsValid())
	{
		return;
	}

	if (GEditor != nullptr)
	{
		UE_LOG(LogSpatialGDKEditor, Verbose, TEXT("Removing UEditorEngine::OnAssetLoaded."));
		FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(GEditor);
		UE_LOG(LogSpatialGDKEditor, Verbose, TEXT("Replacing UEditorEngine::OnAssetLoaded with spatial version that won't run during schema gen."));
		OnAssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddLambda([this](UObject* Asset) {
			OnAssetLoaded(Asset);
		});
	}

}

// This callback is copied from UEditorEngine::OnAssetLoaded so that we can turn it off during schema gen in editor.
void FSpatialGDKEditor::OnAssetLoaded(UObject* Asset)
{
	// do not init worlds when running schema gen.
	if (bSchemaGeneratorRunning)
	{
		return;
	}

	if (UWorld* World = Cast<UWorld>(Asset))
	{
		// Init inactive worlds here instead of UWorld::PostLoad because it is illegal to call UpdateWorldComponents while IsRoutingPostLoad
		if (!World->bIsWorldInitialized && World->WorldType == EWorldType::Inactive)
		{
			// Create the world without a physics scene because creating too many physics scenes causes deadlock issues in PhysX. The scene will be created when it is opened in the level editor.
			// Also, don't create an FXSystem because it consumes too much video memory. This is also created when the level editor opens this world.
			World->InitWorld(UWorld::InitializationValues()
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(true)
				.CreatePhysicsScene(false)
				.CreateFXSystem(false)
			);

			// Update components so the scene is populated
			World->UpdateWorldComponents(true, true);
		}
	}
}

#undef LOCTEXT_NAMESPACE
