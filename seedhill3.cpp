#include <iostream>
#include <vector>
#include <array>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <string>
#include <cctype>

enum class RngBackend {
    PS2,
    PC
};

static inline uint32_t ps2_rand32_step(uint32_t &seed) {
    seed = (seed * 0x41C64E6Du + 0x3039u) & 0x7FFFFFFFu;
    return seed;
}

static inline uint32_t pc_rand15_step(uint32_t &myseed) {
    myseed = myseed * 0x000343FDu + 0x00269EC3u;
    uint32_t out = (myseed >> 16) & 0x7FFFu;
    return out;
}

static inline uint32_t pc_rand31_from_three(uint32_t &myseed) {
    uint32_t r1 = pc_rand15_step(myseed);          
    uint32_t r2 = pc_rand15_step(myseed);         
    uint32_t r3 = pc_rand15_step(myseed);          

    uint32_t out31 = (((r3 & 1u) << 15) | r2);    
    out31 = (out31 << 15) | r1;                    
    return out31;
}

static inline uint32_t rng_next31(uint32_t &state, RngBackend backend) {
    if (backend == RngBackend::PS2) return ps2_rand32_step(state);
    return pc_rand31_from_three(state);
}

static inline void rng_advance(uint32_t &state, RngBackend backend, int n) {
    for (int i = 0; i < n; ++i) rng_next31(state, backend);
}

static constexpr uint64_t PS2_MOD = 0x80000000ull;
static constexpr uint64_t PS2_A   = 0x41C64E6Dull;
static constexpr uint64_t PS2_C   = 0x3039ull;

static uint64_t modinv(uint64_t a, uint64_t m) {
    int64_t t = 0, newt = 1;
    int64_t r = (int64_t)m, newr = (int64_t)a;
    while (newr != 0) {
        int64_t q = r / newr;
        int64_t tmp = t - q * newt; t = newt; newt = tmp;
        tmp = r - q * newr; r = newr; newr = tmp;
    }
    if (r > 1) return 0;
    if (t < 0) t += (int64_t)m;
    return (uint64_t)t;
}

static inline uint32_t ps2_prev_seed(uint32_t cur, uint64_t invA) {
    uint64_t x = (cur + PS2_MOD - (PS2_C % PS2_MOD)) % PS2_MOD;
    uint64_t prev = (invA * x) % PS2_MOD;
    return (uint32_t)prev;
}

static inline uint32_t ps2_rewind_n(uint32_t seed, int n, uint64_t invA) {
    for (int i = 0; i < n; ++i) seed = ps2_prev_seed(seed, invA);
    return seed;
}

std::vector<uint32_t> find_clock_base_seeds(int targetHour, int targetMinute, uint8_t modeByte,
                                           int warmupAfterReset, int maxResults = 200) {
    std::vector<uint32_t> out;
    uint64_t invA = modinv(PS2_A % PS2_MOD, PS2_MOD);
    if (!invA) return out;

    int rem;
    if (modeByte == 2) {
        rem = targetHour - 12;
        if (rem < 0 || rem > 11) return out;
    } else {
        rem = targetHour - 1;
        if (rem < 0 || rem > 11) return out;
    }

    uint64_t start = (uint64_t)((targetMinute % 60) + 60) % 60;
    for (uint64_t s2 = start; s2 < PS2_MOD; s2 += 60) {
        uint32_t seed2 = (uint32_t)s2;
        uint32_t seed1 = ps2_prev_seed(seed2, invA);

        if ((int)(seed1 % 12) != rem) continue;

        uint32_t seed_w = ps2_prev_seed(seed1, invA);
        uint32_t base   = ps2_rewind_n(seed_w, warmupAfterReset, invA);

        out.push_back(base);
        if ((int)out.size() >= maxResults) break;
    }

    return out;
}

std::optional<int> find_warmup_for_first(uint32_t baseSeed, uint32_t R_first, RngBackend backend, int maxSearch = 2000000) {
    uint32_t seed = baseSeed;
    for (int warmup = 0; warmup <= maxSearch; ++warmup) {
        uint32_t tmp = seed;
        uint32_t r = rng_next31(tmp, backend);
        if (r == R_first) return warmup;
        rng_next31(seed, backend);
    }
    return std::nullopt;
}

uint32_t gen_shakespeare_code(uint32_t seed, int warmupAfterReset, RngBackend backend, bool verbose=false) {
    rng_advance(seed, backend, warmupAfterReset);

    std::vector<int> pool{0,1,2,3,4,5,6,7,8,9};
    uint32_t code4 = 0;

    for (int i = 0; i < 4; ++i) {
        int32_t r = static_cast<int32_t>(rng_next31(seed, backend));
        int size = 10 - i;

        int idx = r % size;
        if (idx < 0) idx += size;

        int digit = pool[idx];
        code4 = (code4 << 4) + static_cast<uint32_t>(digit);

        if (verbose) {
            std::cout << "rand#" << (i + 1)
                      << " = 0x" << std::hex << std::uppercase << (uint32_t)r
                      << std::dec
                      << ", size=" << size
                      << ", idx=" << idx
                      << ", digit=" << digit << "\n";
        }

        pool.erase(pool.begin() + idx);
    }

    return code4;
}

uint32_t gen_clock_puzzle(uint32_t seed, int warmupAfterReset, uint8_t modeByte, RngBackend backend, bool verbose=false) {
    rng_advance(seed, backend, warmupAfterReset);

    uint32_t rHour = rng_next31(seed, backend);
    int hour;
    if (modeByte == 2) {
        hour = static_cast<int>(rHour % 12) + 12;
    } else {
        hour = static_cast<int>(rHour % 12) + 1;
    }

    int h_tens = hour / 10;
    int h_ones = hour % 10;

    uint32_t packed = (static_cast<uint32_t>(h_tens) << 12) |
                      (static_cast<uint32_t>(h_ones) << 8);

    if (verbose) {
        std::cout << "Hour RNG  = 0x" << std::hex << std::uppercase << rHour << std::dec
                  << " -> hour=" << hour
                  << " (tens=" << h_tens << ", ones=" << h_ones << ")\n";
    }

    uint32_t rMin = rng_next31(seed, backend);
    int minute = static_cast<int>(rMin % 60);

    int m_tens = minute / 10;
    int m_ones = minute % 10;

    packed |= (static_cast<uint32_t>(m_tens) << 4) |
              (static_cast<uint32_t>(m_ones));

    if (verbose) {
        std::cout << "Min RNG   = 0x" << std::hex << std::uppercase << rMin << std::dec
                  << " -> minute=" << minute
                  << " (tens=" << m_tens << ", ones=" << m_ones << ")\n";
    }

    return packed;
}

