// Plays many careers with scripted referees of different skill and prints how far they get.
// Used to tune career.json; `--check` turns the expectations into a pass/fail test.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "SimBot.h"

using namespace refcore;

namespace {

struct Summary {
    std::map<std::string, int> endings;
    std::vector<int> maxTier;
    std::vector<double> reachSeasons;  // per tier, average season first reached
    std::vector<int> reachCount;
    double avgMark = 0;
    int careers = 0;
};

Summary runBatch(const Content& content, Role role, double skill, int careers, uint64_t seedBase) {
    Summary s;
    s.maxTier.assign(content.tiers.size(), 0);
    s.reachSeasons.assign(content.tiers.size(), 0);
    s.reachCount.assign(content.tiers.size(), 0);
    for (int i = 0; i < careers; ++i) {
        const simbot::CareerOutcome o = simbot::playCareer(content, role, skill, seedBase + static_cast<uint64_t>(i) * 101u);
        s.endings[o.ending] += 1;
        s.maxTier[static_cast<size_t>(o.maxTier)] += 1;
        for (size_t t = 0; t < o.seasonReached.size(); ++t)
            if (o.seasonReached[t] > 0) { s.reachSeasons[t] += o.seasonReached[t]; s.reachCount[t] += 1; }
        s.avgMark += o.avgMark;
        s.careers += 1;
    }
    s.avgMark /= std::max(1, s.careers);
    return s;
}

void print(const Content& content, const char* label, const Summary& s) {
    std::printf("\n=== %s (%d careers) — average mark %.2f\n", label, s.careers, s.avgMark);
    std::printf("  endings:");
    for (const auto& kv : s.endings) std::printf("  %s=%d", kv.first.c_str(), kv.second);
    std::printf("\n  highest tier reached / first season reached:\n");
    for (size_t t = 0; t < content.tiers.size(); ++t)
        std::printf("    %zu %-12s max=%4d  reached=%4d  avgSeason=%.1f\n", t, content.tiers[t].id.c_str(), s.maxTier[t], s.reachCount[t],
                    s.reachCount[t] ? s.reachSeasons[t] / s.reachCount[t] : 0.0);
}

double share(const Summary& s, const std::string& ending) {
    const auto it = s.endings.find(ending);
    return it == s.endings.end() ? 0.0 : static_cast<double>(it->second) / std::max(1, s.careers);
}

}  // namespace

int main(int argc, char** argv) {
    int careers = 200;
    bool check = false;
    double traceSkill = -1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--careers") && i + 1 < argc) careers = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--check")) check = true;
        else if (!std::strcmp(argv[i], "--trace") && i + 1 < argc) traceSkill = std::atof(argv[++i]);
    }
    Content content;
    std::string err;
    if (!content.loadFromDirectory(REFCORE_DATA_DIR, &err)) {
        std::fprintf(stderr, "content: %s\n", err.c_str());
        return 2;
    }
    if (traceSkill >= 0) {
        simbot::playCareer(content, Role::Center, traceSkill, 77, [](const Career& c) {
            const SeasonReview& r = c.review();
            std::printf("season %d age %d tier %d  avg %.2f  index %.0f |", c.season(), c.age(), c.tierIndex(), r.avgMark, c.appointmentIndex());
            for (int i = 0; i < kStatCount; ++i) std::printf(" %s=%.0f", statId(static_cast<Stat>(i)), c.stats().v[static_cast<size_t>(i)]);
            std::printf("\n   %s\n", r.title.c_str());
            for (const std::string& line : r.checks) std::printf("     %s\n", line.c_str());
        });
        return 0;
    }
    const Summary expert = runBatch(content, Role::Center, 0.92, careers, 1000);
    const Summary good = runBatch(content, Role::Center, 0.7, careers, 2000);
    const Summary weak = runBatch(content, Role::Center, 0.35, careers, 3000);
    const Summary learner = runBatch(content, Role::Center, 0.5, careers, 5000);
    const Summary assistant = runBatch(content, Role::Assistant, 0.85, careers, 4000);
    print(content, "expert centre referee (skill 0.92)", expert);
    print(content, "good centre referee (skill 0.70)", good);
    print(content, "learning centre referee (skill 0.50)", learner);
    print(content, "weak centre referee (skill 0.35)", weak);
    print(content, "strong assistant referee (skill 0.85)", assistant);

    if (!check) return 0;
    int failures = 0;
    auto expect = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what);
        if (!ok) ++failures;
    };
    expect(share(expert, "unfinished") == 0 && share(good, "unfinished") == 0 && share(weak, "unfinished") == 0, "every career reaches an ending");
    expect(share(expert, "legend") >= 0.3, "experts usually referee a World Cup final");
    expect(share(good, "legend") < share(expert, "legend"), "skill matters for the World Cup final");
    expect(share(weak, "legend") <= 0.02, "weak referees almost never reach the final");
    expect(weak.maxTier[0] + weak.maxTier[1] + weak.maxTier[2] >= weak.careers / 2, "weak referees mostly stay in the lower tiers");
    expect(expert.reachCount[6] > 0 && expert.reachSeasons[6] / std::max(1, expert.reachCount[6]) >= 7.0, "the World Cup takes years to reach");
    expect(assistant.reachCount[4] > 0, "assistant referees can earn the international badge");
    return failures == 0 ? 0 : 1;
}
