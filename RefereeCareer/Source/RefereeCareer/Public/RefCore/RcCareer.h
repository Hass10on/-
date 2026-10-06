#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "RefCore/RcCommon.h"
#include "RefCore/RcContent.h"
#include "RefCore/RcJson.h"
#include "RefCore/RcMatch.h"
#include "RefCore/RcRandom.h"

namespace refcore {

// The weekly loop: SeasonStart -> Planning -> Event* -> Appointment -> Match -> PostMatch -> Press* -> (next week)
// After the last week of a season: SeasonReview -> SeasonStart. Over ends the career.
enum class Phase : uint8_t { SeasonStart, Planning, Event, Appointment, Match, PostMatch, Press, SeasonReview, Over };
const char* phaseId(Phase p);

struct Fixture {
    int home = 0;
    int away = 1;
    int importance = 1;
    bool derby = false;
    std::string title;      // big-match title (e.g. the cup final), empty for league games
    std::string flag;       // flag set when refereed
    double minScore = 0;    // extra appointment-index requirement (finals)
    double minForm = 0;     // recent average mark required
};

struct Appointment {
    bool appointed = false;
    Fixture fixture;
    Role role = Role::Center;
    double score = 0;        // appointment index 0..100
    std::string reason;      // why this match
    std::string blocked;     // what kept the referee from a bigger match
    std::vector<std::string> otherFixtures;
};

struct HistoryEntry {
    int season = 0;
    int week = 0;
    int tier = 0;
    Role role = Role::Center;
    std::string fixture;
    int importance = 1;
    double mark = 0;
    int kmiErrors = 0;
};

struct PressQuestion {
    std::string text;
    std::string tag;
    bool hasTruth = false;
    bool correct = true;
    std::vector<PressAnswerDef> answers;
};

struct SeasonReview {
    std::string title;
    std::string text;
    std::vector<std::string> checks;  // each requirement, prefixed with the pass/fail symbol
    int fromTier = 0;
    int toTier = 0;
    bool promoted = false;
    bool demoted = false;
    double avgMark = 0;
    int matches = 0;
};

struct ActivityStatus {
    const ActivityDef* def = nullptr;
    bool available = false;
    std::string reason;
};

struct EventChoiceStatus {
    bool available = true;
    std::string reason;
};

class Career {
public:
    explicit Career(const Content& content);

    void startNew(const std::string& refereeName, Role specialisation, uint64_t seed);
    Json toJson() const;
    bool fromJson(const Json& j, std::string* error = nullptr);

    // State
    Phase phase() const { return phase_; }
    const std::string& name() const { return name_; }
    int age() const { return age_; }
    int season() const { return season_; }
    int week() const { return week_; }
    int tierIndex() const { return tier_; }
    const TierDef& tier() const;
    Role specialisation() const { return specialisation_; }
    Role preferredRole() const { return preferred_; }
    const Stats& stats() const { return stats_; }
    double money() const { return money_; }
    double roleSkill(Role r) const { return roleSkill_[static_cast<size_t>(r)]; }
    bool hasFlag(const std::string& f) const { return flags_.count(f) > 0; }
    double suspicion() const { return suspicion_; }
    int injuredWeeks() const { return injured_; }
    int suspendedWeeks() const { return suspended_; }
    const std::vector<HistoryEntry>& history() const { return history_; }
    int seasonMatches() const;
    double seasonAverage() const;
    double recentAverage(int n) const;
    const std::vector<std::string>& news() const { return news_; }
    const std::string& endingId() const { return ending_; }
    std::string endingTitle() const;
    std::string endingText() const;
    double appointmentIndex() const;
    bool canWorkAs(Role r, std::string* why = nullptr) const;

    // SeasonStart
    const std::string& seasonIntro() const { return seasonIntro_; }
    void confirmSeasonStart();

    // Planning
    int slots() const { return content_.slotsPerWeek; }
    std::vector<ActivityStatus> activities() const;
    std::vector<std::string> commitWeek(const std::vector<std::string>& activityIds, Role preferred);

    // Event
    const EventDef* currentEvent() const;
    std::vector<EventChoiceStatus> eventChoices() const;
    std::string chooseEvent(int choice);

    // Appointment
    const Appointment& appointment() const { return appointment_; }
    MatchSetup matchSetup() const;
    void acceptAppointment();

    // Match
    void completeMatch(const MatchReport& report);
    const MatchReport& lastReport() const { return lastReport_; }
    const std::vector<std::string>& lastMatchNews() const { return matchNews_; }
    void continueAfterReport();

    // Press
    const std::vector<PressQuestion>& pressQuestions() const { return press_; }
    int pressIndex() const { return pressIndex_; }
    std::string answerPress(int answer);

    // Season review
    const SeasonReview& review() const { return review_; }
    void continueAfterReview();

private:
    void beginSeason();
    void beginWeek();
    void rollEvents();
    void openNextEventOrAppointment();
    void computeAppointment();
    void endWeek();
    void buildReview();
    void buildPress(const MatchReport& rep);
    void applyEffects(const Effects& e, std::vector<std::string>* log);
    bool meets(const Requirements& r, std::string* why) const;
    std::string teamName(int tierIdx, int team) const;
    std::string fixtureLabel(const Fixture& f) const;
    void setEnding(const std::string& id);
    void addNews(const std::string& line);

    const Content& content_;
    Rng rng_;
    Phase phase_ = Phase::SeasonStart;
    std::string name_;
    Role specialisation_ = Role::Center;
    Role preferred_ = Role::Center;
    int age_ = 22;
    int season_ = 1;
    int week_ = 1;
    int tier_ = 0;
    int seasonsInTier_ = 1;
    Stats stats_;
    double money_ = 0;
    std::array<double, kRoleCount> roleSkill_{};
    std::set<std::string> flags_;
    std::set<std::string> seenEvents_;
    std::map<std::string, int> progress_;
    std::vector<std::pair<std::string, int>> scheduled_;  // event id, absolute week number
    std::vector<std::string> eventQueue_;
    std::vector<HistoryEntry> history_;
    std::vector<std::string> news_;
    std::vector<std::string> matchNews_;
    int injured_ = 0;
    int suspended_ = 0;
    int resting_ = 0;
    int cooldown_ = 0;
    double suspicion_ = 0;
    int fixFavor_ = -1;  // pending fix: 0 home, 1 away
    double fixReward_ = 0;
    bool fixedThisMatch_ = false;
    std::string seasonIntro_;
    Appointment appointment_;
    MatchReport lastReport_;
    std::vector<PressQuestion> press_;
    int pressIndex_ = 0;
    SeasonReview review_;
    std::string ending_;
    int totalWeeks_ = 0;
};

}  // namespace refcore