struct ClockWarmupMatch {
    int warmup;
    uint32_t seedAfterWarmup;
    uint32_t rHour;
    uint32_t rMin;
    uint32_t packed;
};

std::vector<ClockWarmupMatch> find_clock_warmups(uint32_t baseSeed, uint8_t modeByte,
                                               int targetHour, int targetMinute,
                                               RngBackend backend,
                                               int minWarmup = 0, int maxWarmup = 5000, int maxResults = 50) {
    std::vector<ClockWarmupMatch> matches;
    uint32_t targetPacked = ((targetHour / 10) << 12) | ((targetHour % 10) << 8) |
                            ((targetMinute / 10) << 4) | (targetMinute % 10);

    uint32_t seedWarm = baseSeed;

    for (int i = 0; i < minWarmup; ++i) {
        rng_next31(seedWarm, backend);
    }

    for (int w = minWarmup; w <= maxWarmup; ++w) {
        uint32_t seed = seedWarm;
        uint32_t seedAfterWarmup = seedWarm;

        uint32_t rHour = rng_next31(seed, backend);
        int hour = (modeByte == 2) ? (int)(rHour % 12) + 12
                                   : (int)(rHour % 12) + 1;
        int h_tens = hour / 10, h_ones = hour % 10;
        uint32_t packed = ((uint32_t)h_tens << 12) | ((uint32_t)h_ones << 8);

        uint32_t rMin = rng_next31(seed, backend);
        int minute = (int)(rMin % 60);
        int m_tens = minute / 10, m_ones = minute % 10;
        packed |= ((uint32_t)m_tens << 4) | (uint32_t)m_ones;

        if (packed == targetPacked) {
            matches.push_back({w, seedAfterWarmup, rHour, rMin, packed});
            if ((int)matches.size() >= maxResults) break;
        }

        rng_next31(seedWarm, backend);
    }

    return matches;
}

std::vector<ClockWarmupMatch> find_clock_warmups_flexible(uint32_t baseSeed, uint8_t modeByte,
                                                       RngBackend backend,
                                                       bool matchHour, bool matchMinute,
                                                       int targetHour, int targetMinute,
                                                       int minWarmup = 0, int maxWarmup = 5000, int maxResults = 50) {
    std::vector<ClockWarmupMatch> matches;

    uint32_t seedWarm = baseSeed;

    for (int i = 0; i < minWarmup; ++i) {
        rng_next31(seedWarm, backend);
    }

    for (int w = minWarmup; w <= maxWarmup; ++w) {
        uint32_t seed = seedWarm;
        uint32_t seedAfterWarmup = seedWarm;

        uint32_t rHour = 0;
        uint32_t rMin  = 0;
        int hour = 0;
        int minute = 0;

        if (matchHour && !matchMinute) {
            rHour = rng_next31(seed, backend);
            hour = (modeByte == 2) ? (int)(rHour % 12) + 12
                                   : (int)(rHour % 12) + 1;
        } else if (!matchHour && matchMinute) {
            rMin = rng_next31(seed, backend);
            minute = (int)(rMin % 60);
        } else {
            rHour = rng_next31(seed, backend);
            hour = (modeByte == 2) ? (int)(rHour % 12) + 12
                                   : (int)(rHour % 12) + 1;
            rMin = rng_next31(seed, backend);
            minute = (int)(rMin % 60);
        }

        bool ok = true;
        if (matchHour && hour != targetHour) ok = false;
        if (matchMinute && minute != targetMinute) ok = false;

        int h_tens = hour / 10, h_ones = hour % 10;
        int m_tens = minute / 10, m_ones = minute % 10;
        uint32_t packed = ((uint32_t)h_tens << 12) | ((uint32_t)h_ones << 8) |
                          ((uint32_t)m_tens << 4) | (uint32_t)m_ones;

        if (ok) {
            matches.push_back({w, seedAfterWarmup, rHour, rMin, packed});
            if ((int)matches.size() >= maxResults) break;
        }

        rng_next31(seedWarm, backend);
    }

    return matches;
}

std::optional<int> find_seed_distance(uint32_t baseSeed, uint32_t targetSeed, RngBackend backend, int maxSteps = 5000000) {
    if (baseSeed == targetSeed) return 0;
    uint32_t seed = baseSeed;
    for (int n = 1; n <= maxSteps; ++n) {
        rng_next31(seed, backend);
        if (seed == targetSeed) return n;
    }
    return std::nullopt;
}

static std::optional<uint32_t> parse_shakespeare_code_input(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (char ch : s) {
        if (!std::isspace((unsigned char)ch)) t.push_back(ch);
    }
    if (t.empty()) return std::nullopt;

    bool looksHex = false;
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) looksHex = true;
    for (char ch : t) {
        if ((ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) {
            looksHex = true;
            break;
        }
    }

    uint32_t packed = 0;

    if (looksHex) {
        size_t idx = 0;
        if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) idx = 2;
        if (t.size() - idx == 0 || t.size() - idx > 8) return std::nullopt;

        uint32_t val = 0;
        for (; idx < t.size(); ++idx) {
            char ch = t[idx];
            int v = -1;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'A' && ch <= 'F') v = 10 + (ch - 'A');
            else if (ch >= 'a' && ch <= 'f') v = 10 + (ch - 'a');
            else return std::nullopt;
            val = (val << 4) | (uint32_t)v;
        }
        packed = val;
    } else {
        if (t.size() != 4) return std::nullopt;
        packed = 0;
        for (char ch : t) {
            if (ch < '0' || ch > '9') return std::nullopt;
            packed = (packed << 4) | (uint32_t)(ch - '0');
        }
    }

    bool used[10] = {false};
    for (int i = 0; i < 4; ++i) {
        int digit = (packed >> (12 - 4 * i)) & 0xF;
        if (digit < 0 || digit > 9) return std::nullopt;
        if (used[digit]) return std::nullopt;
        used[digit] = true;
    }

    return packed;
}

