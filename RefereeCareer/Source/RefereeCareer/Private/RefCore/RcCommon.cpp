#include "RefCore/RcCommon.h"

namespace refcore {

const char* roleId(Role r) {
    switch (r) {
        case Role::Center: return "center";
        case Role::Assistant: return "assistant";
        case Role::Var: return "var";
    }
    return "center";
}

bool parseRole(const std::string& s, Role& out) {
    if (s == "center") { out = Role::Center; return true; }
    if (s == "assistant") { out = Role::Assistant; return true; }
    if (s == "var") { out = Role::Var; return true; }
    return false;
}

const char* statId(Stat s) {
    switch (s) {
        case Stat::Fairness: return "fairness";
        case Stat::Personality: return "personality";
        case Stat::Trust: return "trust";
        case Stat::Fitness: return "fitness";
        case Stat::Laws: return "laws";
        case Stat::Media: return "media";
        case Stat::Mental: return "mental";
    }
    return "fairness";
}

bool parseStat(const std::string& s, Stat& out) {
    for (int i = 0; i < kStatCount; ++i) {
        const Stat st = static_cast<Stat>(i);
        if (s == statId(st)) {
            out = st;
            return true;
        }
    }
    return false;
}

bool parseVerdict(const std::string& code, Verdict& out) {
    Verdict v;
    if (code == "play" || code == "onside") { v.restart = Restart::PlayOn; }
    else if (code == "adv") { v.restart = Restart::Advantage; }
    else if (code == "fk") { v.restart = Restart::FreeKick; }
    else if (code == "fk_y") { v.restart = Restart::FreeKick; v.card = Card::Yellow; }
    else if (code == "fk_r") { v.restart = Restart::FreeKick; v.card = Card::Red; }
    else if (code == "pen") { v.restart = Restart::Penalty; }
    else if (code == "pen_y") { v.restart = Restart::Penalty; v.card = Card::Yellow; }
    else if (code == "pen_r") { v.restart = Restart::Penalty; v.card = Card::Red; }
    else if (code == "dive_y") { v.restart = Restart::FreeKick; v.card = Card::Yellow; v.cardTo = CardTo::Victim; }
    else if (code == "offside") { v.restart = Restart::Offside; }
    else if (code == "goal") { v.restart = Restart::Goal; }
    else if (code == "no_goal") { v.restart = Restart::NoGoal; }
    else if (code == "conf_none") { v.restart = Restart::FreeKick; }
    else if (code == "conf_y") { v.restart = Restart::FreeKick; v.card = Card::Yellow; }
    else if (code == "conf_y2") { v.restart = Restart::FreeKick; v.card = Card::Yellow; v.cardTo = CardTo::Both; }
    else if (code == "conf_r") { v.restart = Restart::FreeKick; v.card = Card::Red; }
    else return false;
    out = v;
    return true;
}

std::string verdictCode(const Verdict& v) {
    switch (v.restart) {
        case Restart::PlayOn: return "play";
        case Restart::Advantage: return "adv";
        case Restart::Offside: return "offside";
        case Restart::Goal: return "goal";
        case Restart::NoGoal: return "no_goal";
        case Restart::Penalty:
            return v.card == Card::Red ? "pen_r" : v.card == Card::Yellow ? "pen_y" : "pen";
        case Restart::FreeKick:
            if (v.card == Card::Yellow && v.cardTo == CardTo::Victim) return "dive_y";
            if (v.card == Card::Yellow && v.cardTo == CardTo::Both) return "conf_y2";
            return v.card == Card::Red ? "fk_r" : v.card == Card::Yellow ? "fk_y" : "fk";
    }
    return "play";
}

int benefitingSide(const Verdict& v, int offendingSide) {
    const int other = 1 - offendingSide;
    switch (v.restart) {
        case Restart::PlayOn:
        case Restart::Advantage:
            // Advantage keeps the ball with the fouled team; plain play-on lets the offender off.
            return v.restart == Restart::Advantage ? other : offendingSide;
        case Restart::FreeKick:
        case Restart::Penalty:
            return v.cardTo == CardTo::Victim ? offendingSide : other;
        case Restart::Offside:
        case Restart::NoGoal:
            return other;  // offendingSide is the attacking side for offside / goal checks
        case Restart::Goal:
            return offendingSide;
    }
    return other;
}

}  // namespace refcore
