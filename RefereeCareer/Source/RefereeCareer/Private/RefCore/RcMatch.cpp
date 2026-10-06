#include "RefCore/RcMatch.h"

#include <algorithm>
#include <cmath>

namespace refcore {

namespace {

bool matchHasRole(const std::vector<Role>& roles, Role r) {
    return std::find(roles.begin(), roles.end(), r) != roles.end();
}

bool matchInList(const std::vector<Verdict>& list, const Verdict& v) {
    return std::find(list.begin(), list.end(), v) != list.end();
}

bool matchIsLineStage(const IncidentTemplate& t) { return t.stage == "offside" || t.stage == "goal_check"; }

Verdict matchCode(const char* code) {
    Verdict v;
    parseVerdict(code, v);
    return v;
}

const char* matchConfrontationCode(const Verdict& v) {
    if (v.card == Card::Red) return "conf_r";
    if (v.card == Card::Yellow) return v.cardTo == CardTo::Both ? "conf_y2" : "conf_y";
    return "conf_none";
}

std::string matchMinuteText(double minute) { return std::to_string(static_cast<int>(minute) + 1) + "'"; }

}  // namespace

MatchSession::MatchSession(const Content& content, const MatchSetup& setup)
    : content_(content), setup_(setup), rng_(setup.seed) {
    const TierDef& t = tier();
    heat_ = t.baseHeat + (setup_.derby ? 15.0 : 0.0) + (setup_.importance >= 4 ? 8.0 : 0.0);
    calm_ = clampd(35.0 + 0.4 * setup_.stats.get(Stat::Personality) + 0.25 * setup_.stats.get(Stat::Mental), 20.0, 100.0);
    buildPlan();
}

const TierDef& MatchSession::tier() const {
    const int idx = std::max(0, std::min(setup_.tier, static_cast<int>(content_.tiers.size()) - 1));
    return content_.tiers[static_cast<size_t>(idx)];
}

void MatchSession::buildPlan() {
    const TierDef& t = tier();
    const Role role = setup_.role;
    std::vector<const IncidentTemplate*> pool;
    std::vector<double> weights;
    for (const IncidentTemplate& tpl : content_.incidents) {
        if (tpl.stage == "confrontation") continue;  // only triggered by match heat
        if (tpl.minTier > setup_.tier) continue;
        bool fits = false;
        if (role == Role::Var) {
            fits = tpl.reviewable && (matchHasRole(tpl.roles, Role::Var) || matchHasRole(tpl.roles, Role::Center) ||
                                      matchHasRole(tpl.roles, Role::Assistant));
        } else {
            fits = matchHasRole(tpl.roles, role);
        }
        if (!fits) continue;
        double w = tpl.weight;
        if (tpl.difficulty > t.difficulty + 1) w *= 0.3;
        if (tpl.difficulty < t.difficulty - 1) w *= 0.6;
        pool.push_back(&tpl);
        weights.push_back(w);
    }
    if (pool.empty()) return;

    int count = t.incidentsPerMatch + (setup_.importance >= 4 ? 1 : 0) + rng_.irange(-1, 1);
    if (role == Role::Var) count = std::max(4, count - 2);
    count = std::max(3, count);

    const double span = 86.0 / count;
    for (int i = 0; i < count; ++i) {
        const int pick = rng_.weighted(weights);
        if (pick < 0) break;
        weights[static_cast<size_t>(pick)] *= 0.45;  // variety
        PlannedIncident p;
        p.index = static_cast<int>(plan_.size());
        p.tpl = pool[static_cast<size_t>(pick)];
        p.minute = 2.0 + span * i + rng_.range(0.6, span - 0.6);
        p.offendingSide = rng_.irange(0, 1);
        if (role == Role::Var) p.onField = aiCall(*p.tpl);
        plan_.push_back(p);
    }
}

Verdict MatchSession::aiCall(const IncidentTemplate& t) {
    const double acc = tier().aiAccuracy;
    const double pWrong = setup_.role == Role::Var ? clampd(1.0 - acc + 0.2, 0.3, 0.5) : 1.0 - acc;
    if (rng_.chance(pWrong)) {
        if (!t.likelyWrong.empty()) return t.likelyWrong[static_cast<size_t>(rng_.irange(0, static_cast<int>(t.likelyWrong.size()) - 1))];
        switch (t.truth.restart) {
            case Restart::Goal: return matchCode("no_goal");
            case Restart::NoGoal: return matchCode("goal");
            case Restart::Offside: return matchCode("play");
            case Restart::Penalty: return t.truth.card == Card::Red ? matchCode("pen") : matchCode("play");
            case Restart::PlayOn:
            case Restart::Advantage: return t.inBox ? matchCode("pen") : matchCode("fk");
            case Restart::FreeKick:
                if (t.truth.card == Card::Red) return matchCode("fk_y");
                if (t.truth.cardTo == CardTo::Victim) return t.inBox ? matchCode("pen") : matchCode("fk");
                return matchCode("play");
        }
    }
    if (!t.acceptable.empty() && rng_.chance(0.3))
        return t.acceptable[static_cast<size_t>(rng_.irange(0, static_cast<int>(t.acceptable.size()) - 1))];
    return t.truth;
}

int MatchSession::dueIncident(double minuteNow) const {
    for (const PlannedIncident& p : plan_)
        if (!p.resolved && p.minute <= minuteNow) return p.index;
    return -1;
}

double MatchSession::effectiveClarity(const PlannedIncident& p, const ViewSample& view) const {
    const IncidentTemplate& t = *p.tpl;
    double base = 0;
    if (view.replay) {
        base = 0.72 + 0.28 * clampd(view.angleQuality, 0, 1);
    } else if (matchIsLineStage(t) && setup_.role == Role::Assistant) {
        base = clampd(1.0 - view.alignment / 7.0, 0.1, 1.0) * (1.0 - 0.5 * clampd(view.occlusion, 0, 1));
    } else {
        const double d = std::max(0.0, view.distance);
        const double df = d <= 10 ? 1.0 : clampd(1.0 - (d - 10.0) / 32.0, 0.12, 1.0);
        const double af = 0.55 + 0.45 * clampd(view.angleQuality, 0, 1);
        base = df * af * (1.0 - 0.7 * clampd(view.occlusion, 0, 1));
    }
    const double bonus = 0.10 * setup_.stats.get(Stat::Laws) / 100.0 + 0.08 * setup_.roleSkill / 100.0;
    const double penalty = 0.15 * (1.0 - calm_ / 100.0) + 0.035 * (t.difficulty - 1);
    return clampd(base + bonus - penalty, 0.05, 1.0);
}

Perception MatchSession::perceive(int idx, const ViewSample& view) const {
    Perception out;
    if (idx < 0 || idx >= static_cast<int>(plan_.size())) return out;
    const PlannedIncident& p = plan_[static_cast<size_t>(idx)];
    const IncidentTemplate& t = *p.tpl;
    out.clarity = effectiveClarity(p, view);
    // Deterministic per incident and viewing quality, so re-asking does not reroll what you saw.
    Rng local(Rng::mix(setup_.seed, static_cast<uint64_t>(idx) * 131u + static_cast<uint64_t>(out.clarity * 20)));

    if (matchIsLineStage(t) && t.offsideMargin != 0.0) {
        const double perceived = t.offsideMargin + local.normal(0.0, (1.0 - out.clarity) * 1.1);
        const char* key = perceived > 0.6 ? "cue.offside.clear_off"
                          : perceived > 0.05 ? "cue.offside.slight_off"
                          : perceived > -0.5 ? "cue.offside.level"
                                             : "cue.offside.behind";
        out.cues.push_back(content_.str(key));
    }
    for (const CueDef& c : t.cues)
        if (out.clarity >= c.clarity) out.cues.push_back(c.text);
    if (!t.misleading.empty() && local.chance((1.0 - out.clarity) * 0.6)) {
        const std::string& m = t.misleading[static_cast<size_t>(local.irange(0, static_cast<int>(t.misleading.size()) - 1))];
        const size_t at = out.cues.empty() ? 0 : static_cast<size_t>(local.irange(0, static_cast<int>(out.cues.size())));
        out.cues.insert(out.cues.begin() + static_cast<std::ptrdiff_t>(at), m);
    }
    if (out.clarity < 0.3)
        out.note = content_.str(view.occlusion > 0.5 ? "note.blocked" : "note.far");
    else if (out.clarity < 0.55)
        out.note = content_.str("note.partial");
    else
        out.note = content_.str("note.clear");
    if (setup_.role == Role::Var && p.tpl)
        out.note = content_.fmt("note.var_onfield", {{"decision", content_.verdictLabel(p.onField, Role::Center)}}) + " " + out.note;
    return out;
}

std::vector<DecisionOption> MatchSession::options(int idx) const {
    std::vector<DecisionOption> out;
    if (idx < 0 || idx >= static_cast<int>(plan_.size())) return out;
    const PlannedIncident& p = plan_[static_cast<size_t>(idx)];
    const IncidentTemplate& t = *p.tpl;
    const Role role = setup_.role;

    auto add = [&](const char* code, Role labelRole, bool intervene) {
        DecisionOption o;
        o.key = code;
        parseVerdict(code, o.verdict);
        o.label = content_.verdictLabel(o.verdict, labelRole);
        o.intervene = intervene;
        out.push_back(o);
    };

    if (t.stage == "confrontation") {
        for (const char* code : {"conf_none", "conf_y", "conf_y2", "conf_r"}) {
            DecisionOption o;
            o.key = code;
            parseVerdict(code, o.verdict);
            o.label = content_.str(std::string("opt.") + code);
            out.push_back(o);
        }
        return out;
    }

    std::vector<const char*> codes;
    if (t.stage == "goal_check") codes = {"goal", "no_goal"};
    else if (t.stage == "offside") codes = {"offside", "play"};
    else if (role == Role::Assistant) codes = t.inBox ? std::vector<const char*>{"play", "pen", "pen_y", "pen_r"}
                                                      : std::vector<const char*>{"play", "fk", "fk_y", "fk_r"};
    else codes = t.inBox ? std::vector<const char*>{"play", "pen", "pen_y", "pen_r", "dive_y"}
                         : std::vector<const char*>{"play", "adv", "fk", "fk_y", "fk_r", "dive_y"};

    if (role == Role::Var) {
        DecisionOption keep;
        keep.key = "keep";
        keep.verdict = p.onField;
        keep.label = content_.fmt("opt.var.keep", {{"decision", content_.verdictLabel(p.onField, Role::Center)}});
        out.push_back(keep);
        for (const char* code : codes) {
            Verdict v;
            parseVerdict(code, v);
            if (v == p.onField || v.restart == Restart::Advantage) continue;
            DecisionOption o;
            o.key = code;
            o.verdict = v;
            o.intervene = true;
            o.label = content_.fmt("opt.var.recommend", {{"decision", content_.verdictLabel(v, Role::Center)}});
            out.push_back(o);
        }
        return out;
    }
    for (const char* code : codes) add(code, role, false);
    return out;
}

double MatchSession::decisionWindowSeconds(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(plan_.size())) return 5.0;
    double base = setup_.role == Role::Center ? 5.5 : setup_.role == Role::Assistant ? 4.0 : 40.0;
    if (plan_[static_cast<size_t>(idx)].tpl->stage == "confrontation") base = 7.0;
    return base * (0.65 + 0.35 * calm_ / 100.0 + 0.1 * setup_.stats.get(Stat::Laws) / 100.0);
}

