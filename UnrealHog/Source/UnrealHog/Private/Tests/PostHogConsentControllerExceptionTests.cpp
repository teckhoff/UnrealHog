#include "Consent/PostHogConsentController.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogExceptionInput.h"
#include "PostHogDeveloperSettings.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace
{
	// RAII fixture that owns a unique temporary directory for the file storage provider backing
	// these exception tests; removed on scope exit even if an assertion fails mid-test.
	class FScopedExceptionTestStorageDirectory
	{
	public:
		FScopedExceptionTestStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogExceptionTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedExceptionTestStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakeExceptionTestSettings()
	{
		UPostHogDeveloperSettings* Settings = NewObject<UPostHogDeveloperSettings>(GetTransientPackage());
		UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("ApiKey"), TEXT("phc_valid_key"));
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bAnalyticsEnabled"), true);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bDefaultUserOptIn"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bCaptureApplicationLifecycleEvents"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bPreloadFeatureFlags"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bSessionReplay"), false);
		UnrealHogTests::SetPropertyValue<bool>(Settings, TEXT("bReuseAnonymousId"), false);
		return Settings;
	}

	// Same as MakeExceptionTestSettings but with an explicit host type (and optional custom host
	// string), for exercising $exception_personURL host derivation across US/EU/Custom.
	UPostHogDeveloperSettings* MakeExceptionTestSettingsWithHost(EPostHogHost HostType, const FString& CustomHost = FString())
	{
		UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();
		UnrealHogTests::SetPropertyValue<EPostHogHost>(Settings, TEXT("HostType"), HostType);
		if (HostType == EPostHogHost::Custom)
		{
			UnrealHogTests::SetPropertyValue<FString>(Settings, TEXT("Host"), CustomHost);
		}
		return Settings;
	}

	FPostHogConsentController::FStorageProviderFactory MakeExceptionStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	// Captures the most recently created fake transport so tests can drive its completion callbacks.
	FPostHogConsentController::FTransportFactory MakeExceptionTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	// Deterministic, countable stand-in for PostHogUuidV7::New().
	FPostHogConsentController::FUuidGenerator MakeExceptionUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("exception-uuid-%d"), ++Counter); };
	}

	bool TryGetExceptionPayloadEvents(const FPostHogBatchPayload& Payload, TArray<TSharedPtr<FJsonObject>>& OutEvents)
	{
		const TSharedRef<FJsonObject> PayloadJson = Payload.ToJsonObject();

		const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
		if (!PayloadJson->TryGetArrayField(TEXT("batch"), BatchArray))
		{
			return false;
		}

		OutEvents.Reset(BatchArray->Num());
		for (const TSharedPtr<FJsonValue>& EventValue : *BatchArray)
		{
			const TSharedPtr<FJsonObject> EventObject = EventValue->AsObject();
			if (!EventObject.IsValid())
			{
				return false;
			}
			OutEvents.Add(EventObject);
		}

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionValidHandledEmitsOneEventTest, "UnrealHog.Consent.ConsentController.CaptureException.ValidHandledExceptionEmitsOneEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionValidHandledEmitsOneEventTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");
	Exception.StackTrace = TEXT("Frame::One\nFrame::Two\nFrame::Three");
	Exception.bHandled = true;

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, nullptr);
	TestEqual(TEXT("CaptureException succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	FString EventName;
	TestTrue(TEXT("Event has name"), Events[0]->TryGetStringField(TEXT("event"), EventName));
	TestEqual(TEXT("Event is $exception"), EventName, FString(TEXT("$exception")));

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	const TArray<TSharedPtr<FJsonValue>>* ExceptionListArray = nullptr;
	TestTrue(TEXT("properties has $exception_list"), (*PropertiesObject)->TryGetArrayField(TEXT("$exception_list"), ExceptionListArray));
	TestEqual(TEXT("$exception_list has one entry"), ExceptionListArray->Num(), 1);
	if (ExceptionListArray->Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> ExceptionEntry = (*ExceptionListArray)[0]->AsObject();
	if (!TestTrue(TEXT("Exception entry is an object"), ExceptionEntry.IsValid()))
	{
		return false;
	}

	FString EntryType;
	TestTrue(TEXT("Entry has type"), ExceptionEntry->TryGetStringField(TEXT("type"), EntryType));
	TestEqual(TEXT("Entry type matches input"), EntryType, Exception.Type);

	FString EntryValue;
	TestTrue(TEXT("Entry has value"), ExceptionEntry->TryGetStringField(TEXT("value"), EntryValue));
	TestEqual(TEXT("Entry value matches input"), EntryValue, Exception.Message);

	const TSharedPtr<FJsonObject>* Mechanism = nullptr;
	TestTrue(TEXT("Entry has mechanism"), ExceptionEntry->TryGetObjectField(TEXT("mechanism"), Mechanism));
	bool bMechanismHandled = false;
	TestTrue(TEXT("mechanism has handled"), (*Mechanism)->TryGetBoolField(TEXT("handled"), bMechanismHandled));
	TestTrue(TEXT("mechanism.handled is true"), bMechanismHandled);
	FString MechanismSource;
	TestTrue(TEXT("mechanism has source"), (*Mechanism)->TryGetStringField(TEXT("source"), MechanismSource));
	TestEqual(TEXT("mechanism.source is unreal"), MechanismSource, FString(TEXT("unreal")));

	const TSharedPtr<FJsonObject>* Stacktrace = nullptr;
	TestTrue(TEXT("Entry has stacktrace"), ExceptionEntry->TryGetObjectField(TEXT("stacktrace"), Stacktrace));
	FString StacktraceType;
	TestTrue(TEXT("stacktrace has type"), (*Stacktrace)->TryGetStringField(TEXT("type"), StacktraceType));
	TestEqual(TEXT("stacktrace.type is raw"), StacktraceType, FString(TEXT("raw")));

	const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
	TestTrue(TEXT("stacktrace has frames"), (*Stacktrace)->TryGetArrayField(TEXT("frames"), Frames));
	TestEqual(TEXT("frames has 3 entries"), Frames->Num(), 3);

	FString TopLevelExceptionType;
	TestTrue(TEXT("properties has $exception_type"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_type"), TopLevelExceptionType));
	TestEqual(TEXT("$exception_type matches"), TopLevelExceptionType, Exception.Type);

	FString TopLevelExceptionMessage;
	TestTrue(TEXT("properties has $exception_message"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_message"), TopLevelExceptionMessage));
	TestEqual(TEXT("$exception_message matches"), TopLevelExceptionMessage, Exception.Message);

	FString TopLevelExceptionLevel;
	TestTrue(TEXT("properties has $exception_level"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_level"), TopLevelExceptionLevel));
	TestEqual(TEXT("$exception_level is error"), TopLevelExceptionLevel, FString(TEXT("error")));

	FString TopLevelExceptionSource;
	TestTrue(TEXT("properties has $exception_source"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_source"), TopLevelExceptionSource));
	TestEqual(TEXT("$exception_source is unreal_sdk"), TopLevelExceptionSource, FString(TEXT("unreal_sdk")));

	bool bTopLevelExceptionHandled = false;
	TestTrue(TEXT("properties has $exception_handled"), (*PropertiesObject)->TryGetBoolField(TEXT("$exception_handled"), bTopLevelExceptionHandled));
	TestTrue(TEXT("$exception_handled is true"), bTopLevelExceptionHandled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionMissingMessageIsSafeNoOpTest, "UnrealHog.Consent.ConsentController.CaptureException.MissingMessageIsSafeNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionMissingMessageIsSafeNoOpTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("rejected CaptureException with an empty or whitespace-only message or type"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("   ");
	Exception.Type = TEXT("EBreakageError");

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, nullptr);
	TestEqual(TEXT("Blank message rejected"), Result, EPostHogCaptureResult::InvalidEventName);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	if (LastTransport)
	{
		TestEqual(TEXT("No batch sent"), LastTransport->GetSentCount(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionMissingTypeIsSafeNoOpTest, "UnrealHog.Consent.ConsentController.CaptureException.MissingTypeIsSafeNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionMissingTypeIsSafeNoOpTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("rejected CaptureException with an empty or whitespace-only message or type"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("");

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, nullptr);
	TestEqual(TEXT("Blank type rejected"), Result, EPostHogCaptureResult::InvalidEventName);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	if (LastTransport)
	{
		TestEqual(TEXT("No batch sent"), LastTransport->GetSentCount(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionNotOptedInIsSafeNoOpTest, "UnrealHog.Consent.ConsentController.CaptureException.NotOptedInIsSafeNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionNotOptedInIsSafeNoOpTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("dropping event $exception"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, nullptr);
	TestEqual(TEXT("CaptureException without consent is rejected"), Result, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	TestNull(TEXT("No transport created"), LastTransport);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionOptionalPropertiesCannotOverrideExceptionFieldsTest, "UnrealHog.Consent.ConsentController.CaptureException.OptionalPropertiesCannotOverrideExceptionFields", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionOptionalPropertiesCannotOverrideExceptionFieldsTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Real message");
	Exception.Type = TEXT("ERealError");

	UPostHogEventProperties* CallerProperties = NewObject<UPostHogEventProperties>();
	CallerProperties->AddString(TEXT("$exception_type"), TEXT("spoofed"));
	CallerProperties->AddString(TEXT("$exception_message"), TEXT("spoofed message"));
	CallerProperties->AddString(TEXT("custom_key"), TEXT("v"));

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, CallerProperties);
	TestEqual(TEXT("CaptureException succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString ExceptionType;
	TestTrue(TEXT("properties has $exception_type"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_type"), ExceptionType));
	TestEqual(TEXT("$exception_type is the real type, not spoofed"), ExceptionType, Exception.Type);

	FString ExceptionMessage;
	TestTrue(TEXT("properties has $exception_message"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_message"), ExceptionMessage));
	TestEqual(TEXT("$exception_message is the real message, not spoofed"), ExceptionMessage, Exception.Message);

	FString CustomKeyValue;
	TestTrue(TEXT("properties retains custom_key"), (*PropertiesObject)->TryGetStringField(TEXT("custom_key"), CustomKeyValue));
	TestEqual(TEXT("custom_key value is correct"), CustomKeyValue, FString(TEXT("v")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionMultilineNonAsciiSerializesCorrectlyTest, "UnrealHog.Consent.ConsentController.CaptureException.MultilineNonAsciiSerializesCorrectly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionMultilineNonAsciiSerializesCorrectlyTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("café crash\n日本語のメッセージ");
	Exception.Type = TEXT("ENonAsciiError");
	Exception.StackTrace = TEXT("Frame::café\nFrame::日本語\n\nFrame::Three");

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, nullptr);
	TestEqual(TEXT("CaptureException succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString ExceptionMessage;
	TestTrue(TEXT("properties has $exception_message"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_message"), ExceptionMessage));
	TestEqual(TEXT("$exception_message round-trips exactly"), ExceptionMessage, Exception.Message);

	const TArray<TSharedPtr<FJsonValue>>* ExceptionListArray = nullptr;
	TestTrue(TEXT("properties has $exception_list"), (*PropertiesObject)->TryGetArrayField(TEXT("$exception_list"), ExceptionListArray));
	const TSharedPtr<FJsonObject> ExceptionEntry = ExceptionListArray->Num() > 0 ? (*ExceptionListArray)[0]->AsObject() : nullptr;
	if (!TestTrue(TEXT("Exception entry is an object"), ExceptionEntry.IsValid()))
	{
		return false;
	}

	FString EntryValue;
	TestTrue(TEXT("Entry has value"), ExceptionEntry->TryGetStringField(TEXT("value"), EntryValue));
	TestEqual(TEXT("Entry value round-trips exactly"), EntryValue, Exception.Message);

	const TSharedPtr<FJsonObject>* Stacktrace = nullptr;
	TestTrue(TEXT("Entry has stacktrace"), ExceptionEntry->TryGetObjectField(TEXT("stacktrace"), Stacktrace));
	const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
	TestTrue(TEXT("stacktrace has frames"), (*Stacktrace)->TryGetArrayField(TEXT("frames"), Frames));
	// The blank line between "Frame::日本語" and "Frame::Three" is culled, leaving 3 non-empty lines.
	TestEqual(TEXT("frames has one entry per non-empty line"), Frames->Num(), 3);

	if (Frames->Num() > 0)
	{
		const TSharedPtr<FJsonObject> FirstFrame = (*Frames)[0]->AsObject();
		if (TestTrue(TEXT("First frame is an object"), FirstFrame.IsValid()))
		{
			FString FunctionValue;
			TestTrue(TEXT("Frame has function"), FirstFrame->TryGetStringField(TEXT("function"), FunctionValue));
			TestEqual(TEXT("Frame function round-trips exactly"), FunctionValue, FString(TEXT("Frame::café")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionDistinctIdAttachedOnceTest, "UnrealHog.Consent.ConsentController.CaptureException.DistinctIdAttachedOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionDistinctIdAttachedOnceTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	Controller.Identify(TEXT("user-1"), nullptr, nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after identify"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, nullptr);
	TestEqual(TEXT("CaptureException succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	TestEqual(TEXT("One event queued"), Events.Num(), 1);
	if (Events.Num() != 1)
	{
		return false;
	}

	FString DistinctId;
	TestTrue(TEXT("Event has distinct_id"), Events[0]->TryGetStringField(TEXT("distinct_id"), DistinctId));
	TestEqual(TEXT("distinct_id matches identified user"), DistinctId, FString(TEXT("user-1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionPersonUrlUsHostTest, "UnrealHog.Consent.ConsentController.CaptureException.PersonUrlUsHostFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionPersonUrlUsHostTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettingsWithHost(EPostHogHost::US);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Identify(TEXT("user-1"), nullptr, nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after identify"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");

	TestEqual(TEXT("CaptureException succeeds"), Controller.CaptureException(Exception, nullptr), EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	if (!TestEqual(TEXT("One event queued"), Events.Num(), 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString PersonUrl;
	TestTrue(TEXT("properties has $exception_personURL"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_personURL"), PersonUrl));
	TestEqual(TEXT("Person URL matches US host"), PersonUrl, FString(TEXT("https://us.posthog.com/project/phc_valid_key/person/user-1")));
	TestFalse(TEXT("No duplicate slash after scheme"), PersonUrl.RightChop(8).Contains(TEXT("//")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionPersonUrlEuHostTest, "UnrealHog.Consent.ConsentController.CaptureException.PersonUrlEuHostFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionPersonUrlEuHostTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettingsWithHost(EPostHogHost::EU);

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Identify(TEXT("user-1"), nullptr, nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after identify"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");

	TestEqual(TEXT("CaptureException succeeds"), Controller.CaptureException(Exception, nullptr), EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	if (!TestEqual(TEXT("One event queued"), Events.Num(), 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString PersonUrl;
	TestTrue(TEXT("properties has $exception_personURL"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_personURL"), PersonUrl));
	TestEqual(TEXT("Person URL matches EU host"), PersonUrl, FString(TEXT("https://eu.posthog.com/project/phc_valid_key/person/user-1")));
	TestFalse(TEXT("No duplicate slash after scheme"), PersonUrl.RightChop(8).Contains(TEXT("//")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionPersonUrlCustomHostTest, "UnrealHog.Consent.ConsentController.CaptureException.PersonUrlCustomHostFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionPersonUrlCustomHostTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettingsWithHost(EPostHogHost::Custom, TEXT("https://my.i.posthog.com/"));

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Identify(TEXT("user-1"), nullptr, nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after identify"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");

	TestEqual(TEXT("CaptureException succeeds"), Controller.CaptureException(Exception, nullptr), EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	if (!TestEqual(TEXT("One event queued"), Events.Num(), 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString PersonUrl;
	TestTrue(TEXT("properties has $exception_personURL"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_personURL"), PersonUrl));
	TestEqual(TEXT("Person URL replaces .i. and does not duplicate trailing slash"), PersonUrl, FString(TEXT("https://my.posthog.com/project/phc_valid_key/person/user-1")));
	TestFalse(TEXT("No duplicate slash after scheme"), PersonUrl.RightChop(8).Contains(TEXT("//")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionPersonUrlAnonymousDistinctIdTest, "UnrealHog.Consent.ConsentController.CaptureException.PersonUrlAnonymousDistinctIdUsed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionPersonUrlAnonymousDistinctIdTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const FString AnonymousDistinctId = Controller.GetDistinctId();
	TestFalse(TEXT("Anonymous distinct id is assigned"), AnonymousDistinctId.IsEmpty());

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");

	TestEqual(TEXT("CaptureException succeeds"), Controller.CaptureException(Exception, nullptr), EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	if (!TestEqual(TEXT("One event queued"), Events.Num(), 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString PersonUrl;
	TestTrue(TEXT("properties has $exception_personURL"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_personURL"), PersonUrl));
	TestTrue(TEXT("Person URL ends with anonymous distinct id"), PersonUrl.EndsWith(AnonymousDistinctId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionPersonUrlNonAsciiDistinctIdTest, "UnrealHog.Consent.ConsentController.CaptureException.PersonUrlNonAsciiDistinctIdEncoded", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionPersonUrlNonAsciiDistinctIdTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Identify(TEXT("user café/日本語"), nullptr, nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after identify"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");

	TestEqual(TEXT("CaptureException succeeds"), Controller.CaptureException(Exception, nullptr), EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	if (!TestEqual(TEXT("One event queued"), Events.Num(), 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString PersonUrl;
	TestTrue(TEXT("properties has $exception_personURL"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_personURL"), PersonUrl));

	TestTrue(TEXT("Raw slash from distinct id is percent-encoded, not literal"), PersonUrl.Contains(TEXT("%2F")));
	TestFalse(TEXT("Non-ASCII characters are not present raw"), PersonUrl.Contains(TEXT("café")));

	int32 SegmentCount = 0;
	for (int32 Index = 0; Index < PersonUrl.Len(); ++Index)
	{
		if (PersonUrl[Index] == TEXT('/'))
		{
			++SegmentCount;
		}
	}
	// scheme "https://" contributes 2 slashes; host/project/<key>/person/<id> contributes 4 more = 6 total.
	TestEqual(TEXT("Encoded distinct id does not introduce an extra path segment"), SegmentCount, 6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogConsentControllerCaptureExceptionPersonUrlCallerCannotOverrideTest, "UnrealHog.Consent.ConsentController.CaptureException.PersonUrlCallerCannotOverride", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogConsentControllerCaptureExceptionPersonUrlCallerCannotOverrideTest::RunTest(const FString& Parameters)
{
	FScopedExceptionTestStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionStorageFactory(Fixture.GetRootPath()), MakeExceptionTransportFactory(LastTransport), MakeExceptionUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);
	Controller.Identify(TEXT("user-1"), nullptr, nullptr);
	Controller.Flush();
	if (TestNotNull(TEXT("Transport created after identify"), LastTransport))
	{
		LastTransport->CompleteLast(true, 200, TEXT(""));
	}

	FPostHogExceptionInput Exception;
	Exception.Message = TEXT("Something broke");
	Exception.Type = TEXT("EBreakageError");

	UPostHogEventProperties* CallerProperties = NewObject<UPostHogEventProperties>();
	CallerProperties->AddString(TEXT("$exception_personURL"), TEXT("https://evil.example/x"));

	TestEqual(TEXT("CaptureException succeeds"), Controller.CaptureException(Exception, CallerProperties), EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	if (!TestEqual(TEXT("One event queued"), Events.Num(), 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject));

	FString PersonUrl;
	TestTrue(TEXT("properties has $exception_personURL"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_personURL"), PersonUrl));
	TestEqual(TEXT("Person URL is the SDK-computed value, not the spoofed one"), PersonUrl, FString(TEXT("https://us.posthog.com/project/phc_valid_key/person/user-1")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
