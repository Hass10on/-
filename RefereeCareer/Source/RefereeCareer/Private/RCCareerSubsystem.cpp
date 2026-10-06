#include "RCCareerSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RefereeCareer.h"

void URCCareerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const char* Names[] = {"career.json", "incidents.json", "events.json", "press.json", "strings.json"};
	std::string Texts[5];
	for (int32 i = 0; i < 5; ++i)
	{
		const FString Path = FPaths::Combine(DataDir(), UTF8_TO_TCHAR(Names[i]));
		if (!ReadUtf8(Path, Texts[i]))
		{
			LoadError = FString::Printf(TEXT("Cannot read %s"), *Path);
			UE_LOG(LogReferee, Error, TEXT("%s"), *LoadError);
			return;
		}
	}
	std::string Error;
	bContentLoaded = Content.loadFromStrings(Texts[0], Texts[1], Texts[2], Texts[3], Texts[4], &Error);
	if (!bContentLoaded)
	{
		LoadError = ToF(Error);
		UE_LOG(LogReferee, Error, TEXT("Game data: %s"), *LoadError);
	}
	else
	{
		UE_LOG(LogReferee, Log, TEXT("Game data loaded: %d tiers, %d incidents, %d events"), (int32)Content.tiers.size(),
			(int32)Content.incidents.size(), (int32)Content.events.size());
	}
}

void URCCareerSubsystem::Deinitialize()
{
	Match.Reset();
	Career.Reset();
	Super::Deinitialize();
}

URCCareerSubsystem* URCCareerSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<URCCareerSubsystem>() : nullptr;
}

FString URCCareerSubsystem::DataDir() const
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"));
}

bool URCCareerSubsystem::ReadUtf8(const FString& Path, std::string& Out) const
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
	{
		return false;
	}
	Out.assign(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num()));
	return true;
}

bool URCCareerSubsystem::LoadJson(const FString& FileName, refcore::Json& Out) const
{
	std::string Text;
	if (!ReadUtf8(FPaths::Combine(DataDir(), FileName), Text))
	{
		return false;
	}
	std::string Error;
	if (!refcore::Json::parse(Text, Out, &Error))
	{
		UE_LOG(LogReferee, Warning, TEXT("%s: %s"), *FileName, *ToF(Error));
		return false;
	}
	return true;
}

FString URCCareerSubsystem::SavePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), TEXT("RefereeCareer.json"));
}

void URCCareerSubsystem::NewCareer(const FString& Name, refcore::Role Role)
{
	Match.Reset();
	Career = MakeUnique<refcore::Career>(Content);
	const uint64 Seed = static_cast<uint64>(FDateTime::UtcNow().GetTicks()) ^ 0x5EEDu;
	Career->startNew(ToStd(Name), Role, Seed);
	SaveCareer();
}

bool URCCareerSubsystem::HasSave() const
{
	return FPaths::FileExists(SavePath());
}

bool URCCareerSubsystem::LoadCareer()
{
	std::string Text;
	if (!bContentLoaded || !ReadUtf8(SavePath(), Text))
	{
		return false;
	}
	refcore::Json J;
	std::string Error;
	if (!refcore::Json::parse(Text, J, &Error))
	{
		UE_LOG(LogReferee, Warning, TEXT("Save file is damaged: %s"), *ToF(Error));
		return false;
	}
	TUniquePtr<refcore::Career> Loaded = MakeUnique<refcore::Career>(Content);
	if (!Loaded->fromJson(J, &Error))
	{
		UE_LOG(LogReferee, Warning, TEXT("Save file rejected: %s"), *ToF(Error));
		return false;
	}
	Match.Reset();
	Career = MoveTemp(Loaded);
	return true;
}

bool URCCareerSubsystem::SaveCareer() const
{
	if (!Career)
	{
		return false;
	}
	const std::string Text = Career->toJson().dump(1);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Text.data()), static_cast<int32>(Text.size()));
	const bool bOk = FFileHelper::SaveArrayToFile(Bytes, *SavePath());
	if (!bOk)
	{
		UE_LOG(LogReferee, Warning, TEXT("Could not write %s"), *SavePath());
	}
	return bOk;
}

refcore::MatchSession* URCCareerSubsystem::BeginMatch()
{
	if (!Career || Career->phase() != refcore::Phase::Match)
	{
		return nullptr;
	}
	Match = MakeUnique<refcore::MatchSession>(Content, Career->matchSetup());
	return Match.Get();
}

void URCCareerSubsystem::FinishMatch()
{
	if (!Career || !Match)
	{
		return;
	}
	const refcore::MatchReport Report = Match->finish();
	Career->completeMatch(Report);
	Match.Reset();
	SaveCareer();
}

void URCCareerSubsystem::AbandonMatch()
{
	Match.Reset();
}

FString URCCareerSubsystem::ToF(const std::string& S)
{
	const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(S.data()), static_cast<int32>(S.size()));
	FString Out;
	Out.AppendChars(Conv.Get(), Conv.Length());
	return Out;
}

std::string URCCareerSubsystem::ToStd(const FString& S)
{
	const FTCHARToUTF8 Conv(*S);
	return std::string(reinterpret_cast<const char*>(Conv.Get()), static_cast<size_t>(Conv.Length()));
}

FString URCCareerSubsystem::Str(const std::string& Key) const
{
	return ToF(Content.str(Key));
}

FString URCCareerSubsystem::Fmt(const std::string& Key, std::initializer_list<std::pair<std::string, FString>> Vars) const
{
	std::vector<std::pair<std::string, std::string>> V;
	for (const auto& KV : Vars)
	{
		V.emplace_back(KV.first, ToStd(KV.second));
	}
	return ToF(Content.fmt(Key, V));
}