DecisionResult MatchSession::decide(int idx, const std::string& optionKey, double reactionSeconds, const ViewSample& view) {
    DecisionResult r;
    if (idx < 0 || idx >= static_cast<int>(plan_.size())) return r;
    PlannedIncident& p = plan_[static_cast<size_t>(idx)];
    if (p.resolved) return r;
    const std::vector<DecisionOption> opts = options(idx);
    const DecisionOption* chosen = nullptr;
    for (const DecisionOption& o : opts)
        if (o.key == optionKey) chosen = &o;
    if (!chosen) return timeout(idx, view);

    const double clarity = effectiveClarity(p, view);
    const IncidentTemplate& t = *p.tpl;
    const Verdict finalV = (setup_.role == Role::Var && !chosen->intervene) ? p.onField : chosen->verdict;
    const bool ok = finalV == t.truth || matchInList(t.acceptable, finalV);

    if (setup_.role == Role::Center && tier().hasVar && t.reviewable && !ok && rng_.chance(0.85)) {
        pendingReview_ = idx;
        pendingReviewInitial_ = finalV;
        r.varReviewOffered = true;
        r.finalVerdict = finalV;
        r.truth = t.truth;
        r.kmi = t.kmi;
        r.offendingSide = p.offendingSide;
        r.feedback = content_.str("fb.var_review");
        return r;  // logged once the referee has watched the monitor (onFieldReview)
    }
    registerOutcome(p, finalV, chosen->intervene, clarity, reactionSeconds, false, r);
    return r;
}

