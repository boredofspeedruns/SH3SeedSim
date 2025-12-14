#include <iostream>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <string>

static inline uint32_t ps2_rand32_step(uint32_t &seed) {
    seed = (seed * 0x41C64E6Du + 0x3039u) & 0x7FFFFFFFu;
    return seed;
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

std::optional<int> find_warmup_for_first(uint32_t baseSeed, uint32_t R_first, int maxSearch = 2000000) {
    uint32_t seed = baseSeed;
    for (int warmup = 0; warmup <= maxSearch; ++warmup) {
        uint32_t tmp = seed;
        uint32_t r = ps2_rand32_step(tmp); 
        if (r == R_first) return warmup;
        ps2_rand32_step(seed);
    }
    return std::nullopt;
}

uint32_t gen_shakespeare_code(uint32_t seed, int warmupAfterReset, bool verbose=false) {
    for (int i = 0; i < warmupAfterReset; ++i) {
        ps2_rand32_step(seed);
    }

    std::vector<int> pool{0,1,2,3,4,5,6,7,8,9};
    uint32_t code4 = 0;

    for (int i = 0; i < 4; ++i) {
        int32_t r = static_cast<int32_t>(ps2_rand32_step(seed));
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

uint32_t gen_clock_puzzle(uint32_t seed, int warmupAfterReset, uint8_t modeByte, bool verbose=false) {
    for (int i = 0; i < warmupAfterReset; ++i) {
        ps2_rand32_step(seed);
    }

    uint32_t rHour = ps2_rand32_step(seed);
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

    uint32_t rMin = ps2_rand32_step(seed);

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
                                               int minWarmup = 0, int maxWarmup = 5000, int maxResults = 50) {
    std::vector<ClockWarmupMatch> matches;
    uint32_t targetPacked = ((targetHour / 10) << 12) | ((targetHour % 10) << 8) |
                            ((targetMinute / 10) << 4) | (targetMinute % 10);

    uint32_t seedWarm = baseSeed;

    for (int i = 0; i < minWarmup; ++i) {
        ps2_rand32_step(seedWarm);
    }

    for (int w = minWarmup; w <= maxWarmup; ++w) {
        uint32_t seed = seedWarm;            
        uint32_t seedAfterWarmup = seedWarm; 

        uint32_t rHour = ps2_rand32_step(seed);
        int hour = (modeByte == 2) ? (int)(rHour % 12) + 12
                                   : (int)(rHour % 12) + 1;
        int h_tens = hour / 10, h_ones = hour % 10;
        uint32_t packed = ((uint32_t)h_tens << 12) | ((uint32_t)h_ones << 8);

        uint32_t rMin = ps2_rand32_step(seed);
        int minute = (int)(rMin % 60);
        int m_tens = minute / 10, m_ones = minute % 10;
        packed |= ((uint32_t)m_tens << 4) | (uint32_t)m_ones;

        if (packed == targetPacked) {
            matches.push_back({w, seedAfterWarmup, rHour, rMin, packed});
            if ((int)matches.size() >= maxResults) break;
        }

        ps2_rand32_step(seedWarm);
    }

    return matches;
}


std::optional<int> find_seed_distance(uint32_t baseSeed, uint32_t targetSeed, int maxSteps = 5000000) {
    if (baseSeed == targetSeed) return 0;
    uint32_t seed = baseSeed;
    for (int n = 1; n <= maxSteps; ++n) {
        ps2_rand32_step(seed);
        if (seed == targetSeed) return n;
    }
    return std::nullopt;
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

int main() {
    std::cout << "Silent Hill 3 RNG tool\n";
    std::cout << "Choose input mode:\n";
    std::cout << "  1) Shakespeare Puzzle: Enter base seed + warmup count directly\n";
    std::cout << "  2) Shakespeare Puzzle: Enter first Shakespeare Puzzle rand() return (auto-find warmup)\n";
    std::cout << "  3) Shakespeare Puzzle: Enter Shakespeare Puzzle seed directly\n";
    std::cout << "  4) Clock puzzle: Generate HH:MM from seed/warmups + mode byte\n";
    std::cout << "  5) Clock puzzle: Reverse (enter HH:MM -> list possible seeds)\n";
    std::cout << "  6) Clock puzzle: Auto-find warmup(s) from base seed + target HH:MM\n";
    std::cout << "  7) RNG: Continuous warmup distances (base -> target1 -> target2 ...)\n";
    std::cout << "Mode (1/2/3/4/5/6/7): ";

    int mode = 1;
    std::cin >> mode;

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

        auto w = find_warmup_for_first(baseSeed, R_first);
        if (!w) {
            std::cout << "Warmup not found in search range.\n";
            return 0;
        }
        warmup = *w;
        std::cout << "Auto-found warmup = " << warmup << "\n\n";

        uint32_t code = gen_shakespeare_code(baseSeed, warmup, true);
        print_shakespeare(code);
        return 0;

    } else if (mode == 3) {
        std::cout << "Enter Shakespeare Puzzle base seed (hex, no 0x): ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        std::cout << "Warmups after that base seed: ";
        std::cin >> warmup;
        std::cout << "\n";

        uint32_t code = gen_shakespeare_code(baseSeed, warmup, true);
        print_shakespeare(code);
        return 0;

    } else if (mode == 4) {
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
        uint32_t packed = gen_clock_puzzle(baseSeed, warmup, modeByte, true);
        print_clock(packed);
        return 0;

    } else if (mode == 5) {
        std::cout << "Enter target hour (decimal): ";
        int targetHour; std::cin >> targetHour;
        std::cout << "Enter target minute (decimal 0-59): ";
        int targetMinute; std::cin >> targetMinute;

        std::cout << "Enter base seed to measure advances from (hex, no 0x): ";
        uint32_t base = 0;
        std::cin >> std::hex >> base;
        std::cin >> std::dec;

        char modeInput;
        std::cout << "24h path option (y/n): ";
        std::cin >> modeInput;
        uint8_t modeByte = (modeInput == 'y' || modeInput == 'Y') ? 2 : 0;

        int minWarmup;
        std::cout << "Min advances to start searching from (decimal, e.g. 0): ";
        std::cin >> minWarmup;

        int maxWarmup;
        std::cout << "Max advances to search up to (decimal, e.g. 500000): ";
        std::cin >> maxWarmup;

        int maxResults;
        std::cout << "Max matches to show (decimal, e.g. 20): ";
        std::cin >> maxResults;

        auto matches = find_clock_warmups(base, modeByte, targetHour, targetMinute,
                                          minWarmup, maxWarmup, maxResults);

        if (matches.empty()) {
            std::cout << "\nNo advances in [" << minWarmup << ".." << maxWarmup << "] produced "
                      << targetHour << ":" << (targetMinute < 10 ? "0" : "") << targetMinute << ".\n";
            return 0;
        }

        std::cout << "\nMatches for "
                  << targetHour << ":" << (targetMinute < 10 ? "0" : "") << targetMinute
                  << " from base seed 0x" << std::hex << std::uppercase << base << std::dec
                  << " (each advance = 1 rand step):\n";

        for (size_t i = 0; i < matches.size(); ++i) {
            const auto &m = matches[i];
            std::cout << "  [" << i << "] advances=" << m.warmup
                      << "  seed@advance=0x" << std::hex << std::uppercase << m.seedAfterWarmup << std::dec
                      << "  rHour=0x" << std::hex << std::uppercase << m.rHour << std::dec
                      << "  rMin=0x" << std::hex << std::uppercase << m.rMin << std::dec
                      << "  packed=0x" << std::hex << std::uppercase << m.packed << std::dec
                      << "\n";
        }

        // Show the earliest match (smallest advances) as the "best" match.
        const auto &best = matches.front();
        std::cout << "\nEarliest match: advances=" << best.warmup
                  << "  seed@advance=0x" << std::hex << std::uppercase << best.seedAfterWarmup << std::dec << "\n";

        // Optional sanity check: generate the clock time starting from that seed@advance with 0 additional warmup.
        uint32_t packed = gen_clock_puzzle(best.seedAfterWarmup, /*warmupAfterReset=*/0, modeByte, false);
        std::cout << "Sanity-check packed=0x" << std::hex << std::uppercase << packed << std::dec << "\n";
        print_clock(packed);

        return 0;

    } else if (mode == 6) {
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

        auto matches = find_clock_warmups(baseSeed, modeByte, targetHour, targetMinute, minWarmup, maxWarmup, maxResults);
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
    } else if (mode == 7) {
        std::cout << "Enter base seed (hex, no 0x). NTSC: 46CBEAC1\n";
        std::cout << "Base seed: ";
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

            auto dist = find_seed_distance(currentSeed, targetSeed, maxSteps);
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
    } else {
        std::cout << "Enter base seed (hex, no 0x). For new-game stream use 0: ";
        std::cin >> std::hex >> baseSeed;
        std::cin >> std::dec;

        std::cout << "Warmup rand() calls after that base seed: ";
        std::cin >> warmup;
        std::cout << "\n";

        uint32_t code = gen_shakespeare_code(baseSeed, warmup, true);
        print_shakespeare(code);
        return 0;
    }
}