struct ShakespeareMatch {
    int advances;
    uint32_t seedAfterWarmup;
    uint32_t codePacked;
};

static inline uint32_t gen_shakespeare_code_from_seed(uint32_t seedAfterWarmup, RngBackend backend) {
    std::array<int, 10> pool = {0,1,2,3,4,5,6,7,8,9};
    int poolSize = 10;
    uint32_t code4 = 0;

    for (int i = 0; i < 4; ++i) {
        int32_t r = (int32_t)rng_next31(seedAfterWarmup, backend);
        int idx = r % poolSize;
        if (idx < 0) idx += poolSize;

        int digit = pool[idx];
        code4 = (code4 << 4) | (uint32_t)digit;

        for (int j = idx; j < poolSize - 1; ++j) pool[j] = pool[j + 1];
        poolSize--;
    }

    return code4;
}

static std::vector<ShakespeareMatch> find_shakespeare_seeds_for_code(
    uint32_t startSeed,
    uint32_t targetCodePacked,
    int maxResults,
    RngBackend backend,
    int minAdvances = 0,
    int hardMaxAdvances = 10'000'000
) {
    std::vector<ShakespeareMatch> out;
    out.reserve((size_t)maxResults);

    uint32_t seedWarm = startSeed;

    for (int i = 0; i < minAdvances; ++i) {
        rng_next31(seedWarm, backend);
    }

    for (int adv = minAdvances; adv <= hardMaxAdvances; ++adv) {
        uint32_t code = gen_shakespeare_code_from_seed(seedWarm, backend);
        if (code == targetCodePacked) {
            out.push_back({adv, seedWarm, code});
            if ((int)out.size() >= maxResults) break;
        }
        rng_next31(seedWarm, backend);
    }

    return out;
}

static void print_shakespeare(uint32_t code) {
    std::cout << "\nShakespeare 4-digit code = 0x"
              << std::hex << std::uppercase << code << std::dec
              << " (digits packed as nibbles)\n";
    std::cout << "Digits shown as decimal: "
              << ((code >> 12) & 0xF)
              << ((code >> 8)  & 0xF)
              << ((code >> 4)  & 0xF)
              << (code & 0xF) << "\n";
}

static void print_clock(uint32_t packed) {
    int h_tens = (packed >> 12) & 0xF;
    int h_ones = (packed >> 8)  & 0xF;
    int m_tens = (packed >> 4)  & 0xF;
    int m_ones = (packed)       & 0xF;

    int hour = h_tens * 10 + h_ones;
    int minute = m_tens * 10 + m_ones;

    std::cout << "\nClock puzzle packed = 0x"
              << std::hex << std::uppercase << packed << std::dec << "\n";
    std::cout << "Decoded time = "
              << hour << ":"
              << (minute < 10 ? "0" : "") << minute << "\n";
    std::cout << "Digits: " << h_tens << h_ones << m_tens << m_ones << "\n";
}

struct HospitalMatch {
    int advances;
    uint32_t seedAfterWarmup;
    uint32_t codePacked;
};

uint32_t gen_hospital3f_code(uint32_t seed, int warmupAfterReset, RngBackend backend, bool verbose=false);
static void print_hospital3f(uint32_t code);
static std::optional<uint32_t> parse_hospital3f_code_input(const std::string& s);

static std::vector<HospitalMatch> find_hospital3f_seeds_for_code(
    uint32_t startSeed,
    uint32_t targetCodePacked,
    int maxResults,
    RngBackend backend,
    int minAdvances,
    int maxAdvances
);

struct CrematoriumMatch {
    int advances;
    uint32_t seedAfterWarmup;
    uint32_t codePacked;
    bool forced7;
    int forcedPosLSB; 
};

uint32_t gen_crematorium_code_guarantee7(uint32_t seed, int warmupAfterReset, RngBackend backend, bool verbose=false);
static void print_crematorium(uint32_t code, bool forced7, int forcedPosLSB);
static std::optional<uint32_t> parse_crematorium_code_input(const std::string& s);

static std::vector<CrematoriumMatch> find_crematorium_seeds_for_code(
    uint32_t startSeed,
    uint32_t targetCodePacked,
    int maxResults,
    RngBackend backend,
    int minAdvances,
    int maxAdvances
);

