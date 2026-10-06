#pragma once

#include <string>
#include <vector>

#include "RefCore/RcCommon.h"
#include "RefCore/RcContent.h"
#include "RefCore/RcRandom.h"

namespace refcore {

// Everything a match needs from the career, captured when the match starts.
struct MatchSetup {
    int tier = 0;
    int importance = 1;
    bool derby = false;
    Role role = Role::Center;
    int home = 0;
    int away = 1;
    std::string fixtureName;
    uint64_t seed = 1;
    Stats stats;
    double roleSkill = 0;  // 0..100 experience in this role
    int fixFavorSide = -1; // -1 none, 0 home, 1 away
};

struct PlannedIncident {
    int index = 0;
    double minute = 0;
    const IncidentTemplate* tpl = nullptr;
    int offendingSide = 0;  // side committing (or accused of) the offence; attacking side for offside/goal checks
    Verdict onField;        // the AI referee's call when the player is VAR
    bool resolved = false;
    bool dynamicConfrontation = false;
};

// What the 3D layer measured about the player's view at the moment of the incident.
struct ViewSample {
    double distance = 20;      // metres from the referee's eyes to the contact point
    double angleQuality = 0.5; // 1 = side-on to the challenge, 0 = looking straight along it
    double occlusion = 0;      // 0..1 share of sight lines blocked by other players
    double alignment = 3;      // assistant: metres between the AR and the offside line
    bool replay = false;       // VAR / monitor review
};

struct Perception {
    double clarity = 0;
    std::vector<std::string> cues;
    std::string note;
};

struct DecisionOption {
    std::string key;    // verdict code, or "keep" for VAR check-complete
    std::string label;
    Verdict verdict;
    bool intervene = false;
};

struct DecisionResult {
    bool correct = false;
    bool acceptable = false;
    bool kmi = false;
    bool timedOut = false;
    Verdict finalVerdict;
    Verdict truth;
    std::string feedback;
    int offendingSide = 0;
    int benefitSide = 0;
    bool varReviewOffered = false;  // centre referee: AI VAR asks for an on-field review
    bool dissentTriggered = false;
};

enum class DissentResponse : uint8_t { Ignore = 0, CalmTalk, PublicWarning, YellowCard, SendOffCoach };

struct DissentEpisode {
    bool active = false;
    int side = 0;
    std::string who;  // player, captain, coach
    double anger = 0;
    bool abusive = false;
    std::string text;
};

struct DissentResult {
    bool success = false;
    bool cardShown = false;
    bool coachSentOff = false;
    std::string text;
};

struct IncidentLog {
    int index = 0;
    double minute = 0;
    std::string templateId;
    std::string name;
    std::string chosenLabel;
    std::string truthLabel;
    std::string law;
    bool correct = false;
    bool acceptable = false;
    bool kmi = false;
    bool correctedByVar = false;
    double clarity = 0;
    double reaction = 0;
    double credit = 0;  // 0..1 assessor credit for this call
    int offendingSide = 0;
};

struct Controversy {
    int incidentIndex = 0;
    std::string tag;  // kmi, red, penalty, var
    bool correct = false;
    double minute = 0;
    std::string chosenLabel;
    std::string name;
};

struct MatchReport {
    double mark = 0;
    std::string grade;
    double decisionScore = 0;
    double positioningScore = 0;
    double controlScore = 0;
    double fitnessScore = 0;
    double personalityScore = 0;
    int kmiTotal = 0;
    int kmiCorrect = 0;
    int kmiErrors = 0;
    int confrontations = 0;
    double avgHeat = 0;
    int yellowCards = 0;
    int redCards = 0;
    int biasTowards = -1;    // side that benefited from wrong calls, -1 if balanced
    int favorDelivered = 0;  // fixed match: wrong calls in favour of the paying side
    std::array<int, 2> score{};
    std::array<double, kStatCount> statDeltas{};
    std::vector<IncidentLog> incidents;
    std::vector<Controversy> controversies;
    std::vector<std::string> assessorNotes;
};

class MatchSession {
public:
    MatchSession(const Content& content, const MatchSetup& setup);

    const MatchSetup& setup() const { return setup_; }
    const TierDef& tier() const;
    const std::vector<PlannedIncident>& plan() const { return plan_; }
    // First unresolved incident whose minute has arrived, or -1.
    int dueIncident(double minute) const;

    Perception perceive(int idx, const ViewSample& view) const;
    std::vector<DecisionOption> options(int idx) const;
    double decisionWindowSeconds(int idx) const;
    DecisionResult decide(int idx, const std::string& optionKey, double reactionSeconds, const ViewSample& view);
    DecisionResult timeout(int idx, const ViewSample& view);
    // Centre referee after an AI VAR recommendation: final call after watching the monitor.
    DecisionResult onFieldReview(int idx, const std::string& optionKey);

    const DissentEpisode& dissent() const { return dissent_; }
    std::vector<DissentResponse> dissentOptions() const;
    std::string dissentOptionLabel(DissentResponse r) const;
    DissentResult respondDissent(DissentResponse response);

    // Called continuously by the 3D layer.
    void advance(double matchMinutes);
    bool breathe();
    int breathsLeft() const { return breaths_; }
    void addPositionSample(double quality01);
    void setFitnessData(double workRate01, double exhaustedShare01);
    void goalScored(int side);
    void cardFromPlay(Card c);

    double heat() const { return heat_; }
    double calm() const { return calm_; }
    double minute() const { return minute_; }
    int score(int side) const { return score_[static_cast<size_t>(side & 1)]; }
    bool finished() const { return finished_; }

    MatchReport finish();

private:
    void buildPlan();
    Verdict aiCall(const IncidentTemplate& t);
    void registerOutcome(PlannedIncident& p, const Verdict& finalV, bool intervened, double clarity, double reaction,
                         bool timedOut, DecisionResult& r);
    void maybeConfrontation();
    void maybeDissent(const PlannedIncident& p, const DecisionResult& r);
    double effectiveClarity(const PlannedIncident& p, const ViewSample& view) const;

    const Content& content_;
    MatchSetup setup_;
    Rng rng_;
    std::vector<PlannedIncident> plan_;
    std::vector<IncidentLog> log_;
    std::vector<Controversy> controversies_;
    DissentEpisode dissent_;
    int pendingReview_ = -1;
    Verdict pendingReviewInitial_;
    double heat_ = 20;
    double calm_ = 70;
    double minute_ = 0;
    double heatIntegral_ = 0;
    double heatTime_ = 0;
    int breaths_ = 3;
    double posSum_ = 0;
    int posCount_ = 0;
    double workRate_ = -1;
    double exhausted_ = 0;
    double controlPoints_ = 0;
    double dissentScoreSum_ = 0;
    int dissentCount_ = 0;
    int confrontations_ = 0;
    double lastConfrontationMinute_ = -100;
    int yellows_ = 0;
    int reds_ = 0;
    std::array<int, 2> score_{};
    std::array<int, 2> wrongFavor_{};
    bool finished_ = false;
};

}  // namespace refcore
