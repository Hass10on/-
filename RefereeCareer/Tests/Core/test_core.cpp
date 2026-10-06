#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "RefCore/RcCareer.h"
#include "RefCore/RcJson.h"
#include "SimBot.h"

using namespace refcore;

namespace {

int gFailures = 0;
int gChecks = 0;

void expectTrue(bool cond, const char* what, int line) {
    ++gChecks;
    if (!cond) {
        ++gFailures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}
#define EXPECT(c) expectTrue((c), #c, __LINE__)

Content& content() {
    static Content c;
    static bool loaded = false;
    if (!loaded) {
        std::string err;
        loaded = c.loadFromDirectory(REFCORE_DATA_DIR, &err);
        if (!loaded) std::printf("content error: %s\n", err.c_str());
    }
    return c;
}

void testJson() {
    Json j;
    std::string err;
    EXPECT(Json::parse(R"({"a":1,"b":[true,false,null],"c":"حكم \"دولي\"","d":"م\n","e":-2.5e1})", j, &err));
    EXPECT(j["a"].asInt() == 1);
    EXPECT(j["b"].size() == 3 && j["b"].at(0).asBool() && j["b"].at(2).isNull());
    EXPECT(j["c"].asString() == "حكم \"دولي\"");
    EXPECT(j["d"].asString() == "م\n");
    EXPECT(std::fabs(j["e"].asNumber() + 25.0) < 1e-9);
    EXPECT(j["missing"]["deeper"].isNull());
    Json back;
    EXPECT(Json::parse(j.dump(2), back, &err));
    EXPECT(back.dump() == j.dump());
    EXPECT(!Json::parse("{\"a\":}", back, &err));
    EXPECT(!Json::parse("[1,2", back, &err));
}

void testVerdicts() {
    for (const char* code : {"play", "adv", "fk", "fk_y", "fk_r", "pen", "pen_y", "pen_r", "dive_y", "offside", "goal", "no_goal", "conf_y2"}) {
        Verdict v;
        EXPECT(parseVerdict(code, v));
        EXPECT(verdictCode(v) == code);
    }
    Verdict pen, dive;
    parseVerdict("pen", pen);
    parseVerdict("dive_y", dive);
    EXPECT(benefitingSide(pen, 0) == 1);   // penalty against side 0 helps side 1
    EXPECT(benefitingSide(dive, 0) == 0);  // booking the diver helps the accused side
}

void testContent() {
    const Content& c = content();
    EXPECT(c.tiers.size() == 7);
    EXPECT(!c.incidents.empty());
    std::set<std::string> eventIds;
    for (const EventDef& e : c.events) eventIds.insert(e.id);
    for (const EventDef& e : c.events) {
        EXPECT(!e.choices.empty());
        for (const EventChoiceDef& ch : e.choices) {
            if (!ch.effects.scheduleEvent.empty()) EXPECT(eventIds.count(ch.effects.scheduleEvent) == 1);
            if (!ch.effects.ending.empty()) EXPECT(c.ending(ch.effects.ending) != nullptr);
        }
    }
    EXPECT(eventIds.count("integrity_exposed") == 1);
    EXPECT(eventIds.count("fixer_angry") == 1);
    for (const char* ending : {"legend", "retired", "retired_international", "banned", "dismissed", "burnout", "confessed", "pundit"})
        EXPECT(c.ending(ending) != nullptr);
    // Every key the rules ask for exists in strings.json.
    for (const char* key : {"cue.offside.clear_off", "cue.offside.slight_off", "cue.offside.level", "cue.offside.behind", "note.blocked",
                            "note.far", "note.partial", "note.clear", "note.var_onfield", "fb.good", "fb.bad", "fb.var_review",
                            "grade.excellent", "grade.poor", "note.wrong", "note.kmi_wrong", "note.debatable", "note.var_corrected",
                            "note.positioning", "note.ar_line", "note.control", "note.fitness", "note.bias", "note.clean",
                            "opt.conf_none", "opt.conf_y", "opt.conf_y2", "opt.conf_r", "opt.var.keep", "opt.var.recommend",
                            "app.reason", "app.blocked.score", "app.blocked.stat", "app.blocked.cooldown", "app.rotation", "app.none",
                            "season.intro", "season.fitness_pass", "season.fitness_fail", "review.promoted", "review.demoted",
                            "review.stay", "review.top", "ending.summary", "act.complete.var_certified", "stat.money"})
        EXPECT(c.strings.count(key) == 1);
    for (const char* who : {"player", "captain", "coach"})
        for (const char* mood : {".protest", ".angry", ".abuse"}) EXPECT(c.strings.count(std::string("dissent.") + who + mood) == 1);
    for (const char* r : {"ignore", "calm", "warn", "yellow", "sendoff"})
        for (const char* o : {".ok", ".fail"}) EXPECT(c.strings.count(std::string("dissent.res.") + r + o) == 1);
    for (const auto& kv : c.pressRules)
        for (const char* o : {".correct", ".wrong"}) EXPECT(c.strings.count("press.res." + kv.first + o) == 1);
    for (const PressQuestionDef& q : c.press)
        for (const PressAnswerDef& a : q.answers) EXPECT(c.pressRules.count(a.type) == 1);
    // The correct call is always one of the buttons for every role that can face the incident.
    for (const IncidentTemplate& t : c.incidents) {
        if (t.stage == "confrontation") continue;
        for (Role r : t.roles) {
            MatchSetup s;
            s.role = r;
            s.tier = 3;
            s.stats = c.startStats;
            MatchSession m(c, s);
            std::vector<DecisionOption> opts;
            // Build a one-off plan entry by scanning until this template shows up is overkill; check codes directly.
            const bool lineStage = t.stage == "offside" || t.stage == "goal_check";
            std::vector<std::string> codes = t.stage == "goal_check" ? std::vector<std::string>{"goal", "no_goal"}
                                             : lineStage ? std::vector<std::string>{"offside", "play"}
                                             : r == Role::Assistant ? (t.inBox ? std::vector<std::string>{"play", "pen", "pen_y", "pen_r"}
                                                                               : std::vector<std::string>{"play", "fk", "fk_y", "fk_r"})
                                             : (t.inBox ? std::vector<std::string>{"play", "pen", "pen_y", "pen_r", "dive_y"}
                                                        : std::vector<std::string>{"play", "adv", "fk", "fk_y", "fk_r", "dive_y"});
            if (r == Role::Var) codes = t.stage == "goal_check" ? std::vector<std::string>{"goal", "no_goal"}
                                                                : std::vector<std::string>{"play", "pen", "pen_y", "pen_r", "dive_y", "fk", "fk_y", "fk_r"};
            bool found = false;
            for (const std::string& code : codes) {
                Verdict v;
                parseVerdict(code, v);
                if (v == t.truth) found = true;
            }
            if (!found) std::printf("  incident %s has no button for its truth as %s\n", t.id.c_str(), roleId(r));
            EXPECT(found);
        }
    }
}

MatchSetup setupFor(Role role, int tier, uint64_t seed) {
    MatchSetup s;
    s.role = role;
    s.tier = tier;
    s.seed = seed;
    s.stats = content().startStats;
    s.stats.set(Stat::Personality, 70);
    s.stats.set(Stat::Laws, 70);
    s.roleSkill = 40;
    return s;
}

void testPerception() {
    MatchSession m(content(), setupFor(Role::Center, 2, 5));
    EXPECT(!m.plan().empty());
    ViewSample nearV, farV, blockedV;
    nearV.distance = 12; nearV.angleQuality = 0.9;
    farV.distance = 40; farV.angleQuality = 0.9;
    blockedV = nearV; blockedV.occlusion = 0.9;
    const Perception a = m.perceive(0, nearV), b = m.perceive(0, farV), c = m.perceive(0, blockedV);
    EXPECT(a.clarity > b.clarity);
    EXPECT(a.clarity > c.clarity);
    EXPECT(!a.note.empty());
    EXPECT(m.perceive(0, nearV).cues == a.cues);  // stable when asked again
}

void testPerfectAndPoorMatch() {
    for (Role role : {Role::Center, Role::Assistant, Role::Var}) {
        for (int pass = 0; pass < 2; ++pass) {
            MatchSession m(content(), setupFor(role, 3, 42 + static_cast<uint64_t>(role)));
            ViewSample v;
            v.distance = 14; v.angleQuality = 0.9; v.alignment = 0.4; v.replay = role == Role::Var;
            for (double minute = 0; minute <= 90; minute += 0.5) {
                m.advance(minute);
                m.addPositionSample(0.9);
                int idx;
                while ((idx = m.dueIncident(minute)) >= 0) {
                    std::string key = simbot::truthKey(m, idx);
                    if (pass == 1 && m.plan()[static_cast<size_t>(idx)].tpl->kmi) {
                        for (const DecisionOption& o : m.options(idx))
                            if (o.key != key && !(o.verdict == m.plan()[static_cast<size_t>(idx)].tpl->truth)) { key = o.key; break; }
                    }
                    const DecisionResult r = m.decide(idx, key, 1.0, v);
                    if (r.varReviewOffered) m.onFieldReview(idx, key);
                    if (m.dissent().active) m.respondDissent(simbot::idealDissent(m.dissent()));
                }
            }
            m.setFitnessData(0.9, 0.05);
            const MatchReport rep = m.finish();
            if (pass == 0) {
                EXPECT(rep.kmiErrors == 0);
                EXPECT(rep.mark >= 8.2);
                EXPECT(rep.decisionScore > 0.95);
            } else if (rep.kmiTotal > 0) {
                EXPECT(rep.kmiErrors > 0);
                EXPECT(rep.mark <= 7.9);
                EXPECT(rep.statDeltas[static_cast<size_t>(Stat::Trust)] < 0);
            }
        }
    }
}

void testVarProtocol() {
    // Find a VAR plan entry whose on-field call is already right: intervening must cost credit.
    for (uint64_t seed = 1; seed < 200; ++seed) {
        MatchSession m(content(), setupFor(Role::Var, 3, seed));
        for (const PlannedIncident& p : m.plan()) {
            if (p.onField != p.tpl->truth) continue;
            std::string other;
            for (const DecisionOption& o : m.options(p.index))
                if (o.intervene) { other = o.key; break; }
            if (other.empty()) continue;
            ViewSample v;
            v.replay = true;
            m.advance(p.minute);
            m.decide(p.index, other, 5, v);
            const MatchReport rep = m.finish();
            EXPECT(!rep.incidents.empty() && rep.incidents.front().credit < 0.6);
            return;
        }
    }
    EXPECT(false && "no VAR scenario found");
}

void testCareerFlowAndSave() {
    Career c(content());
    c.startNew("حكم الاختبار", Role::Center, 77);
    EXPECT(c.phase() == Phase::SeasonStart);
    c.confirmSeasonStart();
    EXPECT(c.phase() == Phase::Planning);
    const Json saved = c.toJson();
    Career d(content());
    std::string err;
    EXPECT(d.fromJson(saved, &err));
    EXPECT(d.toJson().dump() == saved.dump());

    // Two careers with the same seed and choices stay identical (deterministic saves/replays).
    Career e(content()), f(content());
    e.startNew("A", Role::Assistant, 9);
    f.startNew("A", Role::Assistant, 9);
    Rng r1(3), r2(3);
    for (int i = 0; i < 60 && e.phase() != Phase::Over; ++i) {
        switch (e.phase()) {
            case Phase::SeasonStart: e.confirmSeasonStart(); f.confirmSeasonStart(); break;
            case Phase::Planning: e.commitWeek({"fitness", "laws", "video"}, Role::Assistant); f.commitWeek({"fitness", "laws", "video"}, Role::Assistant); break;
            case Phase::Event: e.chooseEvent(0); f.chooseEvent(0); break;
            case Phase::Appointment: e.acceptAppointment(); f.acceptAppointment(); break;
            case Phase::Match:
                e.completeMatch(simbot::playMatch(content(), e.matchSetup(), 0.8, r1));
                f.completeMatch(simbot::playMatch(content(), f.matchSetup(), 0.8, r2));
                break;
            case Phase::PostMatch: e.continueAfterReport(); f.continueAfterReport(); break;
            case Phase::Press: e.answerPress(0); f.answerPress(0); break;
            case Phase::SeasonReview: e.continueAfterReview(); f.continueAfterReview(); break;
            case Phase::Over: break;
        }
    }
    EXPECT(e.toJson().dump() == f.toJson().dump());
    EXPECT(!e.history().empty());
}

void testFixingChain() {
    // Accepting a fix marks the next appointed match; refusing to deliver schedules the angry fixer.
    for (uint64_t seed = 1; seed < 400; ++seed) {
        Career c(content());
        c.startNew("X", Role::Center, seed);
        Rng rng(seed);
        bool sawFix = false;
        for (int step = 0; step < 4000 && c.phase() != Phase::Over && c.season() < 6; ++step) {
            switch (c.phase()) {
                case Phase::SeasonStart: c.confirmSeasonStart(); break;
                case Phase::Planning: c.commitWeek({"fitness", "seminar", "video"}, Role::Center); break;
                case Phase::Event:
                    if (c.currentEvent()->id == "fixing_approach") { c.chooseEvent(2); sawFix = true; }
                    else c.chooseEvent(0);
                    break;
                case Phase::Appointment:
                    if (sawFix && c.appointment().appointed) {
                        EXPECT(c.matchSetup().fixFavorSide == 0);
                        return;
                    }
                    c.acceptAppointment();
                    break;
                case Phase::Match: c.completeMatch(simbot::playMatch(content(), c.matchSetup(), 0.8, rng)); break;
                case Phase::PostMatch: c.continueAfterReport(); break;
                case Phase::Press: c.answerPress(0); break;
                case Phase::SeasonReview: c.continueAfterReview(); break;
                case Phase::Over: break;
            }
        }
    }
    EXPECT(false && "fixing approach never happened");
}

void testAppointmentsFollowReputation() {
    Career low(content()), high(content());
    low.startNew("L", Role::Center, 5);
    high.startNew("H", Role::Center, 5);
    Json j = high.toJson();
    Json stats = Json::makeObject();
    for (int i = 0; i < kStatCount; ++i) stats.set(statId(static_cast<Stat>(i)), 95.0);
    j.set("stats", stats);
    j.set("tier", 3);
    j.set("week", 4);  // derby week in the premier league
    j.set("phase", "planning");
    std::string err;
    EXPECT(high.fromJson(j, &err));
    EXPECT(high.appointmentIndex() > low.appointmentIndex());
    high.commitWeek({"rest"}, Role::Center);
    while (high.phase() == Phase::Event) high.chooseEvent(0);
    EXPECT(high.phase() == Phase::Appointment);
    EXPECT(high.appointment().appointed);
    EXPECT(high.appointment().fixture.importance == 5);
}

}  // namespace

int main() {
    testJson();
    testVerdicts();
    testContent();
    testPerception();
    testPerfectAndPoorMatch();
    testVarProtocol();
    testCareerFlowAndSave();
    testFixingChain();
    testAppointmentsFollowReputation();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