int main() {
    std::cout << "Silent Hill 3 RNG tool\n";
    std::cout << "Choose input mode:\n";
    std::cout << "  1) Shakespeare Puzzle: Enter base seed + warmup count directly\n";
    std::cout << "  2) Shakespeare Puzzle: Enter first Shakespeare Puzzle rand() return (auto-find warmup)\n";
    std::cout << "  3) Shakespeare Puzzle: Enter Shakespeare Puzzle seed directly\n";
    std::cout << "  4) Shakespeare Puzzle: Reverse (enter 4-digit code -> list possible seeds)\n";
    std::cout << "  5) Clock puzzle: Generate HH:MM from seed/warmups + mode byte\n";
    std::cout << "  6) Clock puzzle: Reverse (hour only / minute only / both) -> list possible seeds\n";
    std::cout << "  7) Clock puzzle: Auto-find warmup(s) from base seed + target HH:MM\n";
    std::cout << "  8) 3F Hospital: Generate 4-digit code from seed/warmups\n";
    std::cout << "  9) 3F Hospital: Reverse (enter 4-digit code -> list possible seeds)\n";
    std::cout << "  10) Crematorium Oven: Generate 4-digit code from seed/warmups\n";
    std::cout << "  11) Crematorium Oven: Reverse (enter 4-digit code -> list possible seeds + forced7)\n";
    std::cout << "  12) RNG: Continuous warmup distances (base -> target1 -> target2 ...)\n";
    std::cout << "Mode (1/2/3/4/5/6/7/8/9/10/11/12): ";

    int mode = 1;
    std::cin >> mode;

    char rngInput;
    std::cout << "RNG backend: PS2 or PC? (p/c): ";
    std::cin >> rngInput;
    RngBackend backend = (rngInput == 'c' || rngInput == 'C') ? RngBackend::PC : RngBackend::PS2;

    int warmup = 0;
    uint32_t baseSeed = 0u;

    if (mode == 2) {
        std::cout << "Enter Shakespeare Puzzle base seed to search from (hex, no 0x, usually 0): ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        uint32_t R_first;
        std::cout << "Enter first Shakespeare Puzzle rand() return (hex, no 0x): ";
        std::cin >> std::hex >> R_first;
        std::cin >> std::dec;

        auto w = find_warmup_for_first(baseSeed, R_first, backend);
        if (!w) {
            std::cout << "Warmup not found in search range.\n";
            return 0;
        }
        warmup = *w;
        std::cout << "Auto-found warmup = " << warmup << "\n\n";

        uint32_t code = gen_shakespeare_code(baseSeed, warmup, backend, true);
        print_shakespeare(code);
        return 0;

    } else if (mode == 3) {
        std::cout << "Enter Shakespeare Puzzle base seed (hex, no 0x): ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        std::cout << "Warmups after that base seed: ";
        std::cin >> warmup;
        std::cout << "\n";

        uint32_t code = gen_shakespeare_code(baseSeed, warmup, backend, true);
        print_shakespeare(code);
        return 0;

    } else if (mode == 5) {
        std::cout << "Enter seed (hex, no 0x): ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        std::cout << "Warmup rand() calls after that seed: ";
        std::cin >> warmup;

        char modeInput;
        std::cout << "24h path option (y/n): ";
        std::cin >> modeInput;
        uint8_t modeByte = (modeInput == 'y' || modeInput == 'Y') ? 2 : 0;

        std::cout << "\n";
        uint32_t packed = gen_clock_puzzle(baseSeed, warmup, modeByte, backend, true);
        print_clock(packed);
        return 0;

    } else if (mode == 6) {
        char which;
        std::cout << "Search for: (h)our only, (m)inute only, (b)oth: ";
        std::cin >> which;

        bool matchHour = false;
        bool matchMinute = false;
        if (which == 'h' || which == 'H') {
            matchHour = true;
        } else if (which == 'm' || which == 'M') {
            matchMinute = true;
        } else {
            matchHour = true;
            matchMinute = true;
        }

        int targetHour = 0;
        int targetMinute = 0;

        if (matchHour) {
            std::cout << "Enter target hour (decimal): ";
            std::cin >> targetHour;
        }
        if (matchMinute) {
            std::cout << "Enter target minute (decimal 0-59): ";
            std::cin >> targetMinute;
        }

        std::cout << "Enter base seed to measure advances from (hex, no 0x): ";
        uint32_t base = 0;
        std::cin >> std::hex >> base;
        std::cin >> std::dec;

        uint8_t modeByte = 0;
        if (matchHour) {
            char modeInput;
            std::cout << "24h path option (y/n): ";
            std::cin >> modeInput;
            modeByte = (modeInput == 'y' || modeInput == 'Y') ? 2 : 0;
        }

        int minWarmup;
        std::cout << "Min advances to start searching from (decimal, e.g. 0): ";
        std::cin >> minWarmup;

        int maxWarmup;
        std::cout << "Max advances to search up to (decimal, e.g. 500000): ";
        std::cin >> maxWarmup;

        int maxResults;
        std::cout << "Max matches to show (decimal, e.g. 20): ";
        std::cin >> maxResults;

        auto matches = find_clock_warmups_flexible(base, modeByte, backend,
                                                   matchHour, matchMinute,
                                                   targetHour, targetMinute,
                                                   minWarmup, maxWarmup, maxResults);

        if (matches.empty()) {
            std::cout << "\nNo advances in [" << minWarmup << ".." << maxWarmup << "] produced a match.\n";
            return 0;
        }

        std::cout << "\nMatches from base seed 0x" << std::hex << std::uppercase << base << std::dec
                  << " (each advance = 1 rand call):\n";

        for (size_t i = 0; i < matches.size(); ++i) {
            const auto &m = matches[i];
            std::cout << "  [" << i << "] advances=" << m.warmup
                      << "  seed@advance=0x" << std::hex << std::uppercase << m.seedAfterWarmup << std::dec;

            if (matchHour && !matchMinute) {
                int hour = ((m.packed >> 12) & 0xF) * 10 + ((m.packed >> 8) & 0xF);
                std::cout << "  hour=" << hour
                          << "  rHour=0x" << std::hex << std::uppercase << m.rHour << std::dec;
            } else if (!matchHour && matchMinute) {
                int minute = ((m.packed >> 4) & 0xF) * 10 + (m.packed & 0xF);
                std::cout << "  minute=" << (minute < 10 ? "0" : "") << minute
                          << "  rMin=0x" << std::hex << std::uppercase << m.rMin << std::dec;
            } else {
                std::cout << "  rHour=0x" << std::hex << std::uppercase << m.rHour << std::dec
                          << "  rMin=0x" << std::hex << std::uppercase << m.rMin << std::dec
                          << "  packed=0x" << std::hex << std::uppercase << m.packed << std::dec;
            }

            std::cout << "\n";
        }

        const auto &best = matches.front();
        std::cout << "\nEarliest match: advances=" << best.warmup
                  << "  seed@advance=0x" << std::hex << std::uppercase << best.seedAfterWarmup << std::dec << "\n";

        uint32_t packed = gen_clock_puzzle(best.seedAfterWarmup, /*warmupAfterReset=*/0, modeByte, backend, false);

        if (matchHour && !matchMinute) {
            int h_tens = (packed >> 12) & 0xF;
            int h_ones = (packed >> 8) & 0xF;
            int hour = h_tens * 10 + h_ones;
            std::cout << "Sanity-check hour=" << hour << "\n";
        } else if (!matchHour && matchMinute) {
            int m_tens = (packed >> 4) & 0xF;
            int m_ones = (packed) & 0xF;
            int minute = m_tens * 10 + m_ones;
            std::cout << "Sanity-check minute=" << (minute < 10 ? "0" : "") << minute << "\n";
        } else {
            std::cout << "Sanity-check packed=0x" << std::hex << std::uppercase << packed << std::dec << "\n";
            print_clock(packed);
        }

        return 0;

    } else if (mode == 7) {
        std::cout << "Enter base seed (hex, no 0x): ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        std::cout << "Enter target hour (decimal): ";
        int targetHour; std::cin >> targetHour;
        std::cout << "Enter target minute (decimal 0-59): ";
        int targetMinute; std::cin >> targetMinute;

        char modeInput;
        std::cout << "24h path option (y/n): ";
        std::cin >> modeInput;
        uint8_t modeByte = (modeInput == 'y' || modeInput == 'Y') ? 2 : 0;

        int minWarmup;
        std::cout << "Min warmup to start searching from (decimal, e.g. 0): ";
        std::cin >> minWarmup;

        int maxWarmup;
        std::cout << "Max warmup to search up to (decimal, e.g. 5000): ";
        std::cin >> maxWarmup;

        int maxResults;
        std::cout << "Max matches to show (decimal, e.g. 20): ";
        std::cin >> maxResults;

        auto matches = find_clock_warmups(baseSeed, modeByte, targetHour, targetMinute,
                                          backend, minWarmup, maxWarmup, maxResults);
        if (matches.empty()) {
            std::cout << "\nNo warmups in [" << minWarmup << ".." << maxWarmup << "] produced that HH:MM.\n";
        } else {
            std::cout << "\nMatches for " << targetHour << ":" << (targetMinute<10?"0":"") << targetMinute << ":\n";
            for (size_t i = 0; i < matches.size(); ++i) {
                const auto &m = matches[i];
                std::cout << "  [" << i << "] warmup=" << m.warmup
                          << "  seed@warmup=0x" << std::hex << std::uppercase << m.seedAfterWarmup << std::dec
                          << "  rHour=0x" << std::hex << std::uppercase << m.rHour << std::dec
                          << "  rMin=0x" << std::hex << std::uppercase << m.rMin << std::dec
                          << "  packed=0x" << std::hex << std::uppercase << m.packed << std::dec
                          << "\n";
            }
        }
        return 0;

    } else if (mode == 12) {
        std::cout << "Enter base seed (hex, no 0x):";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        constexpr int maxSteps = 10000000;

        uint32_t currentSeed = baseSeed;
        uint64_t totalAdvances = 0;

        while (true) {
            uint32_t targetSeed;
            std::cout << "\nEnter target seed (hex, no 0x), or '0' to quit: ";
            std::cin >> std::hex >> targetSeed;
            std::cin >> std::dec;

            if (targetSeed == 0) {
                std::cout << "\nDone. Total advances across all segments: " << totalAdvances << "\n";
                break;
            }

            auto dist = find_seed_distance(currentSeed, targetSeed, backend, maxSteps);
            if (!dist) {
                std::cout << "Target seed not found within " << maxSteps << " advances from current seed.\n";
                std::cout << "(This likely means a reseed/overwrite occurred, or maxSteps is too small.)\n";
            } else {
                std::cout << "Advances from 0x" << std::hex << std::uppercase << currentSeed << std::dec
                          << " -> 0x" << std::hex << std::uppercase << targetSeed << std::dec
                          << " = " << *dist << "\n";
                totalAdvances += (uint64_t)(*dist);
                currentSeed = targetSeed;
            }
        }
        return 0;

    } else if (mode == 4) {
        std::cout << "Enter target Shakespeare code (either 4 digits like 0123, or packed hex like 0x0123): ";
        std::string codeStr;
        std::cin >> codeStr;

        auto parsed = parse_shakespeare_code_input(codeStr);
        if (!parsed) {
            std::cout << "Invalid code. Must be 4 unique digits 0-9 (e.g. 0123), or packed hex (0x0123).\n";
            return 0;
        }
        uint32_t targetCode = *parsed;

        std::cout << "Enter starting seed to scan from (hex, no 0x): ";
        uint32_t startSeed = 0;
        std::cin >> std::hex >> startSeed;
        std::cin >> std::dec;

        int minAdvances = 0;
        std::cout << "Min advances to start searching from (decimal, e.g. 0): ";
        std::cin >> minAdvances;
        if (minAdvances < 0) minAdvances = 0;

        int hardMaxAdvances = 0;
        std::cout << "Max advances to search up to (decimal, e.g. 5000000): ";
        std::cin >> hardMaxAdvances;
        if (hardMaxAdvances < minAdvances) hardMaxAdvances = minAdvances;

        int maxResults = 0;
        std::cout << "Max matches to show (decimal, e.g. 20): ";
        std::cin >> maxResults;
        if (maxResults <= 0) {
            std::cout << "Max results must be > 0.\n";
            return 0;
        }

        auto matches = find_shakespeare_seeds_for_code(startSeed, targetCode, maxResults, backend, minAdvances, hardMaxAdvances);

        if (matches.empty()) {
            std::cout << "\nNo matches found in [" << minAdvances << ".." << hardMaxAdvances
                      << "] advances from start seed 0x" << std::hex << std::uppercase << startSeed << std::dec << ".\n";
            return 0;
        }

        std::cout << "\nMatches for Shakespeare code 0x" << std::hex << std::uppercase << targetCode << std::dec
                  << " starting from seed 0x" << std::hex << std::uppercase << startSeed << std::dec
                  << " (each advance = 1 rand call):\n";

        for (size_t i = 0; i < matches.size(); ++i) {
            const auto &m = matches[i];
            std::cout << "  [" << i << "] advances=" << m.advances
                      << "  seed@advance=0x" << std::hex << std::uppercase << m.seedAfterWarmup << std::dec
                      << "\n";
        }

        std::cout << "\nSanity-check first match:\n";
        uint32_t code = gen_shakespeare_code(matches.front().seedAfterWarmup, /*warmupAfterReset=*/0, backend, false);
        print_shakespeare(code);

        return 0;

    } else if (mode == 8) {
        std::cout << "Enter seed (hex, no 0x): ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        std::cout << "Warmup rand() calls after that seed: ";
        std::cin >> warmup;

        std::cout << "\n";
        uint32_t code = gen_hospital3f_code(baseSeed, warmup, backend, true);
        print_hospital3f(code);
        return 0;

    } else if (mode == 9) {
        std::cout << "Enter target 3F Hospital code (either 4 digits like 2580, or packed hex like 0x2580): ";
        std::string codeStr;
        std::cin >> codeStr;

        auto parsed = parse_hospital3f_code_input(codeStr);
        if (!parsed) {
            std::cout << "Invalid code. Must be 4 unique digits 1-9 (e.g. 2580), or packed hex (0x2580).\n";
            return 0;
        }
        uint32_t targetCode = *parsed;

        std::cout << "Enter starting seed to scan from (hex, no 0x): ";
        uint32_t startSeed = 0;
        std::cin >> std::hex >> startSeed;
        std::cin >> std::dec;

        int minAdvances = 0;
        std::cout << "Min advances to start searching from (decimal, e.g. 0): ";
        std::cin >> minAdvances;
        if (minAdvances < 0) minAdvances = 0;

        int maxAdvances = 0;
        std::cout << "Max advances to search up to (decimal, e.g. 5000000): ";
        std::cin >> maxAdvances;
        if (maxAdvances < minAdvances) maxAdvances = minAdvances;

        int maxResults = 0;
        std::cout << "Max matches to show (decimal, e.g. 20): ";
        std::cin >> maxResults;
        if (maxResults <= 0) {
            std::cout << "Max results must be > 0.\n";
            return 0;
        }

        auto matches = find_hospital3f_seeds_for_code(startSeed, targetCode, maxResults, backend, minAdvances, maxAdvances);
        if (matches.empty()) {
            std::cout << "\nNo matches found in [" << minAdvances << ".." << maxAdvances
                      << "] advances from start seed 0x" << std::hex << std::uppercase << startSeed << std::dec << ".\n";
            return 0;
        }

        std::cout << "\nMatches for 3F Hospital code 0x" << std::hex << std::uppercase << targetCode << std::dec
                  << " starting from seed 0x" << std::hex << std::uppercase << startSeed << std::dec
                  << " (each advance = 1 rand call):\n";

        for (size_t i = 0; i < matches.size(); ++i) {
            const auto &m = matches[i];
            std::cout << "  [" << i << "] advances=" << m.advances
                      << "  seed@advance=0x" << std::hex << std::uppercase << m.seedAfterWarmup << std::dec
                      << "\n";
        }

        std::cout << "\nSanity-check first match:\n";
        uint32_t code = gen_hospital3f_code(matches.front().seedAfterWarmup, /*warmupAfterReset=*/0, backend, false);
        print_hospital3f(code);

        return 0;

    } else if (mode == 10) {
        std::cout << "Enter seed (hex, no 0x): ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        std::cout << "Warmup rand() calls after that seed: ";
        std::cin >> warmup;

        std::cout << "\n";

        uint32_t seedTmp = baseSeed;
        rng_advance(seedTmp, backend, warmup);
        bool forced7 = false;
        int forcedPos = -1;

        uint32_t code = gen_crematorium_code_guarantee7(baseSeed, warmup, backend, true);

        {
            uint32_t s = baseSeed;
            rng_advance(s, backend, warmup);
            std::array<int,10> pool = {0,1,2,3,4,5,6,7,8,9};
            int poolSize = 10;
            uint32_t packed = 0;
            bool saw7 = false;
            for (int i = 0; i < 4; ++i) {
                uint32_t r = rng_next31(s, backend);
                int idx = (int)(r % (uint32_t)poolSize);
                int digit = pool[idx];
                if (digit == 7) saw7 = true;
                packed = (packed << 4) | (uint32_t)digit;
                for (int j = idx; j < poolSize - 1; ++j) pool[j] = pool[j + 1];
                poolSize--;
            }
            if (!saw7) {
                uint32_t r = rng_next31(s, backend);
                forcedPos = (int)(r % 4u);
                forced7 = true;
            }
        }

        print_crematorium(code, forced7, forcedPos);
        return 0;

    } else if (mode == 11) {
        std::cout << "Enter target Crematorium Oven code (either 4 digits like 7012, or packed hex like 0x7012): ";
        std::string codeStr;
        std::cin >> codeStr;

        auto parsed = parse_crematorium_code_input(codeStr);
        if (!parsed) {
            std::cout << "Invalid code. Must be 4 unique digits 0-9 and MUST include 7 (or packed hex).\n";
            return 0;
        }
        uint32_t targetCode = *parsed;

        std::cout << "Enter starting seed to scan from (hex, no 0x): ";
        uint32_t startSeed = 0;
        std::cin >> std::hex >> startSeed;
        std::cin >> std::dec;

        int minAdvances = 0;
        std::cout << "Min advances to start searching from (decimal, e.g. 0): ";
        std::cin >> minAdvances;
        if (minAdvances < 0) minAdvances = 0;

        int maxAdvances = 0;
        std::cout << "Max advances to search up to (decimal, e.g. 5000000): ";
        std::cin >> maxAdvances;
        if (maxAdvances < minAdvances) maxAdvances = minAdvances;

        int maxResults = 0;
        std::cout << "Max matches to show (decimal, e.g. 20): ";
        std::cin >> maxResults;
        if (maxResults <= 0) {
            std::cout << "Max results must be > 0.\n";
            return 0;
        }

        auto matches = find_crematorium_seeds_for_code(startSeed, targetCode, maxResults, backend, minAdvances, maxAdvances);
        if (matches.empty()) {
            std::cout << "\nNo matches found in [" << minAdvances << ".." << maxAdvances
                      << "] advances from start seed 0x" << std::hex << std::uppercase << startSeed << std::dec << ".\n";
            return 0;
        }

        std::cout << "\nMatches for Crematorium Oven code 0x" << std::hex << std::uppercase << targetCode << std::dec
                  << " starting from seed 0x" << std::hex << std::uppercase << startSeed << std::dec
                  << " (each advance = 1 rand call):\n";

        for (size_t i = 0; i < matches.size(); ++i) {
            const auto &m = matches[i];
            std::cout << "  [" << i << "] advances=" << m.advances
                      << "  seed@advance=0x" << std::hex << std::uppercase << m.seedAfterWarmup << std::dec
                      << "  forced7=" << (m.forced7 ? "yes" : "no");
            if (m.forced7) {
                std::cout << " posLSB=" << m.forcedPosLSB;
            }
            std::cout << "\n";
        }

        std::cout << "\nSanity-check first match:\n";
        {
            uint32_t s = matches.front().seedAfterWarmup;
            bool forced7 = false;
            int forcedPos = -1;
            std::array<int,10> pool = {0,1,2,3,4,5,6,7,8,9};
            int poolSize = 10;
            uint32_t packed = 0;
            bool saw7 = false;
            for (int i = 0; i < 4; ++i) {
                uint32_t r = rng_next31(s, backend);
                int idx = (int)(r % (uint32_t)poolSize);
                int digit = pool[idx];
                if (digit == 7) saw7 = true;
                packed = (packed << 4) | (uint32_t)digit;
                for (int j = idx; j < poolSize - 1; ++j) pool[j] = pool[j + 1];
                poolSize--;
            }
            if (!saw7) {
                uint32_t r = rng_next31(s, backend);
                forcedPos = (int)(r % 4u);
                forced7 = true;
                uint32_t shift = (uint32_t)(forcedPos * 4);
                packed = (packed & ~(0xFu << shift)) | (7u << shift);
            }
            print_crematorium(packed, forced7, forcedPos);
        }

        return 0;
    } else {
        std::cout << "Enter base seed (hex, no 0x). For new-game stream use 0: ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        std::cout << "Warmup rand() calls after that base seed: ";
        std::cin >> warmup;
        std::cout << "\n";

        uint32_t code = gen_shakespeare_code(baseSeed, warmup, backend, true);
        print_shakespeare(code);
        return 0;
    }
}