DecisionResult MatchSession::timeout(int idx, const ViewSample& view) {
    DecisionResult r;
    if (idx < 0 || idx >= static_cast<int>(plan_.size())) return r;
    PlannedIncident& p = plan_[static_cast<size_t>(idx)];
    if (p.resolved) return r;
    Verdict v;
    if (setup_.role == Role::Var) v = p.onField;
    else if (p.tpl->stage == "confrontation") parseVerdict("conf_none", v);
    else if (p.tpl->stage == "goal_check") parseVerdict("goal", v);
    registerOutcome(p, v, false, effectiveClarity(p, view), decisionWindowSeconds(idx), true, r);
    return r;
}

DecisionResult MatchSession::onFieldReview(int idx, const std::string& optionKey) {
    DecisionResult r;
    if (idx != pendingReview_ || idx < 0) return r;
    PlannedIncident& p = plan_[static_cast<size_t>(idx)];
    Verdict v = pendingReviewInitial_;
    for (const DecisionOption& o : options(idx))
        if (o.key == optionKey) v = o.verdict;
    pendingReview_ = -1;
    ViewSample monitor;
    monitor.replay = true;
    monitor.angleQuality = 0.8;
    registerOutcome(p, v, true, effectiveClarity(p, monitor), 0, false, r);
    return r;
}

