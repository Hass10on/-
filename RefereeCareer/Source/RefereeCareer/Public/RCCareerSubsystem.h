#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include <initializer_list>
#include <string>
#include <utility>

#include "RefCore/RcCareer.h"
#include "RefCore/RcContent.h"
#include "RefCore/RcJson.h"
#include "RefCore/RcMatch.h"

#include "RCCareerSubsystem.generated.h"

/**
 * Owns the career rules (RefCore) for the whole session: game data from Content/Data, the current career,
 * the match in progress, and the save file in Saved/SaveGames/RefereeCareer.json.
 */
UCLASS()
class REFEREECAREER_API URCCareerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static URCCareerSubsystem* Get(const UObject* WorldContext);

	bool IsContentLoaded() const { return bContentLoaded; }
	const FString& GetLoadError() const { return LoadError; }
	const refcore::Content& GetContent() const { return Content; }
	refcore::Career* GetCareer() const { return Career.Get(); }
	refcore::MatchSession* GetMatch() const { return Match.Get(); }

	void NewCareer(const FString& Name, refcore::Role Role);
	bool HasSave() const;
	bool LoadCareer();
	bool SaveCareer() const;

	refcore::MatchSession* BeginMatch();
	/** Hands the finished match report to the career and saves. */
	void FinishMatch();
	void AbandonMatch();

	// Text helpers (all game text lives in Content/Data/strings.json).
	FString Str(const std::string& Key) const;
	FString Fmt(const std::string& Key, std::initializer_list<std::pair<std::string, FString>> Vars) const;
	static FString ToF(const std::string& S);
	static std::string ToStd(const FString& S);

	FString DataDir() const;
	bool LoadJson(const FString& FileName, refcore::Json& Out) const;

private:
	bool ReadUtf8(const FString& Path, std::string& Out) const;
	FString SavePath() const;

	refcore::Content Content;
	TUniquePtr<refcore::Career> Career;
	TUniquePtr<refcore::MatchSession> Match;
	bool bContentLoaded = false;
	FString LoadError;
};