uint32_t gen_hospital3f_code(uint32_t seed, int warmupAfterReset, RngBackend backend, bool verbose) {
    rng_advance(seed, backend, warmupAfterReset);

    std::vector<int> pool{1,2,3,4,5,6,7,8,9};
    uint32_t code4 = 0;

    for (int i = 0; i < 4; ++i) {
        int32_t r = static_cast<int32_t>(rng_next31(seed, backend));
        int size = 9 - i;

        int idx = r % size;
        if (idx < 0) idx += size;

        int digit = pool[idx];
        code4 = (code4 << 4) | static_cast<uint32_t>(digit);

        if (verbose) {
            std::cout << "rand#" << (i + 1)
                      << " = 0x" << std::hex << std::uppercase << (uint32_t)r
                      << std::dec
                      << ", size=" << size
                      << ", idx=" << idx
                      << ", digit=" << digit << "\n";
        }

        pool.erase(pool.begin() + idx);
    }

    return code4;
}

static void print_hospital3f(uint32_t code) {
    std::cout << "\n3F Hospital code = 0x"
              << std::hex << std::uppercase << code << std::dec
              << " (digits packed as nibbles)\n";
    std::cout << "Digits shown as decimal: "
              << ((code >> 12) & 0xF)
              << ((code >> 8)  & 0xF)
              << ((code >> 4)  & 0xF)
              << (code & 0xF) << "\n";
}