void MatchSession::registerOutcome(PlannedIncident& p, const Verdict& finalV, bool intervened, double clarity,
                                   double reaction, bool timedOut, DecisionResult& r) {
    const IncidentTemplate& t = *p.tpl;
    p.resolved = true;
    const bool correct = finalV == t.truth;
    const bool acceptable = correct || matchInList(t.acceptable, finalV);
    const bool reviewedAsCentre = setup_.role == Role::Center && intervened;

    double credit = correct ? 1.0 : acceptable ? 0.75 : 0.0;
    if (setup_.role == Role::Var) {
        const bool onFieldOk = p.onField == t.truth || matchInList(t.acceptable, p.onField);
        if (!intervened) credit = p.onField == t.truth ? 1.0 : onFieldOk ? 0.9 : 0.0;
        else if (!onFieldOk) credit = correct ? 1.0 : acceptable ? 0.8 : 0.1;
        else credit = acceptable ? 0.5 : 0.0;  // re-refereeing a call that was not clearly wrong
        addPositionSample(clampd(1.0 - 0.6 * reaction / std::max(1.0, decisionWindowSeconds(p.index)), 0.4, 1.0));
    } else if (reviewedAsCentre) {
        credit = correct ? 0.5 : acceptable ? 0.4 : 0.0;
    }
    if (!timedOut && reaction > 0.8 * decisionWindowSeconds(p.index)) credit = std::max(0.0, credit - 0.1);

    r.correct = correct;
    r.acceptable = acceptable;
    r.kmi = t.kmi;
    r.timedOut = timedOut;
    r.finalVerdict = finalV;
    r.truth = t.truth;
    r.offendingSide = p.offendingSide;
    r.benefitSide = benefitingSide(finalV, p.offendingSide);
    r.feedback = content_.str(acceptable ? "fb.good" : "fb.bad");

    IncidentLog l;
    l.index = p.index;
    l.minute = p.minute;
    l.templateId = t.id;
    l.name = t.name;
    l.chosenLabel = content_.verdictLabel(finalV, Role::Center);
    l.truthLabel = content_.verdictLabel(t.truth, Role::Center);
    if (t.stage == "confrontation") {
        l.chosenLabel = content_.str(std::string("opt.") + matchConfrontationCode(finalV));
        l.truthLabel = content_.str(std::string("opt.") + matchConfrontationCode(t.truth));
    }
    l.law = t.law;
    l.correct = correct;
    l.acceptable = acceptable;
    l.kmi = t.kmi;
    l.correctedByVar = reviewedAsCentre && acceptable;
    l.clarity = clarity;
    l.reaction = reaction;
    l.offendingSide = p.offendingSide;
    l.credit = credit;
    log_.push_back(l);

    // Match temperature and the referee's own composure.
    if (credit < 0.4) {
        heat_ += t.heat;
        calm_ -= t.kmi ? 12.0 : 5.0;
        controlPoints_ -= t.kmi ? 2.0 : 1.0;
    } else {
        heat_ -= finalV.card != Card::None ? 4.0 : 2.0;
        calm_ += 2.0;
        controlPoints_ += 0.5;
    }
    if (t.truth.card != Card::None && finalV.card == Card::None) heat_ += 6.0;
    heat_ = clampd(heat_, 0, 100);
    calm_ = clampd(calm_, 0, 100);

    if (finalV.card == Card::Yellow) yellows_ += finalV.cardTo == CardTo::Both ? 2 : 1;
    if (finalV.card == Card::Red) reds_ += 1;

    const int truthBenefit = benefitingSide(t.truth, p.offendingSide);
    if (!acceptable && r.benefitSide != truthBenefit) wrongFavor_[static_cast<size_t>(r.benefitSide)] += 1;

    Controversy c;
    c.incidentIndex = p.index;
    c.correct = acceptable;
    c.minute = p.minute;
    c.chosenLabel = l.chosenLabel;
    c.name = t.name;
    if (t.kmi) c.tag = (setup_.role == Role::Var || reviewedAsCentre) ? "var" : "kmi";
    else if (finalV.card == Card::Red) c.tag = "red";
    else if (finalV.restart == Restart::Penalty) c.tag = "penalty";
    if (!c.tag.empty()) controversies_.push_back(c);

    if (setup_.role != Role::Var) maybeDissent(p, r);
    maybeConfrontation();
}

