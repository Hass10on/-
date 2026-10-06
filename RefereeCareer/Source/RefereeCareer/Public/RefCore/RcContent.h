#pragma once

#include <map>
#include <string>
#include <vector>

#include "RefCore/RcCommon.h"
#include "RefCore/RcJson.h"

namespace refcore {

// What a choice, activity or press answer does to the career.
struct Effects {
    std::array<double, kStatCount> stats{};
    double money = 0;
    double roleSkill = 0;
    double suspicion = 0;
    std::vector<std::string> setFlags;
    std::vector<std::string> clearFlags;
    int suspendWeeks = 0;
    int injuryWeeks = 0;
    int restWeeks = 0;          // voluntary unavailability (family, holidays)
    std::string ending;         // ends the career with this ending id
    std::string fix;            // "home" / "away": the next appointed match is fixed in favour of that side
    double fixReward = 0;
    std::string scheduleEvent;  // follow-up event id
    int scheduleInWeeks = 0;

    static Effects fromJson(const Json& j);
};

// Conditions for events, event choices and activities.
struct Requirements {
    int minTier = -1;
    int maxTier = -1;
    int minSeason = 0;
    double minMoney = 0;
    std::vector<std::pair<Stat, double>> minStats;
    std::vector<std::string> flagsAll;
    std::vector<std::string> flagsNone;
    std::vector<Role> roles;  // empty = any specialisation
    bool needsAppointmentFree = false;

    static Requirements fromJson(const Json& j);
};

struct TeamDef {
    std::string name;
    std::string shortName;
    std::string kitPrimary = "#c0392b";
    std::string kitSecondary = "#ffffff";
    int strength = 50;
};

struct BigMatchDef {
    std::string name;
    int importance = 4;
    int week = -1;  // fixed week of the season, -1 = may appear any week
    double chance = 0.15;
    double minScore = 0;  // appointment index needed on top of the importance threshold
    double minForm = 0;   // average mark of the last four matches (finals go to the in-form referee)
    bool derby = false;
    int home = -1;     // fixed teams (indices into the tier's team list), -1 = drawn at random
    int away = -1;
    std::string flag;  // set when the player referees it (e.g. "wc_final_done")
};

struct PromoteRule {
    int minSeasons = 1;  // seasons that must be completed in the tier before promotion
    int minMatches = 4;
    double minAvgMark = 8.0;
    double minTrust = 0;
    double minFairness = 0;
    double minPersonality = 0;
    double minFitness = 0;
};

struct TierDef {
    std::string id;
    std::string name;
    std::string competition;
    std::string venue = "stadium";
    int playersPerSide = 11;
    double pitchLength = 105;
    double pitchWidth = 68;
    double crowd = 0.5;
    int fee = 0;
    std::vector<Role> roles;
    bool hasVar = false;
    bool national = false;
    PromoteRule promote;
    bool canPromote = true;
    int maxSeasons = 0;  // >0: the career ends after this many seasons in the tier (e.g. two World Cups)
    double demoteBelowMark = 0;
    double demoteBelowTrust = 0;
    double fitnessTest = 40;
    double aiAccuracy = 0.8;
    int difficulty = 1;
    int incidentsPerMatch = 7;
    double baseHeat = 20;
    std::vector<TeamDef> teams;
    std::vector<BigMatchDef> bigMatches;
};

struct ActivityDef {
    std::string id;
    std::string name;
    std::string desc;
    Effects effects;
    double cost = 0;
    double injuryRisk = 0;
    Requirements req;
    std::string progressKey;  // repeated sessions accumulate here...
    int progressNeeded = 0;   // ...and grant progressFlag when complete
    std::string progressFlag;
    bool allowedWhileInjured = false;
};

struct EventChoiceDef {
    std::string text;
    std::string result;
    Effects effects;
    Requirements req;
};

struct EventDef {
    std::string id;
    std::string title;
    std::string text;
    bool chainOnly = false;
    bool once = true;
    double chance = 0.05;
    Requirements req;
    std::vector<EventChoiceDef> choices;
};

struct CueDef {
    std::string text;
    double clarity = 0.5;  // minimum clarity needed to notice this cue
};

struct IncidentTemplate {
    std::string id;
    std::string name;
    std::string stage;  // how Unreal stages it: tackle, dive, handball, holding, elbow, offside, goal_check, confrontation
    std::string anim;   // animation tag for the offender
    std::string law;    // short law reference shown in the assessor report
    Verdict truth;
    std::vector<Verdict> acceptable;
    std::vector<Verdict> likelyWrong;  // what a poorly placed referee tends to give
    bool kmi = false;                  // key match incident
    bool inBox = false;
    bool reviewable = false;           // VAR may intervene
    int difficulty = 1;
    int minTier = 0;
    double weight = 1;
    double offsideMargin = 0;  // metres the attacker is beyond the second-last defender (negative = onside)
    double heat = 6;           // match heat added if the call is wrong
    std::vector<Role> roles;
    std::vector<CueDef> cues;
    std::vector<std::string> misleading;
};

struct PressAnswerDef {
    std::string type;  // defend, admit, explain, no_comment, blame
    std::string text;
};

struct PressQuestionDef {
    std::string tag;  // kmi, red, penalty, var, big, heat, general
    std::string text;
    std::vector<PressAnswerDef> answers;
};

struct PressRule {
    Effects correct;
    Effects wrong;
    Effects neutral;
};

struct AppointmentRules {
    std::array<double, kStatCount> weights{};
    double markWeight = 0.15;
    std::vector<double> thresholds;  // index = importance - 1
    int cooldownWeeks = 2;
    double bigMinFairness = 60;
    double bigMinPersonality = 55;
    double rotationChance = 0.2;
};

struct EndingDef {
    std::string id;
    std::string title;
    std::string text;
};

class Content {
public:
    bool loadFromStrings(const std::string& careerJson, const std::string& incidentsJson, const std::string& eventsJson,
                         const std::string& pressJson, const std::string& stringsJson, std::string* error);
    bool loadFromDirectory(const std::string& dir, std::string* error);

    // Localised text lookup; returns the key itself when missing so gaps are visible.
    const std::string& str(const std::string& key) const;
    std::string fmt(const std::string& key, const std::vector<std::pair<std::string, std::string>>& vars) const;
    static std::string substitute(std::string text, const std::vector<std::pair<std::string, std::string>>& vars);

    const IncidentTemplate* incident(const std::string& id) const;
    const EventDef* event(const std::string& id) const;
    const ActivityDef* activity(const std::string& id) const;
    const EndingDef* ending(const std::string& id) const;
    std::string verdictLabel(const Verdict& v, Role role) const;

    int startAge = 22;
    int retireAge = 45;
    int weeksPerSeason = 8;
    int slotsPerWeek = 3;
    Stats startStats;
    std::vector<TierDef> tiers;
    std::vector<ActivityDef> activities;
    AppointmentRules appointment;
    std::map<std::string, PressRule> pressRules;
    std::vector<IncidentTemplate> incidents;
    std::vector<EventDef> events;
    std::vector<PressQuestionDef> press;
    std::vector<EndingDef> endings;
    std::map<std::string, std::string> strings;
};

std::vector<Role> parseRoles(const Json& j);

}  // namespace refcore