static std::optional<uint32_t> parse_hospital3f_code_input(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (char ch : s) {
        if (!std::isspace((unsigned char)ch)) t.push_back(ch);
    }
    if (t.empty()) return std::nullopt;

    bool looksHex = false;
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) looksHex = true;
    for (char ch : t) {
        if ((ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) {
            looksHex = true;
            break;
        }
    }

    uint32_t packed = 0;

    if (looksHex) {
        size_t idx = 0;
        if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) idx = 2;
        if (t.size() - idx == 0 || t.size() - idx > 8) return std::nullopt;

        uint32_t val = 0;
        for (; idx < t.size(); ++idx) {
            char ch = t[idx];
            int v = -1;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'A' && ch <= 'F') v = 10 + (ch - 'A');
            else if (ch >= 'a' && ch <= 'f') v = 10 + (ch - 'a');
            else return std::nullopt;
            val = (val << 4) | (uint32_t)v;
        }
        packed = val;
    } else {
        if (t.size() != 4) return std::nullopt;
        packed = 0;
        for (char ch : t) {
            if (ch < '0' || ch > '9') return std::nullopt;
            packed = (packed << 4) | (uint32_t)(ch - '0');
        }
    }

    bool used[10] = {false};
    for (int i = 0; i < 4; ++i) {
        int digit = (packed >> (12 - 4 * i)) & 0xF;
        if (digit < 1 || digit > 9) return std::nullopt;
        if (used[digit]) return std::nullopt;
        used[digit] = true;
    }

    return packed;
}