void MatchSession::maybeDissent(const PlannedIncident& p, const DecisionResult& r) {
    if (dissent_.active || p.tpl->stage == "confrontation") return;
    double chanceOf = 0.16 + heat_ / 220.0;
    if (!r.acceptable) chanceOf += 0.35;
    if (p.tpl->kmi) chanceOf += 0.12;
    if (r.finalVerdict.card == Card::Red || r.finalVerdict.restart == Restart::Penalty) chanceOf += 0.15;
    if (setup_.derby) chanceOf += 0.08;
    if (!rng_.chance(clampd(chanceOf, 0, 0.95))) return;

    DissentEpisode d;
    d.active = true;
    d.side = 1 - r.benefitSide;
    const double roll = rng_.uniform();
    d.who = roll < 0.2 ? "coach" : roll < 0.5 ? "captain" : "player";
    d.anger = clampd(25.0 + heat_ * 0.45 + (r.acceptable ? 0.0 : 22.0) + rng_.normal(0.0, 10.0), 5.0, 100.0);
    d.abusive = d.anger > 72.0 && rng_.chance(0.55);
    d.text = content_.str("dissent." + d.who + (d.abusive ? ".abuse" : d.anger > 55 ? ".angry" : ".protest"));
    dissent_ = d;
}

std::vector<DissentResponse> MatchSession::dissentOptions() const {
    std::vector<DissentResponse> out = {DissentResponse::Ignore, DissentResponse::CalmTalk, DissentResponse::PublicWarning,
                                        DissentResponse::YellowCard};
    if (dissent_.who == "coach") out.push_back(DissentResponse::SendOffCoach);
    return out;
}

