#include "Events/PostHogExceptionPropertiesBuilder.h"

#include "Events/PostHogEventProperties.h"
#include "Events/PostHogExceptionInput.h"

void PostHogExceptionPropertiesBuilder::Build(UPostHogEventProperties& Props, const FPostHogExceptionInput& Exception)
{
	UPostHogEventPropertyArray* Frames = NewObject<UPostHogEventPropertyArray>();

	TArray<FString> Lines;
	Exception.StackTrace.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		const FString TrimmedLine = Line.TrimStartAndEnd();
		if (TrimmedLine.IsEmpty())
		{
			continue;
		}

		UPostHogEventProperties* Frame = NewObject<UPostHogEventProperties>();
		Frame->AddString(TEXT("platform"), TEXT("custom"));
		Frame->AddString(TEXT("lang"), TEXT("cpp"));
		Frame->AddString(TEXT("function"), TrimmedLine);

		Frames->AddObject(Frame);
	}

	UPostHogEventProperties* Stacktrace = NewObject<UPostHogEventProperties>();
	Stacktrace->AddString(TEXT("type"), TEXT("raw"));
	Stacktrace->AddArray(TEXT("frames"), Frames);

	UPostHogEventProperties* Mechanism = NewObject<UPostHogEventProperties>();
	Mechanism->AddString(TEXT("type"), TEXT("generic"));
	Mechanism->AddBoolean(TEXT("handled"), Exception.bHandled);
	Mechanism->AddString(TEXT("source"), TEXT("unreal"));
	Mechanism->AddBoolean(TEXT("synthetic"), false);

	UPostHogEventProperties* ExceptionEntry = NewObject<UPostHogEventProperties>();
	ExceptionEntry->AddString(TEXT("type"), Exception.Type);
	ExceptionEntry->AddString(TEXT("value"), Exception.Message);
	ExceptionEntry->AddObject(TEXT("mechanism"), Mechanism);
	ExceptionEntry->AddObject(TEXT("stacktrace"), Stacktrace);

	UPostHogEventPropertyArray* ExceptionList = NewObject<UPostHogEventPropertyArray>();
	ExceptionList->AddObject(ExceptionEntry);

	Props.AddArray(TEXT("$exception_list"), ExceptionList);
	Props.AddString(TEXT("$exception_type"), Exception.Type);
	Props.AddString(TEXT("$exception_message"), Exception.Message);
	Props.AddString(TEXT("$exception_level"), TEXT("error"));
	Props.AddString(TEXT("$exception_source"), TEXT("unreal_sdk"));
	Props.AddBoolean(TEXT("$exception_handled"), Exception.bHandled);
}
