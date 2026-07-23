#include "Events/PostHogExceptionLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Consent/PostHogConsentController.h"
#include "Events/PostHogBatchPayload.h"
#include "Events/PostHogEventProperties.h"
#include "Events/PostHogExceptionInput.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "PostHogDeveloperSettings.h"
#include "Storage/PostHogFileStorageProvider.h"
#include "Tests/PostHogFakeBatchTransport.h"
#include "Tests/PostHogTestPropertyHelpers.h"
#include "UObject/Package.h"

namespace
{
	// Local copies of the consent-controller exception fixture helpers, prefixed to avoid symbol
	// conflicts with PostHogConsentControllerExceptionTests.cpp in a unity build.
	class FScopedExceptionLibraryStorageDirectory
	{
	public:
		FScopedExceptionLibraryStorageDirectory()
			: RootPath(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("UnrealHogExceptionLibraryTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FScopedExceptionLibraryStorageDirectory()
		{
			IFileManager::Get().DeleteDirectory(*RootPath, false, true);
		}

		const FString& GetRootPath() const { return RootPath; }

	private:
		FString RootPath;
	};

	UPostHogDeveloperSettings* MakeExceptionLibraryTestSettings()
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

	FPostHogConsentController::FStorageProviderFactory MakeExceptionLibraryStorageFactory(const FString& RootPath)
	{
		return [RootPath]() -> TUniquePtr<IPostHogStorageProvider>
		{
			return MakeUnique<FPostHogFileStorageProvider>(RootPath);
		};
	}

	FPostHogConsentController::FTransportFactory MakeExceptionLibraryTransportFactory(FPostHogFakeBatchTransport*& OutLastTransport)
	{
		return [&OutLastTransport](const FString&) -> TUniquePtr<IPostHogBatchTransport>
		{
			TUniquePtr<FPostHogFakeBatchTransport> Transport = MakeUnique<FPostHogFakeBatchTransport>();
			OutLastTransport = Transport.Get();
			return Transport;
		};
	}

	FPostHogConsentController::FUuidGenerator MakeExceptionLibraryUuidGenerator(int32& Counter)
	{
		return [&Counter]() { return FString::Printf(TEXT("exception-library-uuid-%d"), ++Counter); };
	}

	bool TryGetExceptionLibraryPayloadEvents(const FPostHogBatchPayload& Payload, TArray<TSharedPtr<FJsonObject>>& OutEvents)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionLibraryExplicitStackCopiesFieldsVerbatimTest, "UnrealHog.Events.ExceptionLibrary.ExplicitStackCopiesFieldsVerbatim", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionLibraryExplicitStackCopiesFieldsVerbatimTest::RunTest(const FString& Parameters)
{
	const FString Message = TEXT("café crash\n日本語のメッセージ");
	const FString Type = TEXT("ENonAsciiError");
	const FString StackTrace = TEXT("  Frame::café\n\tFrame::日本語\n\nFrame::Three  ");

	const FPostHogExceptionInput Exception =
		UPostHogExceptionLibrary::MakeExceptionWithStackTrace(Message, Type, StackTrace, false);

	TestEqual(TEXT("Message copied verbatim"), Exception.Message, Message);
	TestEqual(TEXT("Type copied verbatim"), Exception.Type, Type);
	TestEqual(TEXT("StackTrace copied verbatim, including whitespace and blank lines"), Exception.StackTrace, StackTrace);
	TestFalse(TEXT("bHandled=false propagates"), Exception.bHandled);

	const FPostHogExceptionInput HandledException =
		UPostHogExceptionLibrary::MakeExceptionWithStackTrace(Message, Type, StackTrace, true);
	TestTrue(TEXT("bHandled=true propagates"), HandledException.bHandled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionLibraryExplicitStackAcceptsEmptyStackTraceTest, "UnrealHog.Events.ExceptionLibrary.ExplicitStackAcceptsEmptyStackTrace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionLibraryExplicitStackAcceptsEmptyStackTraceTest::RunTest(const FString& Parameters)
{
	const FPostHogExceptionInput Exception = UPostHogExceptionLibrary::MakeExceptionWithStackTrace(
		TEXT("Door unlock failed"), TEXT("GameplayError"), FString(), true);

	TestEqual(TEXT("Message intact"), Exception.Message, FString(TEXT("Door unlock failed")));
	TestEqual(TEXT("Type intact"), Exception.Type, FString(TEXT("GameplayError")));
	TestTrue(TEXT("Empty stack stays empty (no invented frames)"), Exception.StackTrace.IsEmpty());
	TestTrue(TEXT("bHandled intact"), Exception.bHandled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionLibraryNativeStackReturnsStructWithoutCrashTest, "UnrealHog.Events.ExceptionLibrary.NativeStackReturnsStructWithoutCrash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionLibraryNativeStackReturnsStructWithoutCrashTest::RunTest(const FString& Parameters)
{
	const FPostHogExceptionInput Exception =
		UPostHogExceptionLibrary::MakeExceptionWithCurrentStack(TEXT("m"), TEXT("t"));

	TestEqual(TEXT("Message intact"), Exception.Message, FString(TEXT("m")));
	TestEqual(TEXT("Type intact"), Exception.Type, FString(TEXT("t")));
	TestTrue(TEXT("bHandled defaults to true"), Exception.bHandled);

	// Native stack capture is best-effort: symbol content and even nonemptiness vary by platform,
	// optimization, and symbol packaging, so this is reported rather than asserted.
	if (Exception.StackTrace.IsEmpty())
	{
		AddInfo(TEXT("Native stack walk returned empty text on this platform/configuration; empty StackTrace preserved."));
	}
	else
	{
		AddInfo(FString::Printf(TEXT("Native stack walk returned %d characters."), Exception.StackTrace.Len()));
	}

	// When native symbols resolve, the SDK capture wrapper must be trimmed from the top so the
	// caller is the first reported frame. Guarded by non-empty check because native output can be
	// empty on any platform/config. Uses Contains (not StartsWith) since frame lines may carry
	// address/module prefixes.
	if (!Exception.StackTrace.IsEmpty())
	{
		FString FirstFrame;
		TArray<FString> Lines;
		Exception.StackTrace.ParseIntoArrayLines(Lines, false);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (!Trimmed.IsEmpty())
			{
				FirstFrame = Trimmed;
				break;
			}
		}

		AddInfo(FString::Printf(TEXT("First native frame: %s"), *FirstFrame));
		TestFalse(
			TEXT("Current-stack helper frame trimmed from top of native stack"),
			FirstFrame.Contains(TEXT("MakeExceptionWithCurrentStack")));
	}

	const FPostHogExceptionInput UnhandledException =
		UPostHogExceptionLibrary::MakeExceptionWithNativeStack(TEXT("m2"), TEXT("t2"), false);
	TestEqual(TEXT("Native helper message intact"), UnhandledException.Message, FString(TEXT("m2")));
	TestFalse(TEXT("Native helper bHandled=false propagates"), UnhandledException.bHandled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionLibraryBlueprintThunkUsesScriptFrameTest, "UnrealHog.Events.ExceptionLibrary.BlueprintThunkUsesScriptFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionLibraryBlueprintThunkUsesScriptFrameTest::RunTest(const FString& Parameters)
{
	UFunction* Function = UPostHogExceptionLibrary::StaticClass()->FindFunctionByName(TEXT("MakeExceptionWithCurrentStack"));
	if (!TestNotNull(TEXT("MakeExceptionWithCurrentStack is exposed to the Blueprint VM"), Function))
	{
		return false;
	}

	TestTrue(TEXT("Function is native"), Function->HasAnyFunctionFlags(FUNC_Native));

	// Parameter layout must match the UFUNCTION declaration order.
	struct FMakeExceptionWithCurrentStackParams
	{
		FString Message;
		FString Type;
		bool bHandled;
		FPostHogExceptionInput ReturnValue;
	};

	FMakeExceptionWithCurrentStackParams Params;
	Params.Message = TEXT("Blueprint failure");
	Params.Type = TEXT("BlueprintError");
	Params.bHandled = false;

	GetMutableDefault<UPostHogExceptionLibrary>()->ProcessEvent(Function, &Params);

	TestEqual(TEXT("Thunk copies Message"), Params.ReturnValue.Message, FString(TEXT("Blueprint failure")));
	TestEqual(TEXT("Thunk copies Type"), Params.ReturnValue.Type, FString(TEXT("BlueprintError")));
	TestFalse(TEXT("Thunk copies bHandled"), Params.ReturnValue.bHandled);

	AddInfo(FString::Printf(TEXT("Script-frame stack trace length: %d"), Params.ReturnValue.StackTrace.Len()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionLibraryHelperCreatesNoSideEffectsTest, "UnrealHog.Events.ExceptionLibrary.HelperCreatesNoSideEffects", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionLibraryHelperCreatesNoSideEffectsTest::RunTest(const FString& Parameters)
{
	FScopedExceptionLibraryStorageDirectory Fixture;
	IFileManager::Get().MakeDirectory(*Fixture.GetRootPath(), true);

	auto ListFiles = [&Fixture]()
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Fixture.GetRootPath(), TEXT("*"), true, true);
		Files.Sort();
		return Files;
	};

	const TArray<FString> Before = ListFiles();

	// The helpers take no subsystem, controller, or storage handle: they can only allocate structs.
	const FPostHogExceptionInput A = UPostHogExceptionLibrary::MakeExceptionWithStackTrace(TEXT("a"), TEXT("A"), TEXT("Frame::One"), true);
	const FPostHogExceptionInput B = UPostHogExceptionLibrary::MakeExceptionWithNativeStack(TEXT("b"), TEXT("B"), false);
	const FPostHogExceptionInput C = UPostHogExceptionLibrary::MakeExceptionWithCurrentStack(TEXT("c"), TEXT("C"));

	TestEqual(TEXT("Explicit helper produced its struct"), A.Message, FString(TEXT("a")));
	TestEqual(TEXT("Native helper produced its struct"), B.Message, FString(TEXT("b")));
	TestEqual(TEXT("Current-stack helper produced its struct"), C.Message, FString(TEXT("c")));

	const TArray<FString> After = ListFiles();
	TestEqual(TEXT("No storage files created by helper calls"), After.Num(), Before.Num());
	TestTrue(TEXT("Storage directory contents unchanged"), After == Before);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionLibraryConsentBlockedHelperExceptionIsNoOpTest, "UnrealHog.Events.ExceptionLibrary.ConsentBlockedHelperExceptionIsNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionLibraryConsentBlockedHelperExceptionIsNoOpTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("dropping event $exception"), EAutomationExpectedErrorFlags::Contains, 1, false);

	FScopedExceptionLibraryStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionLibraryStorageFactory(Fixture.GetRootPath()), MakeExceptionLibraryTransportFactory(LastTransport), MakeExceptionLibraryUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionLibraryTestSettings();

	Controller.Initialize(*Settings);

	const FPostHogExceptionInput Exception = UPostHogExceptionLibrary::MakeExceptionWithStackTrace(
		TEXT("Something broke"), TEXT("EBreakageError"), TEXT("Frame::One\nFrame::Two"), true);

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, nullptr);
	TestFalse(TEXT("Consent-blocked capture is not a success"), Result == EPostHogCaptureResult::Success);
	TestEqual(TEXT("Consent-blocked capture reports NotOptedIn"), Result, EPostHogCaptureResult::NotOptedIn);
	TestEqual(TEXT("No events queued"), Controller.GetQueuedEventCount(), 0);
	TestNull(TEXT("No transport created"), LastTransport);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPostHogExceptionLibraryHelperExceptionProducesStandardPayloadShapeTest, "UnrealHog.Events.ExceptionLibrary.HelperExceptionProducesStandardPayloadShape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPostHogExceptionLibraryHelperExceptionProducesStandardPayloadShapeTest::RunTest(const FString& Parameters)
{
	FScopedExceptionLibraryStorageDirectory Fixture;
	FPostHogFakeBatchTransport* LastTransport = nullptr;
	int32 UuidCounter = 0;

	FPostHogConsentController Controller(MakeExceptionLibraryStorageFactory(Fixture.GetRootPath()), MakeExceptionLibraryTransportFactory(LastTransport), MakeExceptionLibraryUuidGenerator(UuidCounter));
	UPostHogDeveloperSettings* Settings = MakeExceptionLibraryTestSettings();

	Controller.Initialize(*Settings);
	Controller.SetOptIn(true, *Settings);

	const FPostHogExceptionInput Exception = UPostHogExceptionLibrary::MakeExceptionWithStackTrace(
		TEXT("Door unlock failed"), TEXT("GameplayError"), TEXT("Frame::One\nFrame::Two\nFrame::Three"), true);

	const EPostHogCaptureResult Result = Controller.CaptureException(Exception, nullptr);
	TestEqual(TEXT("CaptureException succeeds"), Result, EPostHogCaptureResult::Success);

	Controller.Flush();
	if (!TestNotNull(TEXT("Transport created"), LastTransport))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonObject>> Events;
	TestTrue(TEXT("Payload parsed"), TryGetExceptionLibraryPayloadEvents(LastTransport->GetLastPayload(), Events));
	LastTransport->CompleteLast(true, 200, TEXT(""));

	if (!TestEqual(TEXT("One event queued"), Events.Num(), 1))
	{
		return false;
	}

	FString EventName;
	TestTrue(TEXT("Event has name"), Events[0]->TryGetStringField(TEXT("event"), EventName));
	TestEqual(TEXT("Event is $exception"), EventName, FString(TEXT("$exception")));

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	if (!TestTrue(TEXT("Event has properties"), Events[0]->TryGetObjectField(TEXT("properties"), PropertiesObject)))
	{
		return false;
	}

	FString ExceptionType;
	TestTrue(TEXT("properties has $exception_type"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_type"), ExceptionType));
	TestEqual(TEXT("$exception_type matches helper input"), ExceptionType, Exception.Type);

	FString ExceptionMessage;
	TestTrue(TEXT("properties has $exception_message"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_message"), ExceptionMessage));
	TestEqual(TEXT("$exception_message matches helper input"), ExceptionMessage, Exception.Message);

	FString ExceptionLevel;
	TestTrue(TEXT("properties has $exception_level"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_level"), ExceptionLevel));
	TestEqual(TEXT("$exception_level is error"), ExceptionLevel, FString(TEXT("error")));

	FString ExceptionSource;
	TestTrue(TEXT("properties has $exception_source"), (*PropertiesObject)->TryGetStringField(TEXT("$exception_source"), ExceptionSource));
	TestEqual(TEXT("$exception_source is unreal_sdk"), ExceptionSource, FString(TEXT("unreal_sdk")));

	bool bExceptionHandled = false;
	TestTrue(TEXT("properties has $exception_handled"), (*PropertiesObject)->TryGetBoolField(TEXT("$exception_handled"), bExceptionHandled));
	TestTrue(TEXT("$exception_handled is true"), bExceptionHandled);

	const TArray<TSharedPtr<FJsonValue>>* ExceptionListArray = nullptr;
	if (!TestTrue(TEXT("properties has $exception_list"), (*PropertiesObject)->TryGetArrayField(TEXT("$exception_list"), ExceptionListArray)))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> ExceptionEntry = ExceptionListArray->Num() > 0 ? (*ExceptionListArray)[0]->AsObject() : nullptr;
	if (!TestTrue(TEXT("Exception entry is an object"), ExceptionEntry.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Stacktrace = nullptr;
	if (!TestTrue(TEXT("Entry has stacktrace"), ExceptionEntry->TryGetObjectField(TEXT("stacktrace"), Stacktrace)))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
	if (!TestTrue(TEXT("stacktrace has frames"), (*Stacktrace)->TryGetArrayField(TEXT("frames"), Frames)))
	{
		return false;
	}

	TestEqual(TEXT("Helper stack split into one frame per line"), Frames->Num(), 3);
	if (Frames->Num() > 0)
	{
		const TSharedPtr<FJsonObject> FirstFrame = (*Frames)[0]->AsObject();
		if (TestTrue(TEXT("First frame is an object"), FirstFrame.IsValid()))
		{
			FString FrameLang;
			TestTrue(TEXT("Frame has lang"), FirstFrame->TryGetStringField(TEXT("lang"), FrameLang));
			TestEqual(TEXT("Frame lang is cpp"), FrameLang, FString(TEXT("cpp")));

			FString FrameFunction;
			TestTrue(TEXT("Frame has function"), FirstFrame->TryGetStringField(TEXT("function"), FrameFunction));
			TestEqual(TEXT("Frame function matches helper stack line"), FrameFunction, FString(TEXT("Frame::One")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
