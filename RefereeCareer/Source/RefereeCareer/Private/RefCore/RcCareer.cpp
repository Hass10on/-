#include "RefCore/RcCareer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace refcore {

namespace {

std::string careerNum(double v, int decimals = 0) {
    char buf[32];
    if (decimals <= 0) std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
    else std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

std::string careerSigned(double v) {
    const long r = std::lround(v);
    return (r > 0 ? "+" : "") + std::to_string(r);
}

std::string careerStars(const Content& c, int importance) {
    std::string s;
    for (int i = 0; i < 5; ++i) s += c.str(i < importance ? "sym.star" : "sym.star_empty");
    return s;
}

std::string careerHex(uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

uint64_t careerParseHex(const std::string& s) { return static_cast<uint64_t>(std::strtoull(s.c_str(), nullptr, 16)); }

// Gains shrink as an indicator approaches 100 (losses do not), so reputation stays meaningful all career.
double careerDampedGain(double current, double delta) {
    if (delta <= 0) return delta;
    return delta * clampd((100.0 - current) / 60.0, 0.1, 1.0);
}

std::string careerCheckMark(const Content& c, bool ok) { return c.str(ok ? "sym.ok" : "sym.no") + " "; }

}  // namespace

const char* phaseId(Phase p) {
    switch (p) {
        case Phase::SeasonStart: return "season_start";
        case Phase::Planning: return "planning";
        case Phase::Event: return "event";
        case Phase::Appointment: return "appointment";
        case Phase::Match: return "match";
        case Phase::PostMatch: return "post_match";
        case Phase::Press: return "press";
        case Phase::SeasonReview: return "season_review";
        case Phase::Over: return "over";
    }
    return "planning";
}

Career::Career(const Content& content) : content_(content) {}

const TierDef& Career::tier() const {
    const int idx = std::max(0, std::min(tier_, static_cast<int>(content_.tiers.size()) - 1));
    return content_.tiers[static_cast<size_t>(idx)];
}

void Career::startNew(const std::string& refereeName, Role specialisation, uint64_t seed) {
    rng_.setState(seed);
    name_ = refereeName.empty() ? content_.str("default.name") : refereeName;
    specialisation_ = specialisation == Role::Var ? Role::Center : specialisation;
    preferred_ = specialisation_;
    age_ = content_.startAge;
    season_ = 1;
    week_ = 1;
    tier_ = 0;
    seasonsInTier_ = 1;
    stats_ = content_.startStats;
    money_ = 500;
    roleSkill_ = {};
    roleSkill_[static_cast<size_t>(specialisation_)] = 10;
    flags_.clear();
    seenEvents_.clear();
    progress_.clear();
    scheduled_.clear();
    eventQueue_.clear();
    history_.clear();
    news_.clear();
    matchNews_.clear();
    injured_ = suspended_ = resting_ = cooldown_ = 0;
    suspicion_ = 0;
    fixFavor_ = -1;
    fixReward_ = 0;
    fixedThisMatch_ = false;
    ending_.clear();
    totalWeeks_ = 0;
    press_.clear();
    pressIndex_ = 0;
    addNews(content_.fmt("news.start", {{"name", name_}, {"tier", tier().name}}));
    beginSeason();
}

void Career::addNews(const std::string& line) {
    news_.insert(news_.begin(), line);
    if (news_.size() > 14) news_.resize(14);
}

void Career::setEnding(const std::string& id) {
    if (ending_.empty()) ending_ = id;
}

std::string Career::teamName(int tierIdx, int team) const {
    const TierDef& t = content_.tiers[static_cast<size_t>(std::max(0, std::min(tierIdx, static_cast<int>(content_.tiers.size()) - 1)))];
    if (team < 0 || team >= static_cast<int>(t.teams.size())) return "?";
    return t.teams[static_cast<size_t>(team)].name;
}

std::string Career::fixtureLabel(const Fixture& f) const {
    std::string s = teamName(tier_, f.home) + content_.str("sym.vs") + teamName(tier_, f.away);
    if (!f.title.empty()) s = f.title + ": " + s;
    return s;
}

bool Career::meets(const Requirements& r, std::string* why) const {
    auto fail = [&](const std::string& msg) {
        if (why) *why = msg;
        return false;
    };
    if (r.minTier >= 0 && tier_ < r.minTier)
        return fail(content_.fmt("req.tier", {{"tier", content_.tiers[static_cast<size_t>(std::min(r.minTier, static_cast<int>(content_.tiers.size()) - 1))].name}}));
    if (r.maxTier >= 0 && tier_ > r.maxTier) return fail(content_.str("req.maxtier"));
    if (season_ < r.minSeason) return fail(content_.str("req.season"));
    if (money_ < r.minMoney) return fail(content_.fmt("req.money", {{"money", careerNum(r.minMoney)}}));
    for (const auto& ms : r.minStats) {
        if (stats_.get(ms.first) < ms.second)
            return fail(content_.fmt("req.stat", {{"stat", content_.str(std::string("stat.") + statId(ms.first))},
                                                  {"value", careerNum(ms.second)}}));
    }
    for (const std::string& f : r.flagsAll)
        if (!hasFlag(f)) return fail(content_.str("req.flag." + f));
    for (const std::string& f : r.flagsNone)
        if (hasFlag(f)) return fail(content_.str("req.noflag"));
    if (!r.roles.empty() && std::find(r.roles.begin(), r.roles.end(), specialisation_) == r.roles.end())
        return fail(content_.str("req.role"));
    return true;
}

void Career::applyEffects(const Effects& e, std::vector<std::string>* log) {
    for (int i = 0; i < kStatCount; ++i) {
        const double d = e.stats[static_cast<size_t>(i)];
        if (d == 0) continue;
        const Stat s = static_cast<Stat>(i);
        const double applied = careerDampedGain(stats_.get(s), d);
        stats_.add(s, applied);
        if (log) log->push_back(content_.str(std::string("stat.") + statId(s)) + " " + careerSigned(applied));
    }
    if (e.money != 0) {
        money_ += e.money;
        if (log) log->push_back(content_.str("stat.money") + " " + careerSigned(e.money));
    }
    if (e.roleSkill != 0) {
        double& rs = roleSkill_[static_cast<size_t>(preferred_)];
        rs = clampd(rs + e.roleSkill, 0, 100);
    }
    suspicion_ = clampd(suspicion_ + e.suspicion, 0, 100);
    for (const std::string& f : e.setFlags) flags_.insert(f);
    for (const std::string& f : e.clearFlags) flags_.erase(f);
    if (e.suspendWeeks > 0) suspended_ = std::max(suspended_, e.suspendWeeks);
    if (e.injuryWeeks > 0) injured_ = std::max(injured_, e.injuryWeeks);
    if (e.restWeeks > 0) resting_ = std::max(resting_, e.restWeeks);
    if (!e.fix.empty()) {
        fixFavor_ = e.fix == "home" ? 0 : e.fix == "away" ? 1 : rng_.irange(0, 1);
        fixReward_ = e.fixReward;
    }
    if (!e.scheduleEvent.empty()) scheduled_.emplace_back(e.scheduleEvent, totalWeeks_ + std::max(1, e.scheduleInWeeks));
    if (!e.ending.empty()) setEnding(e.ending);
}

int Career::seasonMatches() const {
    int n = 0;
    for (const HistoryEntry& h : history_)
        if (h.season == season_) ++n;
    return n;
}

double Career::seasonAverage() const {
    double s = 0;
    int n = 0;
    for (const HistoryEntry& h : history_)
        if (h.season == season_) { s += h.mark; ++n; }
    return n ? s / n : 0.0;
}

double Career::recentAverage(int n) const {
    double s = 0;
    int c = 0;
    for (auto it = history_.rbegin(); it != history_.rend() && c < n; ++it, ++c) s += it->mark;
    return c ? s / c : 0.0;
}

double Career::appointmentIndex() const {
    const AppointmentRules& ar = content_.appointment;
    double score = 0;
    for (int i = 0; i < kStatCount; ++i) score += ar.weights[static_cast<size_t>(i)] * stats_.v[static_cast<size_t>(i)];
    const double markComponent = history_.empty() ? 50.0 : clampd((recentAverage(5) - 7.0) * 50.0, 0, 100);
    score += ar.markWeight * markComponent;
    return clampd(score, 0, 100);
}

bool Career::canWorkAs(Role r, std::string* why) const {
    const TierDef& t = tier();
    if (r == Role::Var) {
        if (!t.hasVar) { if (why) *why = content_.str("role.var.no_var"); return false; }
        if (!hasFlag("var_certified")) { if (why) *why = content_.str("role.var.no_cert"); return false; }
        return true;
    }
    if (std::find(t.roles.begin(), t.roles.end(), r) == t.roles.end()) {
        if (why) *why = content_.str("role.not_in_tier");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- season & week

void Career::beginSeason() {
    const TierDef& t = tier();
    const double test = stats_.get(Stat::Fitness) + rng_.normal(0.0, 4.0);
    std::string result;
    if (test >= t.fitnessTest) {
        result = content_.fmt("season.fitness_pass", {{"score", careerNum(test)}, {"need", careerNum(t.fitnessTest)}});
    } else {
        suspended_ = std::max(suspended_, 2);
        stats_.add(Stat::Trust, -6);
        result = content_.fmt("season.fitness_fail", {{"score", careerNum(test)}, {"need", careerNum(t.fitnessTest)}});
    }
    seasonIntro_ = content_.fmt("season.intro", {{"season", std::to_string(season_)},
                                                 {"age", std::to_string(age_)},
                                                 {"tier", t.name},
                                                 {"competition", t.competition}}) +
                   "\n\n" + result;
    phase_ = Phase::SeasonStart;
}

void Career::confirmSeasonStart() {
    if (phase_ != Phase::SeasonStart) return;
    beginWeek();
}

void Career::beginWeek() {
    phase_ = Phase::Planning;
    appointment_ = Appointment();
    eventQueue_.clear();
    matchNews_.clear();
    press_.clear();
    pressIndex_ = 0;
}

std::vector<ActivityStatus> Career::activities() const {
    std::vector<ActivityStatus> out;
    for (const ActivityDef& a : content_.activities) {
        ActivityStatus st;
        st.def = &a;
        std::string why;
        if (!a.progressFlag.empty() && hasFlag(a.progressFlag)) {
            st.reason = content_.str("act.done");
        } else if (injured_ > 0 && !a.allowedWhileInjured) {
            st.reason = content_.str("act.injured");
        } else if (money_ < a.cost) {
            st.reason = content_.fmt("req.money", {{"money", careerNum(a.cost)}});
        } else if (!meets(a.req, &why)) {
            st.reason = why;
        } else {
            st.available = true;
        }
        out.push_back(st);
    }
    return out;
}

std::vector<std::string> Career::commitWeek(const std::vector<std::string>& activityIds, Role preferred) {
    std::vector<std::string> log;
    if (phase_ != Phase::Planning) return log;
    preferred_ = canWorkAs(preferred) ? preferred : (canWorkAs(specialisation_) ? specialisation_ : Role::Center);

    const std::vector<ActivityStatus> status = activities();
    std::map<std::string, int> repeats;
    bool trainedFitness = false;
    int used = 0;
    for (const std::string& id : activityIds) {
        if (used >= slots()) break;
        const ActivityStatus* st = nullptr;
        for (const ActivityStatus& s : status)
            if (s.def->id == id) st = &s;
        if (!st || !st->available || money_ < st->def->cost) continue;
        ++used;
        const ActivityDef& a = *st->def;
        const int rep = repeats[id]++;
        Effects e = a.effects;
        const double scale = rep == 0 ? 1.0 : rep == 1 ? 0.7 : 0.45;
        for (double& d : e.stats)
            if (d > 0) d *= scale;  // repeating the same activity in one week helps less
        log.push_back(content_.str("sym.bullet") + a.name);
        applyEffects(e, &log);
        money_ -= a.cost;
        if (e.stats[static_cast<size_t>(Stat::Fitness)] > 0) trainedFitness = true;
        if (a.injuryRisk > 0 && rng_.chance(a.injuryRisk * (1.3 - stats_.get(Stat::Fitness) / 100.0))) {
            injured_ = std::max(injured_, rng_.irange(1, 3));
            log.push_back(content_.fmt("act.injury", {{"weeks", std::to_string(injured_)}}));
            addNews(content_.str("news.injury"));
        }
        if (!a.progressKey.empty()) {
            const int p = ++progress_[a.progressKey];
            if (a.progressNeeded > 0 && p >= a.progressNeeded && !a.progressFlag.empty() && !hasFlag(a.progressFlag)) {
                flags_.insert(a.progressFlag);
                log.push_back(content_.str("act.complete." + a.progressFlag));
                addNews(content_.str("act.complete." + a.progressFlag));
            } else if (a.progressNeeded > 0 && p < a.progressNeeded) {
                log.push_back(content_.fmt("act.progress", {{"done", std::to_string(p)}, {"need", std::to_string(a.progressNeeded)}}));
            }
        }
    }
    // Passive drift every week.
    if (!trainedFitness) stats_.add(Stat::Fitness, -1.0);
    stats_.add(Stat::Media, (50.0 - stats_.get(Stat::Media)) * 0.03);
    stats_.add(Stat::Mental, 1.0);

    rollEvents();
    openNextEventOrAppointment();
    return log;
}

void Career::rollEvents() {
    eventQueue_.clear();
    for (auto it = scheduled_.begin(); it != scheduled_.end();) {
        if (it->second <= totalWeeks_) {
            eventQueue_.push_back(it->first);
            it = scheduled_.erase(it);
        } else {
            ++it;
        }
    }
    if (hasFlag("corrupt") && !hasFlag("exposed") && rng_.chance(suspicion_ / 250.0)) {
        eventQueue_.push_back("integrity_exposed");
    }
    if (!eventQueue_.empty()) return;

    std::vector<const EventDef*> pool;
    std::vector<double> w;
    double total = 0;
    for (const EventDef& e : content_.events) {
        if (e.chainOnly) continue;
        if (e.once && seenEvents_.count(e.id)) continue;
        if (!meets(e.req, nullptr)) continue;
        pool.push_back(&e);
        w.push_back(e.chance);
        total += e.chance;
    }
    if (pool.empty() || !rng_.chance(std::min(0.6, total))) return;
    const int pick = rng_.weighted(w);
    if (pick >= 0) eventQueue_.push_back(pool[static_cast<size_t>(pick)]->id);
}

void Career::openNextEventOrAppointment() {
    if (!ending_.empty()) {
        phase_ = Phase::Over;
        return;
    }
    while (!eventQueue_.empty() && !content_.event(eventQueue_.front())) eventQueue_.erase(eventQueue_.begin());
    if (!eventQueue_.empty()) {
        phase_ = Phase::Event;
        return;
    }
    computeAppointment();
    phase_ = Phase::Appointment;
}

const EventDef* Career::currentEvent() const {
    if (phase_ != Phase::Event || eventQueue_.empty()) return nullptr;
    return content_.event(eventQueue_.front());
}

std::vector<EventChoiceStatus> Career::eventChoices() const {
    std::vector<EventChoiceStatus> out;
    const EventDef* e = currentEvent();
    if (!e) return out;
    for (const EventChoiceDef& c : e->choices) {
        EventChoiceStatus s;
        s.available = meets(c.req, &s.reason);
        out.push_back(s);
    }
    return out;
}

std::string Career::chooseEvent(int choice) {
    const EventDef* e = currentEvent();
    if (!e || choice < 0 || choice >= static_cast<int>(e->choices.size())) return std::string();
    const EventChoiceDef& c = e->choices[static_cast<size_t>(choice)];
    if (!meets(c.req, nullptr)) return std::string();
    std::vector<std::string> log;
    applyEffects(c.effects, &log);
    seenEvents_.insert(e->id);
    std::string text = Content::substitute(c.result, {{"name", name_}});
    if (!log.empty()) {
        text += "\n";
        for (const std::string& l : log) text += "\n" + l;
    }
    eventQueue_.erase(eventQueue_.begin());
    openNextEventOrAppointment();
    return text;
}

// ---------------------------------------------------------------- appointments

void Career::computeAppointment() {
    appointment_ = Appointment();
    const TierDef& t = tier();
    const AppointmentRules& ar = content_.appointment;
    appointment_.score = appointmentIndex();

    if (injured_ > 0) { appointment_.reason = content_.fmt("app.injured", {{"weeks", std::to_string(injured_)}}); return; }
    if (suspended_ > 0) { appointment_.reason = content_.fmt("app.suspended", {{"weeks", std::to_string(suspended_)}}); return; }
    if (resting_ > 0) { appointment_.reason = content_.str("app.resting"); return; }
    if (stats_.get(Stat::Trust) < 12) { appointment_.reason = content_.str("app.notrust"); return; }

    // This week's fixtures in the referee's competition.
    std::vector<Fixture> fixtures;
    const int teamCount = static_cast<int>(t.teams.size());
    auto randomPair = [&](Fixture& f) {
        f.home = rng_.irange(0, teamCount - 1);
        do { f.away = rng_.irange(0, teamCount - 1); } while (f.away == f.home);
    };
    for (const BigMatchDef& bm : t.bigMatches) {
        if (bm.week == week_ || (bm.week < 0 && rng_.chance(bm.chance))) {
            Fixture f;
            randomPair(f);
            if (bm.home >= 0 && bm.away >= 0 && bm.home < teamCount && bm.away < teamCount && bm.home != bm.away) {
                f.home = bm.home;
                f.away = bm.away;
            }
            f.importance = bm.importance;
            f.minScore = bm.minScore;
            f.minForm = bm.minForm;
            f.derby = bm.derby;
            f.title = bm.name;
            f.flag = bm.flag;
            fixtures.push_back(f);
        }
    }
    const int regular = std::min(5, std::max(2, teamCount / 2));
    for (int i = 0; i < regular; ++i) {
        Fixture f;
        randomPair(f);
        const double r = rng_.uniform();
        f.importance = r < 0.5 ? 1 : r < 0.83 ? 2 : 3;
        fixtures.push_back(f);
    }
    std::stable_sort(fixtures.begin(), fixtures.end(), [](const Fixture& a, const Fixture& b) { return a.importance > b.importance; });

    // Weakest weighted indicator, used to explain a missed nomination.
    Stat weakest = Stat::Trust;
    double weakestVal = 1e9;
    for (int i = 0; i < kStatCount; ++i) {
        if (ar.weights[static_cast<size_t>(i)] <= 0) continue;
        if (stats_.v[static_cast<size_t>(i)] < weakestVal) {
            weakestVal = stats_.v[static_cast<size_t>(i)];
            weakest = static_cast<Stat>(i);
        }
    }

    auto unmet = [&](const Fixture& f) -> std::string {
        const int imp = std::max(1, std::min(f.importance, static_cast<int>(ar.thresholds.size())));
        const double thr = std::max(ar.thresholds[static_cast<size_t>(imp - 1)], f.minScore);
        const std::string label = fixtureLabel(f);
        if (cooldown_ > 0 && f.importance > 2) return content_.fmt("app.blocked.cooldown", {{"fixture", label}});
        if (appointment_.score < thr)
            return content_.fmt("app.blocked.score", {{"fixture", label},
                                                      {"score", careerNum(appointment_.score)},
                                                      {"need", careerNum(thr)},
                                                      {"weak", content_.str(std::string("stat.") + statId(weakest))},
                                                      {"weakval", careerNum(weakestVal)}});
        if (f.minForm > 0 && (history_.size() < 4 || recentAverage(4) < f.minForm))
            return content_.fmt("app.blocked.form", {{"fixture", label},
                                                     {"form", careerNum(recentAverage(4), 2)},
                                                     {"need", careerNum(f.minForm, 2)}});
        if (f.importance >= 4 && stats_.get(Stat::Fairness) < ar.bigMinFairness)
            return content_.fmt("app.blocked.stat", {{"fixture", label}, {"stat", content_.str("stat.fairness")},
                                                     {"value", careerNum(stats_.get(Stat::Fairness))}, {"need", careerNum(ar.bigMinFairness)}});
        if (f.importance >= 4 && stats_.get(Stat::Personality) < ar.bigMinPersonality)
            return content_.fmt("app.blocked.stat", {{"fixture", label}, {"stat", content_.str("stat.personality")},
                                                     {"value", careerNum(stats_.get(Stat::Personality))}, {"need", careerNum(ar.bigMinPersonality)}});
        return std::string();
    };

    int chosen = -1;
    for (size_t i = 0; i < fixtures.size(); ++i) {
        if (unmet(fixtures[i]).empty()) { chosen = static_cast<int>(i); break; }
    }
    if (chosen < 0) {
        appointment_.reason = content_.str("app.none");
        appointment_.blocked = fixtures.empty() ? std::string() : unmet(fixtures.front());
        return;
    }
    bool rotated = false;
    if (fixtures[static_cast<size_t>(chosen)].importance <= 3 && chosen + 1 < static_cast<int>(fixtures.size()) &&
        fixtures[static_cast<size_t>(chosen + 1)].importance < fixtures[static_cast<size_t>(chosen)].importance &&
        rng_.chance(ar.rotationChance)) {
        ++chosen;
        rotated = true;
    }
    for (int i = 0; i < chosen; ++i) {
        const std::string why = unmet(fixtures[static_cast<size_t>(i)]);
        if (!why.empty()) { appointment_.blocked = why; break; }
    }
    if (rotated && appointment_.blocked.empty()) appointment_.blocked = content_.str("app.rotation");

    Role role = preferred_;
    if (!canWorkAs(role)) role = canWorkAs(specialisation_) ? specialisation_ : Role::Center;
    appointment_.appointed = true;
    appointment_.fixture = fixtures[static_cast<size_t>(chosen)];
    appointment_.role = role;
    appointment_.reason = content_.fmt("app.reason", {{"fixture", fixtureLabel(appointment_.fixture)},
                                                      {"stars", careerStars(content_, appointment_.fixture.importance)},
                                                      {"score", careerNum(appointment_.score)},
                                                      {"role", content_.str(std::string("role.") + roleId(role))}});
    for (size_t i = 0; i < fixtures.size(); ++i)
        if (static_cast<int>(i) != chosen)
            appointment_.otherFixtures.push_back(careerStars(content_, fixtures[i].importance) + "  " + fixtureLabel(fixtures[i]));
}

MatchSetup Career::matchSetup() const {
    MatchSetup s;
    s.tier = tier_;
    s.importance = appointment_.fixture.importance;
    s.derby = appointment_.fixture.derby;
    s.role = appointment_.role;
    s.home = appointment_.fixture.home;
    s.away = appointment_.fixture.away;
    s.fixtureName = fixtureLabel(appointment_.fixture);
    s.seed = Rng::mix(rng_.state(), static_cast<uint64_t>(totalWeeks_) + 7u);
    s.stats = stats_;
    s.roleSkill = roleSkill_[static_cast<size_t>(appointment_.role)];
    s.fixFavorSide = fixFavor_;
    return s;
}

void Career::acceptAppointment() {
    if (phase_ != Phase::Appointment) return;
    if (appointment_.appointed) {
        fixedThisMatch_ = fixFavor_ >= 0;
        phase_ = Phase::Match;
    } else {
        endWeek();
    }
}

// ---------------------------------------------------------------- after the match

void Career::completeMatch(const MatchReport& report) {
    if (phase_ != Phase::Match) return;
    lastReport_ = report;
    matchNews_.clear();
    for (int i = 0; i < kStatCount; ++i) {
        const Stat st = static_cast<Stat>(i);
        stats_.add(st, careerDampedGain(stats_.get(st), report.statDeltas[static_cast<size_t>(i)]));
    }
    double& rs = roleSkill_[static_cast<size_t>(appointment_.role)];
    rs = clampd(rs + 2.0, 0, 100);
    const TierDef& t = tier();
    const double fee = t.fee * (appointment_.fixture.importance >= 4 ? 2.0 : 1.0) * (appointment_.role == Role::Center ? 1.0 : 0.6);
    money_ += fee;

    HistoryEntry h;
    h.season = season_;
    h.week = week_;
    h.tier = tier_;
    h.role = appointment_.role;
    h.fixture = fixtureLabel(appointment_.fixture);
    h.importance = appointment_.fixture.importance;
    h.mark = report.mark;
    h.kmiErrors = report.kmiErrors;
    history_.push_back(h);

    if (report.kmiErrors > 0) cooldown_ = content_.appointment.cooldownWeeks;
    if (!appointment_.fixture.flag.empty()) flags_.insert(appointment_.fixture.flag);

    if (fixedThisMatch_) {
        if (report.favorDelivered >= 1) {
            money_ += fixReward_;
            suspicion_ = clampd(suspicion_ + 15.0 + 10.0 * report.favorDelivered, 0, 100);
            stats_.add(Stat::Fairness, -8);
            flags_.insert("corrupt");
            matchNews_.push_back(content_.str("news.fix_delivered"));
        } else {
            flags_.insert("fix_betrayed");
            scheduled_.emplace_back("fixer_angry", totalWeeks_ + 1);
            matchNews_.push_back(content_.str("news.fix_refused_on_pitch"));
        }
        fixFavor_ = -1;
        fixReward_ = 0;
        fixedThisMatch_ = false;
    }

    const std::string fx = fixtureLabel(appointment_.fixture);
    if (report.kmiErrors >= 2) matchNews_.push_back(content_.fmt("news.howlers", {{"fixture", fx}}));
    else if (report.kmiErrors == 1) matchNews_.push_back(content_.fmt("news.howler", {{"fixture", fx}}));
    else if (report.mark >= 8.5) matchNews_.push_back(content_.fmt("news.great", {{"fixture", fx}, {"name", name_}}));
    else matchNews_.push_back(content_.fmt("news.normal", {{"fixture", fx}}));
    if (report.confrontations > 0) matchNews_.push_back(content_.str("news.confrontation"));
    if (report.redCards > 0) matchNews_.push_back(content_.fmt("news.reds", {{"count", std::to_string(report.redCards)}}));
    for (const std::string& n : matchNews_) addNews(n);

    buildPress(report);
    phase_ = Phase::PostMatch;
}

void Career::buildPress(const MatchReport& rep) {
    press_.clear();
    pressIndex_ = 0;
    std::vector<Controversy> list = rep.controversies;
    std::stable_sort(list.begin(), list.end(), [](const Controversy& a, const Controversy& b) {
        auto rank = [](const std::string& tag) { return tag == "kmi" || tag == "var" ? 0 : tag == "red" || tag == "penalty" ? 1 : 2; };
        return rank(a.tag) < rank(b.tag);
    });
    for (const Controversy& c : list) {
        if (press_.size() >= 3) break;
        std::vector<const PressQuestionDef*> pool;
        for (const PressQuestionDef& q : content_.press)
            if (q.tag == c.tag) pool.push_back(&q);
        if (pool.empty()) continue;
        const PressQuestionDef* q = pool[static_cast<size_t>(rng_.irange(0, static_cast<int>(pool.size()) - 1))];
        PressQuestion pq;
        pq.tag = c.tag;
        pq.hasTruth = true;
        pq.correct = c.correct;
        pq.text = Content::substitute(q->text, {{"minute", std::to_string(static_cast<int>(c.minute) + 1)},
                                               {"decision", c.chosenLabel},
                                               {"name", c.name},
                                               {"fixture", fixtureLabel(appointment_.fixture)}});
        pq.answers = q->answers;
        press_.push_back(pq);
    }
}

void Career::continueAfterReport() {
    if (phase_ != Phase::PostMatch) return;
    if (!press_.empty()) {
        phase_ = Phase::Press;
        pressIndex_ = 0;
    } else {
        endWeek();
    }
}

std::string Career::answerPress(int answer) {
    if (phase_ != Phase::Press || pressIndex_ >= static_cast<int>(press_.size())) return std::string();
    const PressQuestion& q = press_[static_cast<size_t>(pressIndex_)];
    if (answer < 0 || answer >= static_cast<int>(q.answers.size())) return std::string();
    const std::string& type = q.answers[static_cast<size_t>(answer)].type;
    std::vector<std::string> log;
    std::string text;
    const auto it = content_.pressRules.find(type);
    if (it != content_.pressRules.end()) {
        Effects e = !q.hasTruth ? it->second.neutral : q.correct ? it->second.correct : it->second.wrong;
        const double scale = 0.8 + 0.1 * appointment_.fixture.importance;
        for (double& d : e.stats) d *= scale;
        applyEffects(e, &log);
    }
    text = content_.str("press.res." + type + (q.correct ? ".correct" : ".wrong"));
    for (const std::string& l : log) text += "\n" + l;
    ++pressIndex_;
    if (pressIndex_ >= static_cast<int>(press_.size())) endWeek();
    return text;
}

void Career::endWeek() {
    if (injured_ > 0) { --injured_; stats_.add(Stat::Fitness, -2); }
    if (suspended_ > 0) --suspended_;
    if (resting_ > 0) --resting_;
    if (cooldown_ > 0) --cooldown_;

    if (stats_.get(Stat::Mental) <= 5) {
        const int burnouts = ++progress_["burnouts"];
        if (burnouts >= 3) {
            setEnding("burnout");
        } else {
            resting_ = std::max(resting_, 2);
            stats_.add(Stat::Trust, -4);
            stats_.add(Stat::Mental, 25);
            addNews(content_.str("news.burnout"));
        }
    }
    if (tier_ == 0 && stats_.get(Stat::Trust) <= 3) setEnding("dismissed");
    if (hasFlag("wc_final_done")) setEnding("legend");

    ++totalWeeks_;
    ++week_;
    if (!ending_.empty()) {
        phase_ = Phase::Over;
        return;
    }
    if (week_ > content_.weeksPerSeason) {
        buildReview();
        phase_ = Phase::SeasonReview;
        return;
    }
    beginWeek();
}

void Career::buildReview() {
    SeasonReview r;
    const TierDef& t = tier();
    const PromoteRule& p = t.promote;
    r.matches = seasonMatches();
    r.avgMark = seasonAverage();
    r.fromTier = r.toTier = tier_;

    struct Check { bool ok; std::string text; };
    std::vector<Check> checks;
    if (p.minSeasons > 1)
        checks.push_back({seasonsInTier_ >= p.minSeasons, content_.fmt("review.seasons", {{"have", std::to_string(seasonsInTier_)}, {"need", std::to_string(p.minSeasons)}})});
    checks.push_back({r.matches >= p.minMatches, content_.fmt("review.matches", {{"have", std::to_string(r.matches)}, {"need", std::to_string(p.minMatches)}})});
    checks.push_back({r.avgMark >= p.minAvgMark, content_.fmt("review.mark", {{"have", careerNum(r.avgMark, 2)}, {"need", careerNum(p.minAvgMark, 2)}})});
    auto statCheck = [&](Stat s, double need) {
        if (need <= 0) return;
        checks.push_back({stats_.get(s) >= need, content_.fmt("review.stat", {{"stat", content_.str(std::string("stat.") + statId(s))},
                                                                              {"have", careerNum(stats_.get(s))},
                                                                              {"need", careerNum(need)}})});
    };
    statCheck(Stat::Trust, p.minTrust);
    statCheck(Stat::Fairness, p.minFairness);
    statCheck(Stat::Personality, p.minPersonality);
    statCheck(Stat::Fitness, p.minFitness);

    bool allOk = true;
    for (const Check& c : checks) {
        allOk = allOk && c.ok;
        r.checks.push_back(careerCheckMark(content_, c.ok) + c.text);
    }
    const bool lastTier = tier_ + 1 >= static_cast<int>(content_.tiers.size());
    if (t.canPromote && !lastTier && allOk && !hasFlag("corrupt_suspended")) {
        r.promoted = true;
        r.toTier = tier_ + 1;
        r.title = content_.str("review.promoted.title");
        r.text = content_.fmt("review.promoted", {{"tier", content_.tiers[static_cast<size_t>(r.toTier)].name}});
    } else if (tier_ > 0 && ((r.matches > 0 && r.avgMark < t.demoteBelowMark) || stats_.get(Stat::Trust) < t.demoteBelowTrust)) {
        r.demoted = true;
        r.toTier = tier_ - 1;
        r.title = content_.str("review.demoted.title");
        r.text = content_.fmt("review.demoted", {{"tier", content_.tiers[static_cast<size_t>(r.toTier)].name}});
    } else if (tier_ == 0 && stats_.get(Stat::Trust) < t.demoteBelowTrust) {
        r.title = content_.str("review.dismissed.title");
        r.text = content_.str("review.dismissed");
        setEnding("dismissed");
    } else if (t.maxSeasons > 0 && seasonsInTier_ >= t.maxSeasons) {
        r.title = content_.str("review.last.title");
        r.text = content_.fmt("review.last", {{"tier", t.name}});
        setEnding("retired_international");
    } else {
        r.title = content_.str("review.stay.title");
        r.text = content_.fmt(lastTier ? "review.top" : "review.stay", {{"tier", t.name}});
    }
    review_ = r;
}

void Career::continueAfterReview() {
    if (phase_ != Phase::SeasonReview) return;
    if (review_.promoted) {
        addNews(content_.fmt("news.promoted", {{"name", name_}, {"tier", content_.tiers[static_cast<size_t>(review_.toTier)].name}}));
        flags_.insert("reached_" + content_.tiers[static_cast<size_t>(review_.toTier)].id);
    }
    if (review_.demoted) addNews(content_.fmt("news.demoted", {{"name", name_}, {"tier", content_.tiers[static_cast<size_t>(review_.toTier)].name}}));
    seasonsInTier_ = review_.toTier == tier_ ? seasonsInTier_ + 1 : 1;
    tier_ = review_.toTier;
    ++age_;
    ++season_;
    week_ = 1;
    if (!canWorkAs(preferred_)) preferred_ = canWorkAs(specialisation_) ? specialisation_ : Role::Center;
    if (age_ > content_.retireAge) setEnding(tier_ >= 4 ? "retired_international" : "retired");
    if (!ending_.empty()) {
        phase_ = Phase::Over;
        return;
    }
    beginSeason();
}

std::string Career::endingTitle() const {
    const EndingDef* e = content_.ending(ending_);
    return e ? Content::substitute(e->title, {{"name", name_}}) : ending_;
}

std::string Career::endingText() const {
    const EndingDef* e = content_.ending(ending_);
    double sum = 0;
    int best = 0;
    for (const HistoryEntry& h : history_) {
        sum += h.mark;
        best = std::max(best, h.tier);
    }
    const std::string summary = content_.fmt("ending.summary", {{"matches", std::to_string(history_.size())},
                                                                {"avg", careerNum(history_.empty() ? 0.0 : sum / history_.size(), 2)},
                                                                {"tier", content_.tiers[static_cast<size_t>(best)].name},
                                                                {"seasons", std::to_string(season_)}});
    return (e ? Content::substitute(e->text, {{"name", name_}}) : std::string()) + "\n\n" + summary;
}

// ---------------------------------------------------------------- save / load

Json Career::toJson() const {
    Json j = Json::makeObject();
    j.set("version", 1);
    j.set("rng", careerHex(rng_.state()));
    j.set("phase", phaseId(phase_));
    j.set("name", name_);
    j.set("specialisation", roleId(specialisation_));
    j.set("preferred", roleId(preferred_));
    j.set("age", age_);
    j.set("season", season_);
    j.set("week", week_);
    j.set("tier", tier_);
    j.set("seasonsInTier", seasonsInTier_);
    j.set("money", money_);
    j.set("totalWeeks", totalWeeks_);
    Json st = Json::makeObject();
    for (int i = 0; i < kStatCount; ++i) st.set(statId(static_cast<Stat>(i)), stats_.v[static_cast<size_t>(i)]);
    j.set("stats", st);
    Json rs = Json::makeArray();
    for (double v : roleSkill_) rs.push(v);
    j.set("roleSkill", rs);
    Json flags = Json::makeArray();
    for (const std::string& f : flags_) flags.push(f);
    j.set("flags", flags);
    Json seen = Json::makeArray();
    for (const std::string& f : seenEvents_) seen.push(f);
    j.set("seenEvents", seen);
    Json prog = Json::makeObject();
    for (const auto& kv : progress_) prog.set(kv.first, kv.second);
    j.set("progress", prog);
    Json sched = Json::makeArray();
    for (const auto& s : scheduled_) {
        Json o = Json::makeObject();
        o.set("event", s.first);
        o.set("week", s.second);
        sched.push(o);
    }
    j.set("scheduled", sched);
    Json queue = Json::makeArray();
    for (const std::string& e : eventQueue_) queue.push(e);
    j.set("eventQueue", queue);
    Json hist = Json::makeArray();
    for (const HistoryEntry& h : history_) {
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
    Json news = Json::makeArray();
    for (const std::string& n : news_) news.push(n);
    j.set("news", news);
    j.set("injured", injured_);
    j.set("suspended", suspended_);
    j.set("resting", resting_);
    j.set("cooldown", cooldown_);
    j.set("suspicion", suspicion_);
    j.set("fixFavor", fixFavor_);
    j.set("fixReward", fixReward_);
    j.set("seasonIntro", seasonIntro_);
    j.set("ending", ending_);
    return j;
}

bool Career::fromJson(const Json& j, std::string* error) {
    if (!j.isObject() || j["version"].asInt(0) != 1) {
        if (error) *error = "unsupported save";
        return false;
    }
    rng_.setState(careerParseHex(j["rng"].asString("1")));
    name_ = j["name"].asString();
    parseRole(j["specialisation"].asString("center"), specialisation_);
    parseRole(j["preferred"].asString("center"), preferred_);
    age_ = j["age"].asInt(content_.startAge);
    season_ = j["season"].asInt(1);
    week_ = j["week"].asInt(1);
    tier_ = std::max(0, std::min(j["tier"].asInt(0), static_cast<int>(content_.tiers.size()) - 1));
    seasonsInTier_ = j["seasonsInTier"].asInt(1);
    money_ = j["money"].asNumber(0);
    totalWeeks_ = j["totalWeeks"].asInt(0);
    for (int i = 0; i < kStatCount; ++i)
        stats_.set(static_cast<Stat>(i), j["stats"][statId(static_cast<Stat>(i))].asNumber(50));
    for (size_t i = 0; i < roleSkill_.size(); ++i) roleSkill_[i] = j["roleSkill"].at(i).asNumber(0);
    flags_.clear();
    for (const Json& f : j["flags"].items()) flags_.insert(f.asString());
    seenEvents_.clear();
    for (const Json& f : j["seenEvents"].items()) seenEvents_.insert(f.asString());
    progress_.clear();
    for (const auto& kv : j["progress"].members()) progress_[kv.first] = kv.second.asInt();
    scheduled_.clear();
    for (const Json& s : j["scheduled"].items()) scheduled_.emplace_back(s["event"].asString(), s["week"].asInt());
    eventQueue_.clear();
    for (const Json& e : j["eventQueue"].items()) eventQueue_.push_back(e.asString());
    history_.clear();
    for (const Json& o : j["history"].items()) {
        HistoryEntry h;
        h.season = o["season"].asInt();
        h.week = o["week"].asInt();
        h.tier = o["tier"].asInt();
        parseRole(o["role"].asString("center"), h.role);
        h.fixture = o["fixture"].asString();
        h.importance = o["importance"].asInt(1);
        h.mark = o["mark"].asNumber();
        h.kmiErrors = o["kmiErrors"].asInt();
        history_.push_back(h);
    }
    news_.clear();
    for (const Json& n : j["news"].items()) news_.push_back(n.asString());
    injured_ = j["injured"].asInt();
    suspended_ = j["suspended"].asInt();
    resting_ = j["resting"].asInt();
    cooldown_ = j["cooldown"].asInt();
    suspicion_ = j["suspicion"].asNumber();
    fixFavor_ = j["fixFavor"].asInt(-1);
    fixReward_ = j["fixReward"].asNumber();
    seasonIntro_ = j["seasonIntro"].asString();
    ending_ = j["ending"].asString();
    fixedThisMatch_ = false;

    // Saves are written between screens; resume at the start of the stored phase.
    const std::string ph = j["phase"].asString("planning");
    if (!ending_.empty()) phase_ = Phase::Over;
    else if (ph == "season_start") phase_ = Phase::SeasonStart;
    else if (ph == "season_review") { buildReview(); phase_ = Phase::SeasonReview; }
    else if (ph == "event" && !eventQueue_.empty()) phase_ = Phase::Event;
    else beginWeek();  // planning / appointment / match / press resume at weekly planning
    return true;
}

}  // namespace refcore
