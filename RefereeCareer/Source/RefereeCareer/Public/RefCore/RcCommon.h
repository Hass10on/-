// RefCore: engine-agnostic rules of the referee career (no Unreal headers here).
// The same sources build inside the Unreal module and in Tests/Core with CMake.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace refcore {

constexpr double kPi = 3.14159265358979323846;

inline double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double lerpd(double a, double b, double t) { return a + (b - a) * t; }

// The three refereeing positions the player can work in.
enum class Role : uint8_t { Center = 0, Assistant = 1, Var = 2 };
constexpr int kRoleCount = 3;
const char* roleId(Role r);
bool parseRole(const std::string& s, Role& out);

// Career indicators, all on a 0..100 scale.
// Fairness, Personality and Trust are the three reputation indicators that drive appointments.
enum class Stat : uint8_t { Fairness = 0, Personality, Trust, Fitness, Laws, Media, Mental };
constexpr int kStatCount = 7;
const char* statId(Stat s);
bool parseStat(const std::string& s, Stat& out);

struct Stats {
    std::array<double, kStatCount> v{};
    double get(Stat s) const { return v[static_cast<size_t>(s)]; }
    void set(Stat s, double x) { v[static_cast<size_t>(s)] = clampd(x, 0.0, 100.0); }
    void add(Stat s, double d) { set(s, get(s) + d); }
};

// A refereeing decision: how play restarts plus any disciplinary sanction.
enum class Restart : uint8_t { PlayOn, Advantage, FreeKick, Penalty, Offside, Goal, NoGoal };
enum class Card : uint8_t { None, Yellow, Red };
enum class CardTo : uint8_t { Offender, Victim, Both };

struct Verdict {
    Restart restart = Restart::PlayOn;
    Card card = Card::None;
    CardTo cardTo = CardTo::Offender;

    bool operator==(const Verdict& o) const {
        return restart == o.restart && card == o.card && (card == Card::None || cardTo == o.cardTo);
    }
    bool operator!=(const Verdict& o) const { return !(*this == o); }
};

// Codes used in data files and saves: play, adv, fk, fk_y, fk_r, pen, pen_y, pen_r, dive_y,
// offside, onside (= play), goal, no_goal, conf_none, conf_y, conf_y2, conf_r.
bool parseVerdict(const std::string& code, Verdict& out);
std::string verdictCode(const Verdict& v);

// Which team benefits from a verdict, given the side that committed (or is accused of) the offence.
// Returns 0/1 for the benefiting side.
int benefitingSide(const Verdict& v, int offendingSide);

}  // namespace refcore
