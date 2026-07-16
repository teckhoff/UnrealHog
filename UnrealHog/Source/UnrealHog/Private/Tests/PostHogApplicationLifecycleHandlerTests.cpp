#include "Lifecycle/PostHogApplicationLifecycleHandler.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Events/PostHogEventProperties.h"
#include "Misc/AutomationTest.h"
#include "PostHogDeveloperSettings.h"
#include "Serialization/JsonSerializer.h"
#include "Storage/PostHogStorageProvider.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace
{
	class FPostHogLifecycleHandlerTestStorage final : public IPostHogStorageProvider
	{
	public:
		virtual bool SaveEvent(const FString&, const FString&) override { return false; }
		virtual bool LoadEvent(const FString&, FString&) override { return false; }
		virtual bool DeleteEvent(const FString&) override { return false; }
		virtual bool ClearEvents() override { return false; }
		virtual TArray<FString> GetEventIds() override { return TArray<FString>(); }
		virtual int32 GetEventCount() override { return 0; }

		virtual bool SaveState(const FString& StateKey, const FString& StateJson) override
		{
			States.Add(StateKey, StateJson);
			SaveCounts.FindOrAdd(StateKey)++;
			return true;
		}

		using IPostHogStorageProvider::SaveState;

		virtual bool LoadState(const FString& StateKey, FString& StateJson) override
		{
			LoadCounts.FindOrAdd(StateKey)++;

			if (const FString* FoundState = States.Find(StateKey))
			{
				StateJson = *FoundState;
				return true;
			}

			StateJson.Empty();
			return false;
		}

		virtual bool DeleteState(const FString& StateKey) override
		{
			return States.Remove(StateKey) > 0;
		}

		bool HasState(const FString& StateKey) const
		{
			return States.Contains(StateKey);
		}

		int32 GetSaveCount(const FString& StateKey) const
		{
			if (const int32* Count = SaveCounts.Find(StateKey))
			{
				return *Count;
			}

			return 0;
		}

		int32 GetLoadCount(const FString& StateKey) const
		{
			if (const int32* Count = LoadCounts.Find(StateKey))
			{
				return *Count;
			}

			return 0;
		}

	private:
		TMap<FString, FString> States;
		TMap<FString, int32> SaveCounts;
		TMap<FString, int32> LoadCounts;
	};

	struct FPostHogLifecycleCapturedEvent
	{
		FString Name;
		TMap<FString, FString> Strings;
		TMap<FString, bool> Bools;
	};

	UPostHogDeveloperSettings* MakeLifecycleHandlerSettings(bool bCaptureLifecycleEvents)
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), bCaptureLifecycleEvents);
		return Settings;
	}

	FPostHogApplicationMetadata MakeLifecycleHandlerMetadata(const FString& Version, const FString& Build)
	{
		FPostHogApplicationMetadata Metadata;
		Metadata.Version = Version;
		Metadata.Build = Build;
		return Metadata;
	}

	FPostHogApplicationLifecycleHandler::FMetadataProvider MakeLifecycleHandlerMetadataProvider(FPostHogApplicationMetadata& Metadata)
	{
		return [&Metadata]()
		{
			return Metadata;
		};
	}

	void CaptureLifecycleHandlerEvent(TArray<FPostHogLifecycleCapturedEvent>& CapturedEvents,
		const FString& EventName,
		UPostHogEventProperties* Properties)
	{
		FPostHogLifecycleCapturedEvent CapturedEvent;
		CapturedEvent.Name = EventName;

		if (Properties)
		{
			for (const FPostHogEventProperty& Property : Properties->GetProperties())
			{
				switch (Property.Type)
				{
				case EPostHogPropertyType::String:
					CapturedEvent.Strings.Add(Property.Key, Property.StringValue);
					break;
				case EPostHogPropertyType::Boolean:
					CapturedEvent.Bools.Add(Property.Key, Property.bBoolValue);
					break;
				default:
					break;
				}
			}
		}

		CapturedEvents.Add(MoveTemp(CapturedEvent));
	}

	void SeedLifecycleHandlerState(FPostHogLifecycleHandlerTestStorage& Storage, const FString& Version, const FString& Build)
	{
		const TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
		StateObject->SetStringField(TEXT("lastSeenVersion"), Version);
		StateObject->SetStringField(TEXT("lastSeenBuild"), Build);
		Storage.SaveState(TEXT("lifecycle"), StateObject);
	}

	bool TryReadLifecycleHandlerState(FPostHogLifecycleHandlerTestStorage& Storage, FString& OutVersion, FString& OutBuild)
	{
		FString StateJson;
		if (!Storage.LoadState(TEXT("lifecycle"), StateJson))
		{
			return false;
		}

		TSharedPtr<FJsonObject> StateObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);
		if (!FJsonSerializer::Deserialize(Reader, StateObject) || !StateObject.IsValid())
		{
			return false;
		}

		return StateObject->TryGetStringField(TEXT("lastSeenVersion"), OutVersion)
			&& StateObject->TryGetStringField(TEXT("lastSeenBuild"), OutBuild);
	}

	void TestLifecycleHandlerVersionBuildProperties(FAutomationTestBase& Test,
		const FPostHogLifecycleCapturedEvent& Event,
		const FString& ExpectedVersion,
		const FString& ExpectedBuild)
	{
		Test.TestEqual(TEXT("version property"), Event.Strings.FindRef(TEXT("version")), ExpectedVersion);
		Test.TestEqual(TEXT("build property"), Event.Strings.FindRef(TEXT("build")), ExpectedBuild);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogApplicationLifecycleFirstLaunchTest, "UnrealHog.Lifecycle.ApplicationLifecycleHandler.FirstPermittedLaunchEmitsInstalledThenOpenedAndPersists", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogApplicationLifecycleFirstLaunchTest::RunTest(const FString& Parameters)
{
	FPostHogLifecycleHandlerTestStorage Storage;
	FPostHogApplicationMetadata Metadata = MakeLifecycleHandlerMetadata(TEXT("1.0.0"), TEXT("build-1"));
	TArray<FPostHogLifecycleCapturedEvent> CapturedEvents;

	FPostHogApplicationLifecycleHandler Handler(
		[&CapturedEvents](const FString& EventName, UPostHogEventProperties* Properties)
		{
			CaptureLifecycleHandlerEvent(CapturedEvents, EventName, Properties);
		},
		nullptr,
		nullptr,
		MakeLifecycleHandlerMetadataProvider(Metadata));

	Handler.Start(*MakeLifecycleHandlerSettings(/*bCaptureLifecycleEvents=*/true), Storage);

	TestEqual(TEXT("Two initial lifecycle events"), CapturedEvents.Num(), 2);
	if (CapturedEvents.Num() == 2)
	{
		TestEqual(TEXT("Installed first"), CapturedEvents[0].Name, TEXT("Application Installed"));
		TestLifecycleHandlerVersionBuildProperties(*this, CapturedEvents[0], TEXT("1.0.0"), TEXT("build-1"));

		TestEqual(TEXT("Opened second"), CapturedEvents[1].Name, TEXT("Application Opened"));
		TestLifecycleHandlerVersionBuildProperties(*this, CapturedEvents[1], TEXT("1.0.0"), TEXT("build-1"));
		TestFalse(TEXT("Initial open is not from background"), CapturedEvents[1].Bools.FindRef(TEXT("from_background")));
	}

	FString SavedVersion;
	FString SavedBuild;
	TestTrue(TEXT("Lifecycle state persisted"), TryReadLifecycleHandlerState(Storage, SavedVersion, SavedBuild));
	TestEqual(TEXT("Saved version"), SavedVersion, TEXT("1.0.0"));
	TestEqual(TEXT("Saved build"), SavedBuild, TEXT("build-1"));
	TestEqual(TEXT("One lifecycle state save"), Storage.GetSaveCount(TEXT("lifecycle")), 1);

	Handler.Stop();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogApplicationLifecycleUpdatedTest, "UnrealHog.Lifecycle.ApplicationLifecycleHandler.VersionChangeEmitsUpdatedWithPreviousValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogApplicationLifecycleUpdatedTest::RunTest(const FString& Parameters)
{
	FPostHogLifecycleHandlerTestStorage Storage;
	SeedLifecycleHandlerState(Storage, TEXT("1.0.0"), TEXT("build-1"));
	const int32 SavesAfterSeeding = Storage.GetSaveCount(TEXT("lifecycle"));

	FPostHogApplicationMetadata Metadata = MakeLifecycleHandlerMetadata(TEXT("2.0.0"), TEXT("build-2"));
	TArray<FPostHogLifecycleCapturedEvent> CapturedEvents;

	FPostHogApplicationLifecycleHandler Handler(
		[&CapturedEvents](const FString& EventName, UPostHogEventProperties* Properties)
		{
			CaptureLifecycleHandlerEvent(CapturedEvents, EventName, Properties);
		},
		nullptr,
		nullptr,
		MakeLifecycleHandlerMetadataProvider(Metadata));

	Handler.Start(*MakeLifecycleHandlerSettings(/*bCaptureLifecycleEvents=*/true), Storage);

	TestEqual(TEXT("Two update lifecycle events"), CapturedEvents.Num(), 2);
	if (CapturedEvents.Num() == 2)
	{
		TestEqual(TEXT("Updated first"), CapturedEvents[0].Name, TEXT("Application Updated"));
		TestLifecycleHandlerVersionBuildProperties(*this, CapturedEvents[0], TEXT("2.0.0"), TEXT("build-2"));
		TestEqual(TEXT("Previous version"), CapturedEvents[0].Strings.FindRef(TEXT("previous_version")), TEXT("1.0.0"));
		TestEqual(TEXT("Previous build"), CapturedEvents[0].Strings.FindRef(TEXT("previous_build")), TEXT("build-1"));

		TestEqual(TEXT("Opened second"), CapturedEvents[1].Name, TEXT("Application Opened"));
		TestFalse(TEXT("Initial updated open is not from background"), CapturedEvents[1].Bools.FindRef(TEXT("from_background")));
	}

	FString SavedVersion;
	FString SavedBuild;
	TestTrue(TEXT("Lifecycle state persisted"), TryReadLifecycleHandlerState(Storage, SavedVersion, SavedBuild));
	TestEqual(TEXT("Saved updated version"), SavedVersion, TEXT("2.0.0"));
	TestEqual(TEXT("Saved updated build"), SavedBuild, TEXT("build-2"));
	TestEqual(TEXT("One lifecycle state save after seeding"), Storage.GetSaveCount(TEXT("lifecycle")), SavesAfterSeeding + 1);

	Handler.Stop();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogApplicationLifecycleBuildUpdatedTest, "UnrealHog.Lifecycle.ApplicationLifecycleHandler.BuildChangeEmitsUpdatedWithPreviousValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogApplicationLifecycleBuildUpdatedTest::RunTest(const FString& Parameters)
{
	FPostHogLifecycleHandlerTestStorage Storage;
	SeedLifecycleHandlerState(Storage, TEXT("1.0.0"), TEXT("build-1"));

	FPostHogApplicationMetadata Metadata = MakeLifecycleHandlerMetadata(TEXT("1.0.0"), TEXT("build-2"));
	TArray<FPostHogLifecycleCapturedEvent> CapturedEvents;

	FPostHogApplicationLifecycleHandler Handler(
		[&CapturedEvents](const FString& EventName, UPostHogEventProperties* Properties)
		{
			CaptureLifecycleHandlerEvent(CapturedEvents, EventName, Properties);
		},
		nullptr,
		nullptr,
		MakeLifecycleHandlerMetadataProvider(Metadata));

	Handler.Start(*MakeLifecycleHandlerSettings(/*bCaptureLifecycleEvents=*/true), Storage);

	TestEqual(TEXT("Build change emits Updated then Opened"), CapturedEvents.Num(), 2);
	if (CapturedEvents.Num() == 2)
	{
		TestEqual(TEXT("Updated first"), CapturedEvents[0].Name, TEXT("Application Updated"));
		TestLifecycleHandlerVersionBuildProperties(*this, CapturedEvents[0], TEXT("1.0.0"), TEXT("build-2"));
		TestEqual(TEXT("Previous version"), CapturedEvents[0].Strings.FindRef(TEXT("previous_version")), TEXT("1.0.0"));
		TestEqual(TEXT("Previous build"), CapturedEvents[0].Strings.FindRef(TEXT("previous_build")), TEXT("build-1"));

		TestEqual(TEXT("Opened second"), CapturedEvents[1].Name, TEXT("Application Opened"));
	}

	FString SavedVersion;
	FString SavedBuild;
	TestTrue(TEXT("Lifecycle state persisted"), TryReadLifecycleHandlerState(Storage, SavedVersion, SavedBuild));
	TestEqual(TEXT("Version remains unchanged"), SavedVersion, TEXT("1.0.0"));
	TestEqual(TEXT("Updated build persisted"), SavedBuild, TEXT("build-2"));

	Handler.Stop();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogApplicationLifecycleDuplicateTransitionsTest, "UnrealHog.Lifecycle.ApplicationLifecycleHandler.SuppressesDuplicateBackgroundForegroundTransitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogApplicationLifecycleDuplicateTransitionsTest::RunTest(const FString& Parameters)
{
	FPostHogLifecycleHandlerTestStorage Storage;
	FPostHogApplicationMetadata Metadata = MakeLifecycleHandlerMetadata(TEXT("1.0.0"), TEXT("build-1"));
	TArray<FPostHogLifecycleCapturedEvent> CapturedEvents;
	int32 ForegroundCount = 0;
	int32 BackgroundCount = 0;

	FPostHogApplicationLifecycleHandler Handler(
		[&CapturedEvents](const FString& EventName, UPostHogEventProperties* Properties)
		{
			CaptureLifecycleHandlerEvent(CapturedEvents, EventName, Properties);
		},
		[&ForegroundCount]()
		{
			++ForegroundCount;
		},
		[&BackgroundCount]()
		{
			++BackgroundCount;
		},
		MakeLifecycleHandlerMetadataProvider(Metadata));

	Handler.Start(*MakeLifecycleHandlerSettings(/*bCaptureLifecycleEvents=*/true), Storage);
	CapturedEvents.Reset();

	Handler.NotifyApplicationForegrounded();
	Handler.NotifyApplicationBackgrounded();
	Handler.NotifyApplicationBackgrounded();
	Handler.NotifyApplicationForegrounded();
	Handler.NotifyApplicationForegrounded();

	TestEqual(TEXT("One background callback"), BackgroundCount, 1);
	TestEqual(TEXT("One foreground callback"), ForegroundCount, 1);
	TestEqual(TEXT("Only background and reopen events captured"), CapturedEvents.Num(), 2);
	if (CapturedEvents.Num() == 2)
	{
		TestEqual(TEXT("Backgrounded captured once"), CapturedEvents[0].Name, TEXT("Application Backgrounded"));
		TestLifecycleHandlerVersionBuildProperties(*this, CapturedEvents[0], TEXT("1.0.0"), TEXT("build-1"));

		TestEqual(TEXT("Reopen captured once"), CapturedEvents[1].Name, TEXT("Application Opened"));
		TestTrue(TEXT("Reopen is from background"), CapturedEvents[1].Bools.FindRef(TEXT("from_background")));
	}

	Handler.Stop();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogApplicationLifecycleTerminationTest, "UnrealHog.Lifecycle.ApplicationLifecycleHandler.TerminationMapsToSingleBackground", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogApplicationLifecycleTerminationTest::RunTest(const FString& Parameters)
{
	FPostHogLifecycleHandlerTestStorage Storage;
	FPostHogApplicationMetadata Metadata = MakeLifecycleHandlerMetadata(TEXT("1.0.0"), TEXT("build-1"));
	TArray<FPostHogLifecycleCapturedEvent> CapturedEvents;
	int32 BackgroundCount = 0;

	FPostHogApplicationLifecycleHandler Handler(
		[&CapturedEvents](const FString& EventName, UPostHogEventProperties* Properties)
		{
			CaptureLifecycleHandlerEvent(CapturedEvents, EventName, Properties);
		},
		nullptr,
		[&BackgroundCount]()
		{
			++BackgroundCount;
		},
		MakeLifecycleHandlerMetadataProvider(Metadata));

	Handler.Start(*MakeLifecycleHandlerSettings(/*bCaptureLifecycleEvents=*/true), Storage);
	CapturedEvents.Reset();

	Handler.NotifyApplicationTerminating();
	Handler.NotifyApplicationTerminating();
	Handler.NotifyApplicationBackgrounded();

	TestEqual(TEXT("Termination/background callback once"), BackgroundCount, 1);
	TestEqual(TEXT("Termination captures one background event"), CapturedEvents.Num(), 1);
	if (CapturedEvents.Num() == 1)
	{
		TestEqual(TEXT("Termination event is backgrounded"), CapturedEvents[0].Name, TEXT("Application Backgrounded"));
	}

	Handler.Stop();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogApplicationLifecycleDisabledTest, "UnrealHog.Lifecycle.ApplicationLifecycleHandler.DisabledSettingEmitsNoEventsAndDoesNotTouchLifecycleState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogApplicationLifecycleDisabledTest::RunTest(const FString& Parameters)
{
	FPostHogLifecycleHandlerTestStorage Storage;
	FPostHogApplicationMetadata Metadata = MakeLifecycleHandlerMetadata(TEXT("1.0.0"), TEXT("build-1"));
	TArray<FPostHogLifecycleCapturedEvent> CapturedEvents;
	int32 ForegroundCount = 0;
	int32 BackgroundCount = 0;

	FPostHogApplicationLifecycleHandler Handler(
		[&CapturedEvents](const FString& EventName, UPostHogEventProperties* Properties)
		{
			CaptureLifecycleHandlerEvent(CapturedEvents, EventName, Properties);
		},
		[&ForegroundCount]()
		{
			++ForegroundCount;
		},
		[&BackgroundCount]()
		{
			++BackgroundCount;
		},
		MakeLifecycleHandlerMetadataProvider(Metadata));

	Handler.Start(*MakeLifecycleHandlerSettings(/*bCaptureLifecycleEvents=*/false), Storage);
	Handler.NotifyApplicationBackgrounded();
	Handler.NotifyApplicationForegrounded();

	TestEqual(TEXT("No lifecycle events captured when disabled"), CapturedEvents.Num(), 0);
	TestFalse(TEXT("No lifecycle state created when disabled"), Storage.HasState(TEXT("lifecycle")));
	TestEqual(TEXT("No lifecycle state load when disabled"), Storage.GetLoadCount(TEXT("lifecycle")), 0);
	TestEqual(TEXT("No lifecycle state save when disabled"), Storage.GetSaveCount(TEXT("lifecycle")), 0);
	TestEqual(TEXT("Background callback still routed"), BackgroundCount, 1);
	TestEqual(TEXT("Foreground callback still routed"), ForegroundCount, 1);

	Handler.Stop();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