std::string MatchSession::dissentOptionLabel(DissentResponse r) const {
    static const char* keys[] = {"dissent.opt.ignore", "dissent.opt.calm", "dissent.opt.warn", "dissent.opt.yellow",
                                 "dissent.opt.sendoff"};
    return content_.str(keys[static_cast<int>(r)]);
}

DissentResult MatchSession::respondDissent(DissentResponse response) {
    DissentResult out;
    if (!dissent_.active) return out;
    const double ideal = 1.0, ok = 0.65, poor = 0.35, bad = 0.1;
    double quality = ok;
    const bool coach = dissent_.who == "coach";
    switch (response) {
        case DissentResponse::Ignore:
            quality = dissent_.abusive ? bad : dissent_.anger >= 60 ? bad : dissent_.anger >= 35 ? ok : ideal;
            break;
        case DissentResponse::CalmTalk:
            quality = dissent_.abusive ? poor : dissent_.anger >= 60 ? (setup_.stats.get(Stat::Personality) >= 60 ? ideal : ok) : ideal;
            break;
        case DissentResponse::PublicWarning:
            quality = dissent_.abusive ? ok : dissent_.anger >= 60 ? ideal : ok;
            break;
        case DissentResponse::YellowCard:
            quality = dissent_.abusive ? ideal : dissent_.anger >= 60 ? ok : dissent_.anger >= 35 ? poor : bad;
            break;
        case DissentResponse::SendOffCoach:
            quality = (coach && dissent_.abusive) ? ideal : bad;
            break;
    }
    const double successP = clampd(0.25 + 0.6 * quality + (setup_.stats.get(Stat::Personality) - 50.0) / 250.0 +
                                       (calm_ - 50.0) / 300.0,
                                   0.05, 0.97);
    out.success = rng_.chance(successP);
    dissentScoreSum_ += quality * (out.success ? 1.0 : 0.7);
    dissentCount_ += 1;
    if (out.success) {
        heat_ -= 6.0 + 8.0 * quality;
        calm_ += 4.0;
        controlPoints_ += 1.0;
    } else {
        heat_ += 8.0;
        calm_ -= 7.0 * (1.0 - setup_.stats.get(Stat::Personality) / 200.0);
        controlPoints_ -= 1.0;
    }
    heat_ = clampd(heat_, 0, 100);
    calm_ = clampd(calm_, 0, 100);
    if (response == DissentResponse::YellowCard) {
        out.cardShown = true;
        yellows_ += 1;
    }
    out.coachSentOff = response == DissentResponse::SendOffCoach;
    static const char* names[] = {"ignore", "calm", "warn", "yellow", "sendoff"};
    out.text = content_.str(std::string("dissent.res.") + names[static_cast<int>(response)] + (out.success ? ".ok" : ".fail"));
    dissent_.active = false;
    maybeConfrontation();
    return out;
}