static inline uint32_t gen_hospital3f_code_from_seed(uint32_t seedAfterWarmup, RngBackend backend) {
    std::array<int, 9> pool = {1,2,3,4,5,6,7,8,9};
    int poolSize = 9;
    uint32_t code4 = 0;

    for (int i = 0; i < 4; ++i) {
        int32_t r = (int32_t)rng_next31(seedAfterWarmup, backend);
        int idx = r % poolSize;
        if (idx < 0) idx += poolSize;

        int digit = pool[idx];
        code4 = (code4 << 4) | (uint32_t)digit;

        for (int j = idx; j < poolSize - 1; ++j) pool[j] = pool[j + 1];
        poolSize--;
    }

    return code4;
}

static std::vector<HospitalMatch> find_hospital3f_seeds_for_code(
    uint32_t startSeed,
    uint32_t targetCodePacked,
    int maxResults,
    RngBackend backend,
    int minAdvances = 0,
    int maxAdvances = 10'000'000
) {
    std::vector<HospitalMatch> out;
    out.reserve((size_t)maxResults);

    uint32_t seedWarm = startSeed;

    for (int i = 0; i < minAdvances; ++i) {
        rng_next31(seedWarm, backend);
    }

    for (int adv = minAdvances; adv <= maxAdvances; ++adv) {
        uint32_t code = gen_hospital3f_code_from_seed(seedWarm, backend);
        if (code == targetCodePacked) {
            out.push_back({adv, seedWarm, code});
            if ((int)out.size() >= maxResults) break;
        }
        rng_next31(seedWarm, backend);
    }

    return out;
}

