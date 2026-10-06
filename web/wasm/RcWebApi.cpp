// JSON command interface over RefCore for the browser build.
// Compiled with the WASI SDK to WebAssembly, then to plain JavaScript with wasm2js (see web/tools/build_core.sh),
// so the web game runs exactly the same rules as the Unreal module and the C++ tests.
//
// Protocol: rc_call(utf8 JSON {"cmd": "...", ...}) -> utf8 JSON {"ok": true, ...} | {"ok": false, "error": "..."}
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "RefCore/RcCareer.h"
#include "RefCore/RcContent.h"
#include "RefCore/RcJson.h"
#include "RefCore/RcMatch.h"

using namespace refcore;

namespace {

Content gContent;
bool gLoaded = false;
std::unique_ptr<Career> gCareer;
std::unique_ptr<MatchSession> gMatch;
std::string gOut;

Json ok() {
    Json j = Json::makeObject();
    j.set("ok", true);
    return j;
}

Json fail(const std::string& msg) {
    Json j = Json::makeObject();
    j.set("ok", false);
    j.set("error", msg);
    return j;
}

Json strList(const std::vector<std::string>& v) {
    Json a = Json::makeArray();
    for (const std::string& s : v) a.push(s);
    return a;
}

const char* restartName(Restart r) {
    switch (r) {
        case Restart::PlayOn: return "play";
        case Restart::Advantage: return "adv";
        case Restart::FreeKick: return "fk";
        case Restart::Penalty: return "pen";
        case Restart::Offside: return "offside";
        case Restart::Goal: return "goal";
        case Restart::NoGoal: return "no_goal";
    }
    return "play";
}

const char* cardName(Card c) { return c == Card::Red ? "red" : c == Card::Yellow ? "yellow" : "none"; }
const char* cardToName(CardTo c) { return c == CardTo::Victim ? "victim" : c == CardTo::Both ? "both" : "offender"; }

Json verdictJson(const Verdict& v) {
    Json j = Json::makeObject();
    j.set("code", verdictCode(v));
    j.set("restart", restartName(v.restart));
    j.set("card", cardName(v.card));
    j.set("cardTo", cardToName(v.cardTo));
    j.set("label", gContent.verdictLabel(v, Role::Center));
    return j;
}

Json statsJson(const Stats& s) {
    Json j = Json::makeObject();
    for (int i = 0; i < kStatCount; ++i) j.set(statId(static_cast<Stat>(i)), s.v[static_cast<size_t>(i)]);
    return j;
}

Json teamJson(const TeamDef& t) {
    Json j = Json::makeObject();
    j.set("name", t.name);
    j.set("short", t.shortName);
    Json kit = Json::makeArray();
    kit.push(t.kitPrimary);
    kit.push(t.kitSecondary);
    j.set("kit", kit);
    j.set("strength", t.strength);
    return j;
}

Json tierJson(const TierDef& t) {
    Json j = Json::makeObject();
    j.set("id", t.id);
    j.set("name", t.name);
    j.set("competition", t.competition);
    j.set("venue", t.venue);
    j.set("playersPerSide", t.playersPerSide);
    Json pitch = Json::makeArray();
    pitch.push(t.pitchLength);
    pitch.push(t.pitchWidth);
    j.set("pitch", pitch);
    j.set("crowd", t.crowd);
    j.set("var", t.hasVar);
    j.set("national", t.national);
    return j;
}

Json reportJson(const MatchReport& r) {
    Json j = Json::makeObject();
    j.set("mark", r.mark);
    j.set("grade", r.grade);
    j.set("decisions", r.decisionScore);
    j.set("positioning", r.positioningScore);
    j.set("control", r.controlScore);
    j.set("fitness", r.fitnessScore);
    j.set("personality", r.personalityScore);
    j.set("kmiTotal", r.kmiTotal);
    j.set("kmiCorrect", r.kmiCorrect);
    j.set("kmiErrors", r.kmiErrors);
    j.set("confrontations", r.confrontations);
    j.set("avgHeat", r.avgHeat);
    j.set("yellow", r.yellowCards);
    j.set("red", r.redCards);
    j.set("biasTowards", r.biasTowards);
    Json score = Json::makeArray();
    score.push(r.score[0]);
    score.push(r.score[1]);
    j.set("score", score);
    Json deltas = Json::makeObject();
    for (int i = 0; i < kStatCount; ++i) deltas.set(statId(static_cast<Stat>(i)), r.statDeltas[static_cast<size_t>(i)]);
    j.set("deltas", deltas);
    Json inc = Json::makeArray();
    for (const IncidentLog& l : r.incidents) {
        Json o = Json::makeObject();
        o.set("minute", l.minute);
        o.set("id", l.templateId);
        o.set("name", l.name);
        o.set("chosen", l.chosenLabel);
        o.set("truth", l.truthLabel);
        o.set("law", l.law);
        o.set("correct", l.correct);
        o.set("acceptable", l.acceptable);
        o.set("kmi", l.kmi);
        o.set("correctedByVar", l.correctedByVar);
        o.set("clarity", l.clarity);
        o.set("credit", l.credit);
        inc.push(o);
    }
    j.set("incidents", inc);
    j.set("notes", strList(r.assessorNotes));
    return j;
}

Json careerState() {
    const Career& c = *gCareer;
    Json j = Json::makeObject();
    j.set("ok", true);
    j.set("phase", phaseId(c.phase()));
    j.set("name", c.name());
    j.set("age", c.age());
    j.set("season", c.season());
    j.set("week", c.week());
    j.set("weeksPerSeason", gContent.weeksPerSeason);
    j.set("slots", c.slots());
    j.set("tier", c.tierIndex());
    j.set("tierInfo", tierJson(c.tier()));
    j.set("specialisation", roleId(c.specialisation()));
    j.set("preferred", roleId(c.preferredRole()));
    j.set("money", c.money());
    j.set("injured", c.injuredWeeks());
    j.set("suspended", c.suspendedWeeks());
    j.set("stats", statsJson(c.stats()));
    j.set("appointmentIndex", c.appointmentIndex());
    Json rs = Json::makeObject();
    Json can = Json::makeObject();
    for (int i = 0; i < kRoleCount; ++i) {
        const Role r = static_cast<Role>(i);
        rs.set(roleId(r), c.roleSkill(r));
        std::string why;
        Json w = Json::makeObject();
        w.set("ok", c.canWorkAs(r, &why));
        w.set("why", why);
        can.set(roleId(r), w);
    }
    j.set("roleSkill", rs);
    j.set("canWork", can);
    Json tiers = Json::makeArray();
    for (const TierDef& t : gContent.tiers) tiers.push(tierJson(t));
    j.set("tiers", tiers);
    j.set("news", strList(c.news()));
    j.set("seasonIntro", c.seasonIntro());
    j.set("fixAccepted", c.hasFlag("fix_accepted"));
    j.set("varCertified", c.hasFlag("var_certified"));

    Json acts = Json::makeArray();
    for (const ActivityStatus& a : c.activities()) {
        Json o = Json::makeObject();
        o.set("id", a.def->id);
        o.set("name", a.def->name);
        o.set("desc", a.def->desc);
        o.set("cost", a.def->cost);
        o.set("available", a.available);
        o.set("reason", a.reason);
        acts.push(o);
    }
    j.set("activities", acts);

    if (const EventDef* e = c.currentEvent()) {
        Json ev = Json::makeObject();
        ev.set("id", e->id);
        ev.set("title", e->title);
        ev.set("text", Content::substitute(e->text, {{"name", c.name()}}));
        Json ch = Json::makeArray();
        const std::vector<EventChoiceStatus> st = c.eventChoices();
        for (size_t i = 0; i < e->choices.size(); ++i) {
            Json o = Json::makeObject();
            o.set("text", e->choices[i].text);
            o.set("available", st[i].available);
            o.set("reason", st[i].reason);
            ch.push(o);
        }
        ev.set("choices", ch);
        j.set("event", ev);
    }

    if (c.phase() == Phase::Appointment || c.phase() == Phase::Match) {
        const Appointment& a = c.appointment();
        Json ap = Json::makeObject();
        ap.set("appointed", a.appointed);
        ap.set("role", roleId(a.role));
        ap.set("importance", a.fixture.importance);
        ap.set("derby", a.fixture.derby);
        ap.set("title", a.fixture.title);
        ap.set("fixture", a.appointed ? c.matchSetup().fixtureName : std::string());
        ap.set("score", a.score);
        ap.set("reason", a.reason);
        ap.set("blocked", a.blocked);
        ap.set("others", strList(a.otherFixtures));
        if (a.appointed) {
            const TierDef& t = c.tier();
            ap.set("home", teamJson(t.teams[static_cast<size_t>(a.fixture.home)]));
            ap.set("away", teamJson(t.teams[static_cast<size_t>(a.fixture.away)]));
        }
        j.set("appointment", ap);
    }

    if (c.phase() == Phase::PostMatch || c.phase() == Phase::Press) {
        j.set("report", reportJson(c.lastReport()));
        j.set("matchNews", strList(c.lastMatchNews()));
    }

    if (c.phase() == Phase::Press && c.pressIndex() < static_cast<int>(c.pressQuestions().size())) {
        const PressQuestion& q = c.pressQuestions()[static_cast<size_t>(c.pressIndex())];
        Json p = Json::makeObject();
        p.set("index", c.pressIndex());
        p.set("total", static_cast<int>(c.pressQuestions().size()));
        p.set("text", q.text);
        p.set("tag", q.tag);
        Json ans = Json::makeArray();
        for (const PressAnswerDef& a : q.answers) {
            Json o = Json::makeObject();
            o.set("type", a.type);
            o.set("text", a.text);
            ans.push(o);
        }
        p.set("answers", ans);
        j.set("press", p);
    }

    if (c.phase() == Phase::SeasonReview) {
        const SeasonReview& r = c.review();
        Json rv = Json::makeObject();
        rv.set("title", r.title);
        rv.set("text", r.text);
        rv.set("checks", strList(r.checks));
        rv.set("promoted", r.promoted);
        rv.set("demoted", r.demoted);
        rv.set("avgMark", r.avgMark);
        rv.set("matches", r.matches);
        rv.set("toTier", r.toTier);
        j.set("review", rv);
    }

    if (c.phase() == Phase::Over) {
        Json e = Json::makeObject();
        e.set("id", c.endingId());
        e.set("title", c.endingTitle());
        e.set("text", c.endingText());
        j.set("ending", e);
    }

    Json hist = Json::makeArray();
    for (const HistoryEntry& h : c.history()) {
        Json o = Json::makeObject();
        o.set("season", h.season);
        o.set("week", h.week);
        o.set("tier", h.tier);
        o.set("role", roleId(h.role));
        o.set("fixture", h.fixture);
        o.set("importance", h.importance);
        o.set("mark", h.mark);
        o.set("kmiErrors", h.kmiErrors);
        hist.push(o);
    }
    j.set("history", hist);
    return j;
}

Json planJson() {
    Json plan = Json::makeArray();
    for (const PlannedIncident& p : gMatch->plan()) {
        const IncidentTemplate& t = *p.tpl;
        Json o = Json::makeObject();
        o.set("index", p.index);
        o.set("minute", p.minute);
        o.set("id", t.id);
        o.set("name", t.name);
        o.set("stage", t.stage);
        o.set("anim", t.anim);
        o.set("inBox", t.inBox);
        o.set("kmi", t.kmi);
        o.set("difficulty", t.difficulty);
        o.set("offendingSide", p.offendingSide);
        o.set("offsideMargin", t.offsideMargin);
        o.set("truth", verdictJson(t.truth));
        o.set("onField", verdictJson(p.onField));
        o.set("resolved", p.resolved);
        o.set("dynamic", p.dynamicConfrontation);
        plan.push(o);
    }
    return plan;
}

Json dissentJson() {
    const DissentEpisode& d = gMatch->dissent();
    if (!d.active) return Json();
    Json o = Json::makeObject();
    o.set("side", d.side);
    o.set("who", d.who);
    o.set("anger", d.anger);
    o.set("abusive", d.abusive);
    o.set("text", d.text);
    Json opts = Json::makeArray();
    for (DissentResponse r : gMatch->dissentOptions()) opts.push(gMatch->dissentOptionLabel(r));
    o.set("options", opts);
    return o;
}

Json matchState() {
    Json j = ok();
    j.set("minute", gMatch->minute());
    j.set("heat", gMatch->heat());
    j.set("calm", gMatch->calm());
    j.set("breaths", gMatch->breathsLeft());
    j.set("score0", gMatch->score(0));
    j.set("score1", gMatch->score(1));
    j.set("plan", planJson());
    j.set("dissent", dissentJson());
    return j;
}

ViewSample viewFrom(const Json& v) {
    ViewSample s;
    s.distance = v["distance"].asNumber(20);
    s.angleQuality = v["angleQuality"].asNumber(0.5);
    s.occlusion = v["occlusion"].asNumber(0);
    s.alignment = v["alignment"].asNumber(3);
    s.replay = v["replay"].asBool(false);
    return s;
}

Json resultJson(const DecisionResult& r) {
    Json j = ok();
    j.set("correct", r.correct);
    j.set("acceptable", r.acceptable);
    j.set("kmi", r.kmi);
    j.set("timedOut", r.timedOut);
    j.set("final", verdictJson(r.finalVerdict));
    j.set("truth", verdictJson(r.truth));
    j.set("feedback", r.feedback);
    j.set("offendingSide", r.offendingSide);
    j.set("benefitSide", r.benefitSide);
    j.set("varReviewOffered", r.varReviewOffered);
    j.set("heat", gMatch->heat());
    j.set("calm", gMatch->calm());
    j.set("dissent", dissentJson());
    return j;
}

bool needCareer(Json& out) {
    if (!gCareer) {
        out = fail("no career");
        return false;
    }
    return true;
}

bool needMatch(Json& out) {
    if (!gMatch) {
        out = fail("no match in progress");
        return false;
    }
    return true;
}

uint64_t seedFrom(const Json& j) {
    const std::string s = j["seed"].asString();
    if (!s.empty()) return static_cast<uint64_t>(std::strtoull(s.c_str(), nullptr, 10));
    return static_cast<uint64_t>(j["seed"].asNumber(12345));
}

Json dispatch(const Json& in) {
    const std::string cmd = in["cmd"].asString();
    Json out;

    if (cmd == "init") {
        std::string err;
        gLoaded = gContent.loadFromStrings(in["career"].asString(), in["incidents"].asString(), in["events"].asString(),
                                           in["press"].asString(), in["strings"].asString(), &err);
        gCareer.reset();
        gMatch.reset();
        return gLoaded ? ok() : fail(err);
    }
    if (!gLoaded) return fail("content not loaded");

    if (cmd == "career.new") {
        Role r = Role::Center;
        parseRole(in["role"].asString("center"), r);
        gMatch.reset();
        gCareer = std::make_unique<Career>(gContent);
        gCareer->startNew(in["name"].asString(), r, seedFrom(in));
        return careerState();
    }
    if (cmd == "career.load") {
        Json save;
        std::string err;
        if (!Json::parse(in["save"].asString(), save, &err)) return fail("save: " + err);
        auto c = std::make_unique<Career>(gContent);
        if (!c->fromJson(save, &err)) return fail(err);
        gMatch.reset();
        gCareer = std::move(c);
        return careerState();
    }
    if (!needCareer(out)) return out;
    Career& c = *gCareer;

    if (cmd == "career.state") return careerState();
    if (cmd == "career.save") {
        Json j = ok();
        j.set("save", c.toJson().dump());
        return j;
    }
    if (cmd == "career.confirmSeasonStart") {
        c.confirmSeasonStart();
        return careerState();
    }
    if (cmd == "career.commitWeek") {
        std::vector<std::string> ids;
        for (const Json& a : in["activities"].items()) ids.push_back(a.asString());
        Role r = c.preferredRole();
        parseRole(in["role"].asString(roleId(r)), r);
        const std::vector<std::string> log = c.commitWeek(ids, r);
        Json j = careerState();
        j.set("log", strList(log));
        return j;
    }
    if (cmd == "career.chooseEvent") {
        const std::string text = c.chooseEvent(in["choice"].asInt(0));
        Json j = careerState();
        j.set("result", text);
        return j;
    }
    if (cmd == "career.acceptAppointment") {
        c.acceptAppointment();
        return careerState();
    }
    if (cmd == "career.continueAfterReport") {
        c.continueAfterReport();
        return careerState();
    }
    if (cmd == "career.answerPress") {
        const std::string text = c.answerPress(in["answer"].asInt(0));
        Json j = careerState();
        j.set("result", text);
        return j;
    }
    if (cmd == "career.continueAfterReview") {
        c.continueAfterReview();
        return careerState();
    }

    if (cmd == "match.begin") {
        if (c.phase() != Phase::Match) return fail("career is not in the match phase");
        const MatchSetup setup = c.matchSetup();
        gMatch = std::make_unique<MatchSession>(gContent, setup);
        const TierDef& t = gMatch->tier();
        Json j = matchState();
        j.set("tier", tierJson(t));
        j.set("tierIndex", setup.tier);
        j.set("importance", setup.importance);
        j.set("derby", setup.derby);
        j.set("role", roleId(setup.role));
        j.set("fixture", setup.fixtureName);
        j.set("home", teamJson(t.teams[static_cast<size_t>(setup.home)]));
        j.set("away", teamJson(t.teams[static_cast<size_t>(setup.away)]));
        j.set("fitness", setup.stats.get(Stat::Fitness));
        j.set("personality", setup.stats.get(Stat::Personality));
        j.set("seed", std::to_string(setup.seed % 1000000007ull));
        return j;
    }
    if (!needMatch(out)) return out;
    MatchSession& m = *gMatch;

    if (cmd == "match.state") return matchState();
    if (cmd == "match.advance") {
        m.advance(in["minute"].asNumber(m.minute()));
        Json j = ok();
        j.set("heat", m.heat());
        j.set("calm", m.calm());
        j.set("due", m.dueIncident(m.minute()));
        return j;
    }
    if (cmd == "match.due") {
        Json j = ok();
        j.set("index", m.dueIncident(in["minute"].asNumber(m.minute())));
        return j;
    }
    if (cmd == "match.perceive") {
        const Perception p = m.perceive(in["index"].asInt(-1), viewFrom(in["view"]));
        Json j = ok();
        j.set("clarity", p.clarity);
        j.set("cues", strList(p.cues));
        j.set("note", p.note);
        return j;
    }
    if (cmd == "match.options") {
        const int idx = in["index"].asInt(-1);
        Json j = ok();
        Json opts = Json::makeArray();
        for (const DecisionOption& o : m.options(idx)) {
            Json oj = Json::makeObject();
            oj.set("key", o.key);
            oj.set("label", o.label);
            oj.set("intervene", o.intervene);
            oj.set("verdict", verdictJson(o.verdict));
            opts.push(oj);
        }
        j.set("options", opts);
        j.set("window", m.decisionWindowSeconds(idx));
        return j;
    }
    if (cmd == "match.decide")
        return resultJson(m.decide(in["index"].asInt(-1), in["key"].asString(), in["reaction"].asNumber(1), viewFrom(in["view"])));
    if (cmd == "match.timeout") return resultJson(m.timeout(in["index"].asInt(-1), viewFrom(in["view"])));
    if (cmd == "match.review") return resultJson(m.onFieldReview(in["index"].asInt(-1), in["key"].asString("keep")));
    if (cmd == "match.respond") {
        const std::vector<DissentResponse> opts = m.dissentOptions();
        const int i = in["option"].asInt(0);
        if (i < 0 || i >= static_cast<int>(opts.size())) return fail("bad dissent option");
        const DissentResult r = m.respondDissent(opts[static_cast<size_t>(i)]);
        Json j = ok();
        j.set("success", r.success);
        j.set("cardShown", r.cardShown);
        j.set("coachSentOff", r.coachSentOff);
        j.set("text", r.text);
        j.set("heat", m.heat());
        j.set("calm", m.calm());
        return j;
    }
    if (cmd == "match.breathe") {
        Json j = ok();
        j.set("used", m.breathe());
        j.set("breaths", m.breathsLeft());
        j.set("calm", m.calm());
        return j;
    }
    if (cmd == "match.position") {
        m.addPositionSample(in["quality"].asNumber(0.7));
        return ok();
    }
    if (cmd == "match.fitness") {
        m.setFitnessData(in["workRate"].asNumber(0.8), in["exhausted"].asNumber(0));
        return ok();
    }
    if (cmd == "match.goal") {
        m.goalScored(in["side"].asInt(0));
        return ok();
    }
    if (cmd == "match.card") {
        const std::string card = in["card"].asString("yellow");
        m.cardFromPlay(card == "red" ? Card::Red : Card::Yellow);
        return ok();
    }
    if (cmd == "match.finish") {
        const MatchReport rep = m.finish();
        gMatch.reset();
        c.completeMatch(rep);
        return careerState();
    }
    return fail("unknown command: " + cmd);
}

}  // namespace

extern "C" {

__attribute__((export_name("rc_alloc"))) char* rc_alloc(int size) { return static_cast<char*>(std::malloc(static_cast<size_t>(size) + 1)); }

__attribute__((export_name("rc_free"))) void rc_free(char* p) { std::free(p); }

__attribute__((export_name("rc_call"))) const char* rc_call(const char* input) {
    Json in;
    std::string err;
    if (!Json::parse(input ? std::string(input) : std::string(), in, &err)) {
        gOut = fail("bad request: " + err).dump();
    } else {
        gOut = dispatch(in).dump();
    }
    return gOut.c_str();
}

__attribute__((export_name("rc_result_length"))) int rc_result_length() { return static_cast<int>(gOut.size()); }

}  // extern "C"