void MatchSession::maybeConfrontation() {
    if (heat_ < 85.0 || minute_ - lastConfrontationMinute_ < 12.0) return;
    const IncidentTemplate* t = content_.incident("mass_confrontation");
    if (!t) return;
    for (const PlannedIncident& p : plan_)
        if (!p.resolved && p.dynamicConfrontation) return;
    PlannedIncident p;
    p.index = static_cast<int>(plan_.size());
    p.tpl = t;
    p.minute = minute_ + 0.3;
    p.offendingSide = rng_.irange(0, 1);
    p.dynamicConfrontation = true;
    p.onField = t->truth;
    plan_.push_back(p);
    lastConfrontationMinute_ = minute_;
    confrontations_ += 1;
}

void MatchSession::advance(double matchMinutes) {
    const double dt = std::max(0.0, matchMinutes - minute_);
    minute_ = std::max(minute_, matchMinutes);
    const double rest = tier().baseHeat + (setup_.derby ? 15.0 : 0.0);
    heat_ += (rest - heat_) * std::min(1.0, 0.02 * dt);
    if (heat_ < 60) calm_ += 0.25 * dt;
    else calm_ -= 0.3 * dt * (heat_ - 60.0) / 40.0;
    heat_ = clampd(heat_, 0, 100);
    calm_ = clampd(calm_, 0, 100);
    heatIntegral_ += heat_ * dt;
    heatTime_ += dt;
    maybeConfrontation();
}

bool MatchSession::breathe() {
    if (breaths_ <= 0) return false;
    breaths_ -= 1;
    calm_ = clampd(calm_ + 18.0, 0, 100);
    return true;
}

void MatchSession::addPositionSample(double quality01) {
    posSum_ += clampd(quality01, 0, 1);
    posCount_ += 1;
}

void MatchSession::setFitnessData(double workRate01, double exhaustedShare01) {
    workRate_ = clampd(workRate01, 0, 1);
    exhausted_ = clampd(exhaustedShare01, 0, 1);
}

void MatchSession::goalScored(int side) { score_[static_cast<size_t>(side & 1)] += 1; }

void MatchSession::cardFromPlay(Card c) {
    if (c == Card::Yellow) yellows_ += 1;
    if (c == Card::Red) reds_ += 1;
}

