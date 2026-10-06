// A scripted "player" used by tests and the balance simulator. It stands in for the Unreal layer:
// it produces view samples, makes calls whose accuracy depends on clarity and skill, and handles dissent.
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "RefCore/RcCareer.h"
#include "RefCore/RcMatch.h"

namespace simbot {

using namespace refcore;

inline std::string truthKey(const MatchSession& m, int idx) {
    const PlannedIncident& p = m.plan()[static_cast<size_t>(idx)];
    const std::vector<DecisionOption> opts = m.options(idx);
    if (m.setup().role == Role::Var) {
        const IncidentTemplate& t = *p.tpl;
        const bool onFieldOk = p.onField == t.truth || std::find(t.acceptable.begin(), t.acceptable.end(), p.onField) != t.acceptable.end();
        if (onFieldOk) return "keep";
    }
    for (const DecisionOption& o : opts)
        if (o.verdict == p.tpl->truth && (m.setup().role != Role::Var || o.intervene)) return o.key;
    return opts.empty() ? std::string() : opts.front().key;
}

inline DissentResponse idealDissent(const DissentEpisode& d) {
    if (d.abusive) return d.who == "coach" ? DissentResponse::SendOffCoach : DissentResponse::YellowCard;
    if (d.anger >= 60) return DissentResponse::PublicWarning;
    return DissentResponse::CalmTalk;
}

inline MatchReport playMatch(const Content& content, const MatchSetup& setup, double skill, Rng& rng) {
    MatchSession m(content, setup);
    const double fitness = setup.stats.get(Stat::Fitness);
    for (double minute = 0; minute <= 90.0; minute += 0.5) {
        m.advance(minute);
        if (static_cast<int>(minute * 2) % 2 == 0)
            m.addPositionSample(clampd(rng.normal(0.55 + 0.4 * skill * (0.6 + 0.4 * fitness / 100.0), 0.1), 0, 1));
        if (m.calm() < 35 && m.breathsLeft() > 0 && rng.chance(skill)) m.breathe();
        int idx = m.dueIncident(minute);
        int guard = 0;
        while (idx >= 0 && guard++ < 10) {
            ViewSample v;
            v.distance = std::max(4.0, lerpd(34.0, 13.0, skill) + rng.normal(0, 5));
            v.angleQuality = clampd(0.25 + 0.6 * skill + rng.normal(0, 0.15), 0, 1);
            v.occlusion = rng.chance(0.35 * (1.0 - skill)) ? 0.6 : 0.0;
            v.alignment = std::max(0.0, lerpd(6.0, 0.6, skill) + rng.normal(0, 0.8));
            v.replay = setup.role == Role::Var;
            const Perception per = m.perceive(idx, v);
            const std::vector<DecisionOption> opts = m.options(idx);
            const std::string right = truthKey(m, idx);
            std::string key = right;
            const double pRight = clampd(per.clarity * 0.55 + 0.45 * skill, 0.05, 0.98);
            if (!rng.chance(pRight) && opts.size() > 1) {
                do { key = opts[static_cast<size_t>(rng.irange(0, static_cast<int>(opts.size()) - 1))].key; } while (key == right);
            }
            const DecisionResult r = m.decide(idx, key, m.decisionWindowSeconds(idx) * lerpd(0.7, 0.3, skill), v);
            if (r.varReviewOffered) m.onFieldReview(idx, rng.chance(0.75 + 0.2 * skill) ? right : key);
            if (m.dissent().active) {
                const std::vector<DissentResponse> resp = m.dissentOptions();
                m.respondDissent(rng.chance(skill) ? idealDissent(m.dissent()) : resp[static_cast<size_t>(rng.irange(0, static_cast<int>(resp.size()) - 1))]);
            }
            idx = m.dueIncident(minute);
        }
        if (rng.chance(0.012)) m.goalScored(rng.irange(0, 1));
    }
    m.setFitnessData(clampd(fitness / 100.0 + 0.25, 0, 1), clampd(0.55 - fitness / 180.0, 0, 1));
    return m.finish();
}

struct CareerOutcome {
    std::string ending;
    int maxTier = 0;
    int seasons = 0;
    int matches = 0;
    double avgMark = 0;
    std::vector<int> seasonReached;  // season number when each tier was first reached
};

inline std::vector<std::string> pickActivities(const Career& c, Rng& rng) {
    std::vector<std::string> wanted;
    const Stats& s = c.stats();
    const double nextTest = c.tierIndex() + 1 < 7 ? 45.0 + 5.0 * c.tierIndex() : 74.0;
    if (s.get(Stat::Mental) < 40) wanted.push_back(rng.chance(0.5) ? "psych" : "rest");
    if (s.get(Stat::Fitness) < nextTest + 8) wanted.push_back("fitness");
    if (c.tierIndex() >= 2 && !c.hasFlag("var_certified")) wanted.push_back("var_course");
    if (s.get(Stat::Trust) < 75) wanted.push_back("seminar");
    if (s.get(Stat::Personality) < 72) wanted.push_back("mentor");
    wanted.push_back("video");
    wanted.push_back("laws");
    wanted.push_back("gym");
    wanted.push_back("fitness");
    wanted.push_back("rest");
    std::vector<std::string> out;
    for (const std::string& id : wanted) {
        if (static_cast<int>(out.size()) >= c.slots()) break;
        for (const ActivityStatus& a : c.activities())
            if (a.def->id == id && a.available) { out.push_back(id); break; }
    }
    return out;
}

inline int pickEventChoice(const Career& c, double skill, Rng& rng) {
    const EventDef* e = c.currentEvent();
    const std::vector<EventChoiceStatus> st = c.eventChoices();
    if (!e || st.empty()) return 0;
    // A careful bot reports corruption; a careless one sometimes takes the money.
    for (size_t i = 0; i < e->choices.size(); ++i) {
        const Effects& fx = e->choices[i].effects;
        if (!st[i].available) continue;
        if (!fx.fix.empty() && rng.chance(0.85 + 0.15 * skill)) continue;
        if (fx.ending == "pundit") continue;
        if (e->id == "fixing_approach" && i == 0 && skill > 0.5) continue;  // prefers reporting
        double gain = 0;
        for (double d : fx.stats) gain += d;
        if (gain >= -2 || rng.chance(0.3)) return static_cast<int>(i);
    }
    for (size_t i = 0; i < st.size(); ++i)
        if (st[i].available) return static_cast<int>(i);
    return 0;
}

inline CareerOutcome playCareer(const Content& content, Role role, double skill, uint64_t seed,
                                void (*onReview)(const Career&) = nullptr) {
    Career c(content);
    c.startNew("Bot", role, seed);
    Rng rng(seed * 7919u + 13u);
    CareerOutcome out;
    out.seasonReached.assign(content.tiers.size(), -1);
    out.seasonReached[0] = 1;
    int steps = 0;
    while (c.phase() != Phase::Over && steps++ < 20000) {
        switch (c.phase()) {
            case Phase::SeasonStart: c.confirmSeasonStart(); break;
            case Phase::Planning: {
                Role pref = role;
                if (c.canWorkAs(Role::Var) && rng.chance(0.2)) pref = Role::Var;
                c.commitWeek(pickActivities(c, rng), pref);
                break;
            }
            case Phase::Event: {
                if (c.chooseEvent(pickEventChoice(c, skill, rng)).empty()) {
                    // No valid choice: pick the first available one.
                    const std::vector<EventChoiceStatus> st = c.eventChoices();
                    for (size_t i = 0; i < st.size(); ++i)
                        if (st[i].available) { c.chooseEvent(static_cast<int>(i)); break; }
                }
                break;
            }
            case Phase::Appointment: c.acceptAppointment(); break;
            case Phase::Match: c.completeMatch(playMatch(content, c.matchSetup(), skill, rng)); break;
            case Phase::PostMatch: c.continueAfterReport(); break;
            case Phase::Press: {
                const PressQuestion& q = c.pressQuestions()[static_cast<size_t>(c.pressIndex())];
                int pick = 0;
                const std::string want = rng.chance(skill) ? (q.correct ? "defend" : "admit") : "no_comment";
                for (size_t i = 0; i < q.answers.size(); ++i)
                    if (q.answers[i].type == want) pick = static_cast<int>(i);
                c.answerPress(pick);
                break;
            }
            case Phase::SeasonReview:
                if (onReview) onReview(c);
                c.continueAfterReview();
                if (c.phase() != Phase::Over && out.seasonReached[static_cast<size_t>(c.tierIndex())] < 0)
                    out.seasonReached[static_cast<size_t>(c.tierIndex())] = c.season();
                break;
            case Phase::Over: break;
        }
        out.maxTier = std::max(out.maxTier, c.tierIndex());
    }
    out.ending = c.endingId().empty() ? "unfinished" : c.endingId();
    out.seasons = c.season();
    out.matches = static_cast<int>(c.history().size());
    double sum = 0;
    for (const HistoryEntry& h : c.history()) sum += h.mark;
    out.avgMark = out.matches ? sum / out.matches : 0;
    return out;
}

}  // namespace simbot