static void print_crematorium(uint32_t code, bool forced7, int forcedPosLSB) {
    std::cout << "\nCrematorium Oven code = 0x"
              << std::hex << std::uppercase << code << std::dec
              << " (digits packed as nibbles)\n";
    std::cout << "Digits shown as decimal: "
              << ((code >> 12) & 0xF)
              << ((code >> 8)  & 0xF)
              << ((code >> 4)  & 0xF)
              << (code & 0xF) << "\n";
    std::cout << "Used !has7 force-branch: " << (forced7 ? "YES" : "NO") << "\n";
    if (forced7) {
        std::cout << "Forced nibble pos (LSB-based): " << forcedPosLSB
                  << " (0=rightmost digit, 3=leftmost digit)\n";
    }
}

static std::optional<uint32_t> parse_crematorium_code_input(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (char ch : s) {
        if (!std::isspace((unsigned char)ch)) t.push_back(ch);
    }
    if (t.empty()) return std::nullopt;

    bool looksHex = false;
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) looksHex = true;
    for (char ch : t) {
        if ((ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) {
            looksHex = true;
            break;
        }
    }

    uint32_t packed = 0;

    if (looksHex) {
        size_t idx = 0;
        if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) idx = 2;
        if (t.size() - idx == 0 || t.size() - idx > 8) return std::nullopt;

        uint32_t val = 0;
        for (; idx < t.size(); ++idx) {
            char ch = t[idx];
            int v = -1;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'A' && ch <= 'F') v = 10 + (ch - 'A');
            else if (ch >= 'a' && ch <= 'f') v = 10 + (ch - 'a');
            else return std::nullopt;
            val = (val << 4) | (uint32_t)v;
        }
        packed = val;
    } else {
        if (t.size() != 4) return std::nullopt;
        packed = 0;
        for (char ch : t) {
            if (ch < '0' || ch > '9') return std::nullopt;
            packed = (packed << 4) | (uint32_t)(ch - '0');
        }
    }

    bool used[10] = {false};
    bool has7 = false;
    for (int i = 0; i < 4; ++i) {
        int digit = (packed >> (12 - 4 * i)) & 0xF;
        if (digit < 0 || digit > 9) return std::nullopt;
        if (used[digit]) return std::nullopt;
        used[digit] = true;
        if (digit == 7) has7 = true;
    }

    if (!has7) return std::nullopt;

    return packed;
}

struct CrematoriumMeta {
    uint32_t codePacked;
    bool forced7;
    int forcedPosLSB; 
};

static inline CrematoriumMeta gen_crematorium_meta_from_seed(uint32_t seedAfterWarmup, RngBackend backend) {
    std::array<int,10> pool = {0,1,2,3,4,5,6,7,8,9};
    int poolSize = 10;

    uint32_t packed = 0;
    bool saw7 = false;

    for (int i = 0; i < 4; ++i) {
        uint32_t r = rng_next31(seedAfterWarmup, backend);
        int idx = (int)(r % (uint32_t)poolSize);
        int digit = pool[idx];
        if (digit == 7) saw7 = true;
        packed = (packed << 4) | (uint32_t)digit;

        for (int j = idx; j < poolSize - 1; ++j) pool[j] = pool[j + 1];
        poolSize--;
    }

    bool forced7 = false;
    int forcedPos = -1;

    if (!saw7) {
        uint32_t r = rng_next31(seedAfterWarmup, backend);
        forcedPos = (int)(r % 4u);
        forced7 = true;
        uint32_t shift = (uint32_t)(forcedPos * 4);
        packed = (packed & ~(0xFu << shift)) | (7u << shift);
    }

    return {packed, forced7, forcedPos};
}

uint32_t gen_crematorium_code_guarantee7(uint32_t seed, int warmupAfterReset, RngBackend backend, bool verbose) {
    rng_advance(seed, backend, warmupAfterReset);

    std::vector<int> remainingDigits{0,1,2,3,4,5,6,7,8,9};

    uint32_t packedCode = 0;
    bool sawSeven = false;

    for (int draw = 0; draw < 4; ++draw) {
        uint32_t r = rng_next31(seed, backend);
        int poolSize = 10 - draw;
        int pickIndex = (int)(r % (uint32_t)poolSize);

        int digit = remainingDigits[pickIndex];
        if (digit == 7) sawSeven = true;

        packedCode = (packedCode << 4) | (uint32_t)digit;

        if (verbose) {
            std::cout << "draw#" << (draw + 1)
                      << " r=0x" << std::hex << std::uppercase << r << std::dec
                      << " poolSize=" << poolSize
                      << " pickIndex=" << pickIndex
                      << " digit=" << digit
                      << (digit == 7 ? " (saw 7)" : "")
                      << "\n";
        }

        remainingDigits.erase(remainingDigits.begin() + pickIndex);
    }

    if (!sawSeven) {
        uint32_t r = rng_next31(seed, backend);
        int posLSB = (int)(r % 4u);
        uint32_t shift = (uint32_t)(posLSB * 4);

        uint32_t before = packedCode;
        packedCode = (packedCode & ~(0xFu << shift)) | (7u << shift);

        if (verbose) {
            std::cout << "force7 r=0x" << std::hex << std::uppercase << r << std::dec
                      << " posLSB=" << posLSB
                      << " before=0x" << std::hex << std::uppercase << before
                      << " after=0x" << packedCode << std::dec << "\n";
        }
    }

    return packedCode;
}

static std::vector<CrematoriumMatch> find_crematorium_seeds_for_code(
    uint32_t startSeed,
    uint32_t targetCodePacked,
    int maxResults,
    RngBackend backend,
    int minAdvances,
    int maxAdvances
) {
    std::vector<CrematoriumMatch> out;
    out.reserve((size_t)maxResults);

    uint32_t seedWarm = startSeed;
    for (int i = 0; i < minAdvances; ++i) {
        rng_next31(seedWarm, backend);
    }

    for (int adv = minAdvances; adv <= maxAdvances; ++adv) {
        uint32_t tmp = seedWarm;
        CrematoriumMeta meta = gen_crematorium_meta_from_seed(tmp, backend);
        if (meta.codePacked == targetCodePacked) {
            out.push_back({adv, seedWarm, meta.codePacked, meta.forced7, meta.forcedPosLSB});
            if ((int)out.size() >= maxResults) break;
        }
        rng_next31(seedWarm, backend);
    }

    return out;
}