MatchReport MatchSession::finish() {
    MatchReport rep;
    finished_ = true;
    double wsum = 0, csum = 0;
    for (const IncidentLog& l : log_) {
        const double w = l.kmi ? 3.0 : 1.0;
        wsum += w;
        csum += w * l.credit;
        if (l.kmi) {
            rep.kmiTotal += 1;
            if (l.credit >= 0.4) rep.kmiCorrect += 1;
            else rep.kmiErrors += 1;
        }
    }
    rep.decisionScore = wsum > 0 ? csum / wsum : 0.85;
    rep.positioningScore = posCount_ ? posSum_ / posCount_ : 0.7;
    rep.avgHeat = heatTime_ > 0 ? heatIntegral_ / heatTime_ : heat_;
    rep.controlScore = clampd(0.75 + controlPoints_ * 0.05 - (rep.avgHeat - 40.0) / 150.0 - confrontations_ * 0.12, 0, 1);
    rep.fitnessScore = setup_.role == Role::Var ? 0.85 : workRate_ < 0 ? 0.8 : clampd(0.6 * workRate_ + 0.4 * (1.0 - exhausted_), 0, 1);
    rep.personalityScore = dissentCount_ ? dissentScoreSum_ / dissentCount_ : 0.75;
    rep.confrontations = confrontations_;
    rep.yellowCards = yellows_;
    rep.redCards = reds_;
    rep.score = score_;

    const double raw = 0.5 * rep.decisionScore + 0.15 * rep.positioningScore + 0.17 * rep.controlScore +
                       0.1 * rep.fitnessScore + 0.08 * rep.personalityScore;
    double mark = 6.6 + 2.4 * raw;
    if (rep.kmiErrors == 1) mark = std::min(mark, 7.9);
    else if (rep.kmiErrors == 2) mark = std::min(mark, 7.5);
    else if (rep.kmiErrors >= 3) mark = std::min(mark, 7.0);
    int hardCalls = 0;
    for (const IncidentLog& l : log_) {
        const IncidentTemplate* t = content_.incident(l.templateId);
        if (t && l.kmi && l.credit >= 0.99 && t->difficulty >= 3) hardCalls += 1;
    }
    mark = std::min(9.0, mark + 0.05 * std::min(3, hardCalls));
    rep.mark = std::round(mark * 10.0) / 10.0;
    rep.grade = content_.str(rep.mark >= 8.5 ? "grade.excellent" : rep.mark >= 8.2 ? "grade.verygood" : rep.mark >= 7.9 ? "grade.good"
                             : rep.mark >= 7.5 ? "grade.fair" : "grade.poor");

    const int diff = wrongFavor_[0] - wrongFavor_[1];
    rep.biasTowards = diff >= 2 ? 0 : diff <= -2 ? 1 : -1;
    if (setup_.fixFavorSide >= 0) rep.favorDelivered = wrongFavor_[static_cast<size_t>(setup_.fixFavorSide)];

    auto delta = [&](Stat s, double d) { rep.statDeltas[static_cast<size_t>(s)] += d; };
    // Fairness is about impartiality: it grows with clean, even-handed matches and falls when errors lean one way.
    delta(Stat::Fairness, 0.6 + (rep.decisionScore - 0.75) * 3.0 - 0.6 * rep.kmiErrors - (rep.biasTowards >= 0 ? 2.0 * std::abs(diff) : 0.0));
    delta(Stat::Personality, (rep.personalityScore - 0.65) * 4.0 + (rep.controlScore - 0.7) * 3.0 - 1.5 * confrontations_);
    delta(Stat::Trust, (rep.mark - 8.1) * 5.0 * (setup_.importance >= 4 ? 1.3 : 1.0) - 1.5 * rep.kmiErrors);
    delta(Stat::Media, -rep.kmiErrors * (setup_.importance >= 3 ? 5.0 : 3.0) + (rep.mark >= 8.4 && setup_.importance >= 3 ? 2.0 : 0.0));
    delta(Stat::Mental, -1.0 - rep.avgHeat / 25.0 - 4.0 * rep.kmiErrors + (rep.mark >= 8.4 ? 3.0 : 0.0));
    delta(Stat::Laws, 0.3);

    rep.incidents = log_;
    rep.controversies = controversies_;
    if (setup_.importance >= 4) {
        Controversy big;
        big.tag = "big";
        big.correct = rep.kmiErrors == 0;
        rep.controversies.push_back(big);
    }
    if (rep.avgHeat > 65) {
        Controversy hot;
        hot.tag = "heat";
        hot.correct = rep.confrontations == 0;
        rep.controversies.push_back(hot);
    }

    for (const IncidentLog& l : log_) {
        if (l.credit >= 0.75) continue;
        const char* key = l.correctedByVar ? "note.var_corrected" : l.acceptable ? "note.debatable" : l.kmi ? "note.kmi_wrong" : "note.wrong";
        rep.assessorNotes.push_back(content_.fmt(key, {{"minute", matchMinuteText(l.minute)},
                                                       {"name", l.name},
                                                       {"chosen", l.chosenLabel},
                                                       {"truth", l.truthLabel},
                                                       {"law", l.law}}));
    }
    if (rep.positioningScore < 0.55) rep.assessorNotes.push_back(content_.str(setup_.role == Role::Assistant ? "note.ar_line" : "note.positioning"));
    if (rep.controlScore < 0.55) rep.assessorNotes.push_back(content_.str("note.control"));
    if (rep.fitnessScore < 0.55) rep.assessorNotes.push_back(content_.str("note.fitness"));
    if (rep.biasTowards >= 0) rep.assessorNotes.push_back(content_.str("note.bias"));
    if (rep.assessorNotes.empty()) rep.assessorNotes.push_back(content_.str("note.clean"));
    return rep;
}

}  // namespace refcore
