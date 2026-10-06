#include "RefCore/RcContent.h"

#include <fstream>
#include <sstream>

namespace refcore {

namespace {

std::vector<std::string> contentStringList(const Json& j) {
    std::vector<std::string> out;
    if (j.isString()) out.push_back(j.asString());
    for (const Json& item : j.items())
        if (item.isString()) out.push_back(item.asString());
    return out;
}

void contentParseStatBlock(const Json& j, std::array<double, kStatCount>& out) {
    for (const auto& kv : j.members()) {
        Stat s;
        if (parseStat(kv.first, s)) out[static_cast<size_t>(s)] = kv.second.asNumber();
    }
}

bool contentReadFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

Verdict contentVerdict(const Json& j, const std::string& def) {
    Verdict v;
    parseVerdict(j.asString(def), v);
    return v;
}

std::vector<Verdict> contentVerdicts(const Json& j) {
    std::vector<Verdict> out;
    for (const std::string& code : contentStringList(j)) {
        Verdict v;
        if (parseVerdict(code, v)) out.push_back(v);
    }
    return out;
}

TierDef contentParseTier(const Json& t) {
    TierDef d;
    d.id = t["id"].asString();
    d.name = t["name"].asString();
    d.competition = t["competition"].asString(d.name);
    d.venue = t["venue"].asString("stadium");
    d.playersPerSide = t["playersPerSide"].asInt(11);
    d.pitchLength = t["pitch"].at(0).asNumber(105);
    d.pitchWidth = t["pitch"].at(1).asNumber(68);
    d.crowd = t["crowd"].asNumber(0.5);
    d.fee = t["fee"].asInt(0);
    d.roles = parseRoles(t["roles"]);
    if (d.roles.empty()) d.roles = {Role::Center, Role::Assistant};
    d.hasVar = t["var"].asBool(false);
    d.national = t["national"].asBool(false);
    const Json& p = t["promote"];
    d.canPromote = !p.isNull();
    d.promote.minSeasons = p["minSeasons"].asInt(1);
    d.promote.minMatches = p["minMatches"].asInt(4);
    d.promote.minAvgMark = p["minAvgMark"].asNumber(8.0);
    d.promote.minTrust = p["minTrust"].asNumber(0);
    d.promote.minFairness = p["minFairness"].asNumber(0);
    d.promote.minPersonality = p["minPersonality"].asNumber(0);
    d.promote.minFitness = p["minFitness"].asNumber(0);
    d.maxSeasons = t["maxSeasons"].asInt(0);
    d.demoteBelowMark = t["demoteBelowMark"].asNumber(0);
    d.demoteBelowTrust = t["demoteBelowTrust"].asNumber(0);
    d.fitnessTest = t["fitnessTest"].asNumber(40);
    d.aiAccuracy = t["aiAccuracy"].asNumber(0.8);
    d.difficulty = t["difficulty"].asInt(1);
    d.incidentsPerMatch = t["incidentsPerMatch"].asInt(7);
    d.baseHeat = t["baseHeat"].asNumber(20);
    for (const Json& tm : t["teams"].items()) {
        TeamDef team;
        team.name = tm["name"].asString();
        team.shortName = tm["short"].asString(team.name);
        team.kitPrimary = tm["kit"].at(0).asString("#c0392b");
        team.kitSecondary = tm["kit"].at(1).asString("#ffffff");
        team.strength = tm["strength"].asInt(50);
        d.teams.push_back(team);
    }
    for (const Json& bm : t["bigMatches"].items()) {
        BigMatchDef b;
        b.name = bm["name"].asString();
        b.importance = bm["importance"].asInt(4);
        b.week = bm["week"].asInt(-1);
        b.chance = bm["chance"].asNumber(0.15);
        b.minScore = bm["minScore"].asNumber(0);
        b.minForm = bm["minForm"].asNumber(0);
        b.derby = bm["derby"].asBool(false);
        b.flag = bm["flag"].asString();
        b.home = bm["teams"].at(0).asInt(-1);
        b.away = bm["teams"].at(1).asInt(-1);
        d.bigMatches.push_back(b);
    }
    return d;
}

IncidentTemplate contentParseIncident(const Json& j) {
    IncidentTemplate t;
    t.id = j["id"].asString();
    t.name = j["name"].asString();
    t.stage = j["stage"].asString("tackle");
    t.anim = j["anim"].asString(t.stage);
    t.law = j["law"].asString();
    t.truth = contentVerdict(j["truth"], "play");
    t.acceptable = contentVerdicts(j["acceptable"]);
    t.likelyWrong = contentVerdicts(j["likelyWrong"]);
    t.kmi = j["kmi"].asBool(false);
    t.inBox = j["inBox"].asBool(false);
    t.reviewable = j["reviewable"].asBool(t.kmi);
    t.difficulty = j["difficulty"].asInt(1);
    t.minTier = j["minTier"].asInt(0);
    t.weight = j["weight"].asNumber(1);
    t.offsideMargin = j["offsideMargin"].asNumber(0);
    t.heat = j["heat"].asNumber(t.kmi ? 14 : 6);
    t.roles = parseRoles(j["roles"]);
    for (const Json& c : j["cues"].items()) t.cues.push_back({c["text"].asString(), c["clarity"].asNumber(0.5)});
    t.misleading = contentStringList(j["misleading"]);
    return t;
}

}  // namespace

std::vector<Role> parseRoles(const Json& j) {
    std::vector<Role> out;
    for (const std::string& s : contentStringList(j)) {
        Role r;
        if (parseRole(s, r)) out.push_back(r);
    }
    return out;
}

Effects Effects::fromJson(const Json& j) {
    Effects e;
    for (const auto& kv : j.members()) {
        Stat s;
        if (parseStat(kv.first, s)) e.stats[static_cast<size_t>(s)] = kv.second.asNumber();
    }
    e.money = j["money"].asNumber(0);
    e.roleSkill = j["roleSkill"].asNumber(0);
    e.suspicion = j["suspicion"].asNumber(0);
    e.setFlags = contentStringList(j["setFlags"]);
    e.clearFlags = contentStringList(j["clearFlags"]);
    e.suspendWeeks = j["suspendWeeks"].asInt(0);
    e.injuryWeeks = j["injuryWeeks"].asInt(0);
    e.restWeeks = j["restWeeks"].asInt(0);
    e.ending = j["ending"].asString();
    e.fix = j["fix"].asString();
    e.fixReward = j["fixReward"].asNumber(0);
    const Json& next = j["next"];
    if (next.isObject()) {
        e.scheduleEvent = next["event"].asString();
        e.scheduleInWeeks = next["inWeeks"].asInt(1);
    }
    return e;
}

Requirements Requirements::fromJson(const Json& j) {
    Requirements r;
    r.minTier = j["minTier"].asInt(-1);
    r.maxTier = j["maxTier"].asInt(-1);
    r.minSeason = j["minSeason"].asInt(0);
    r.minMoney = j["minMoney"].asNumber(0);
    for (const auto& kv : j["minStats"].members()) {
        Stat s;
        if (parseStat(kv.first, s)) r.minStats.emplace_back(s, kv.second.asNumber());
    }
    r.flagsAll = contentStringList(j["flagsAll"]);
    r.flagsNone = contentStringList(j["flagsNone"]);
    r.roles = parseRoles(j["roles"]);
    r.needsAppointmentFree = j["appointmentFree"].asBool(false);
    return r;
}

bool Content::loadFromStrings(const std::string& careerJson, const std::string& incidentsJson,
                              const std::string& eventsJson, const std::string& pressJson,
                              const std::string& stringsJson, std::string* error) {
    Json career, inc, ev, pr, st;
    std::string err;
    if (!Json::parse(careerJson, career, &err)) { if (error) *error = "career.json: " + err; return false; }
    if (!Json::parse(incidentsJson, inc, &err)) { if (error) *error = "incidents.json: " + err; return false; }
    if (!Json::parse(eventsJson, ev, &err)) { if (error) *error = "events.json: " + err; return false; }
    if (!Json::parse(pressJson, pr, &err)) { if (error) *error = "press.json: " + err; return false; }
    if (!Json::parse(stringsJson, st, &err)) { if (error) *error = "strings.json: " + err; return false; }

    startAge = career["startAge"].asInt(22);
    retireAge = career["retireAge"].asInt(45);
    weeksPerSeason = career["weeksPerSeason"].asInt(8);
    slotsPerWeek = career["slotsPerWeek"].asInt(3);
    contentParseStatBlock(career["startStats"], startStats.v);

    tiers.clear();
    for (const Json& t : career["tiers"].items()) tiers.push_back(contentParseTier(t));

    activities.clear();
    for (const Json& a : career["activities"].items()) {
        ActivityDef d;
        d.id = a["id"].asString();
        d.name = a["name"].asString();
        d.desc = a["desc"].asString();
        d.effects = Effects::fromJson(a["effects"]);
        d.cost = a["cost"].asNumber(0);
        d.injuryRisk = a["injuryRisk"].asNumber(0);
        d.req = Requirements::fromJson(a["requires"]);
        d.progressKey = a["progress"]["key"].asString();
        d.progressNeeded = a["progress"]["needed"].asInt(0);
        d.progressFlag = a["progress"]["flag"].asString();
        d.allowedWhileInjured = a["whileInjured"].asBool(false);
        activities.push_back(d);
    }

    const Json& ap = career["appointment"];
    contentParseStatBlock(ap["weights"], appointment.weights);
    appointment.markWeight = ap["weights"]["mark"].asNumber(0.15);
    appointment.thresholds.clear();
    for (const Json& x : ap["thresholds"].items()) appointment.thresholds.push_back(x.asNumber());
    if (appointment.thresholds.empty()) appointment.thresholds = {0, 40, 52, 63, 74};
    appointment.cooldownWeeks = ap["cooldownWeeks"].asInt(2);
    appointment.bigMinFairness = ap["bigMatchMinFairness"].asNumber(60);
    appointment.bigMinPersonality = ap["bigMatchMinPersonality"].asNumber(55);
    appointment.rotationChance = ap["rotationChance"].asNumber(0.2);

    pressRules.clear();
    for (const auto& kv : career["pressRules"].members()) {
        PressRule r;
        r.correct = Effects::fromJson(kv.second["correct"]);
        r.wrong = Effects::fromJson(kv.second["wrong"]);
        r.neutral = Effects::fromJson(kv.second["neutral"]);
        pressRules[kv.first] = r;
    }

    endings.clear();
    for (const Json& e : career["endings"].items())
        endings.push_back({e["id"].asString(), e["title"].asString(), e["text"].asString()});

    incidents.clear();
    for (const Json& j : inc["incidents"].items()) incidents.push_back(contentParseIncident(j));

    events.clear();
    for (const Json& e : ev["events"].items()) {
        EventDef d;
        d.id = e["id"].asString();
        d.title = e["title"].asString();
        d.text = e["text"].asString();
        d.chainOnly = e["chainOnly"].asBool(false);
        d.once = e["once"].asBool(true);
        d.chance = e["chance"].asNumber(0.05);
        d.req = Requirements::fromJson(e["requires"]);
        for (const Json& c : e["choices"].items()) {
            EventChoiceDef ch;
            ch.text = c["text"].asString();
            ch.result = c["result"].asString();
            ch.effects = Effects::fromJson(c["effects"]);
            ch.req = Requirements::fromJson(c["requires"]);
            d.choices.push_back(ch);
        }
        events.push_back(d);
    }

    press.clear();
    for (const Json& q : pr["questions"].items()) {
        PressQuestionDef d;
        d.tag = q["tag"].asString("general");
        d.text = q["text"].asString();
        for (const Json& a : q["answers"].items()) d.answers.push_back({a["type"].asString(), a["text"].asString()});
        press.push_back(d);
    }

    strings.clear();
    for (const auto& kv : st.members())
        if (kv.second.isString()) strings[kv.first] = kv.second.asString();

    if (tiers.empty()) { if (error) *error = "career.json has no tiers"; return false; }
    for (const TierDef& t : tiers) {
        if (t.teams.size() < 2) { if (error) *error = "tier " + t.id + " needs at least two teams"; return false; }
    }
    if (incidents.empty()) { if (error) *error = "incidents.json has no incidents"; return false; }
    return true;
}

bool Content::loadFromDirectory(const std::string& dir, std::string* error) {
    const char* names[] = {"career.json", "incidents.json", "events.json", "press.json", "strings.json"};
    std::string texts[5];
    for (int i = 0; i < 5; ++i) {
        if (!contentReadFile(dir + "/" + names[i], texts[i])) {
            if (error) *error = std::string("cannot read ") + dir + "/" + names[i];
            return false;
        }
    }
    return loadFromStrings(texts[0], texts[1], texts[2], texts[3], texts[4], error);
}

const std::string& Content::str(const std::string& key) const {
    const auto it = strings.find(key);
    return it != strings.end() ? it->second : key;
}

std::string Content::substitute(std::string text, const std::vector<std::pair<std::string, std::string>>& vars) {
    for (const auto& kv : vars) {
        const std::string token = "{" + kv.first + "}";
        size_t pos = 0;
        while ((pos = text.find(token, pos)) != std::string::npos) {
            text.replace(pos, token.size(), kv.second);
            pos += kv.second.size();
        }
    }
    return text;
}

std::string Content::fmt(const std::string& key, const std::vector<std::pair<std::string, std::string>>& vars) const {
    return substitute(str(key), vars);
}

const IncidentTemplate* Content::incident(const std::string& id) const {
    for (const IncidentTemplate& t : incidents)
        if (t.id == id) return &t;
    return nullptr;
}

const EventDef* Content::event(const std::string& id) const {
    for (const EventDef& e : events)
        if (e.id == id) return &e;
    return nullptr;
}

const ActivityDef* Content::activity(const std::string& id) const {
    for (const ActivityDef& a : activities)
        if (a.id == id) return &a;
    return nullptr;
}

const EndingDef* Content::ending(const std::string& id) const {
    for (const EndingDef& e : endings)
        if (e.id == id) return &e;
    return nullptr;
}

std::string Content::verdictLabel(const Verdict& v, Role role) const {
    const std::string code = verdictCode(v);
    const std::string roleKey = std::string("opt.") + roleId(role) + "." + code;
    if (strings.count(roleKey)) return strings.at(roleKey);
    return str("verdict." + code);
}

}  // namespace refcore
