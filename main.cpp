#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

namespace rng {
std::random_device &GetDevice() {
    static std::random_device random_device;
    return random_device;
}

std::mt19937 &GetGenerator() {
    auto &device = GetDevice();
    static std::mt19937 generator(device());
    return generator;
}
}  // namespace rng

class Light {
public:
    [[nodiscard]] bool IsOn() const {
        return is_on;
    }

    [[nodiscard]] bool IsOff() const {
        return not IsOn();
    }

    void TurnOn() {
        is_on = true;
    }

    void TurnOff() {
        is_on = false;
    }

    bool is_on = false;
};

struct PrisonerInput {
    int32_t day_number = 0;
    Light *light = nullptr;
};

enum class PrisonerClaim { claim_nothing, claim_that_everyone_has_been_in_the_room };

class PrisonerBase {
public:
    PrisonerBase(int32_t prisoner_id, int32_t n_prisoners)
        : prisoner_id{prisoner_id}, n_prisoners{n_prisoners} {
    }

    virtual PrisonerClaim TakeAction(PrisonerInput input) = 0;

    int32_t prisoner_id = 0;
    int32_t n_prisoners = 0;
};

class FalsePrisonerClaimException : public std::exception {};

template <class Prisoner>
class Prison {
public:
    explicit Prison(int32_t n_prisoners)
        : n_prisoners{n_prisoners},
          distribution_(0, n_prisoners - 1),
          prisoners_have_been_in_the_room_indicators(n_prisoners) {
        for (int32_t i = 0; i < n_prisoners; ++i) {
            prisoners.emplace_back(i, n_prisoners);
        }
    }

    bool HaveAllPrisonersBeenInTheRoom() {
        return std::all_of(prisoners_have_been_in_the_room_indicators.begin(),
                           prisoners_have_been_in_the_room_indicators.end(),
                           [](bool x) { return x; });
    }

    PrisonerClaim NextDay() {
        auto prisoner_id = distribution_(rng::GetGenerator());
        prisoners_have_been_in_the_room_indicators[prisoner_id] = true;
        auto prisoner_claim = prisoners[prisoner_id].TakeAction({day_number, &light});
        ++day_number;
        return prisoner_claim;
    }

    int32_t Run() {
        while (true) {
            auto prisoner_claim = NextDay();
            if (prisoner_claim == PrisonerClaim::claim_that_everyone_has_been_in_the_room) {
                if (HaveAllPrisonersBeenInTheRoom()) {
                    return day_number;
                } else {
                    throw FalsePrisonerClaimException{};
                }
            }
        }
    }

    [[maybe_unused]] int32_t n_prisoners = 0;
    int32_t day_number = 0;
    Light light = Light{};
    std::vector<Prisoner> prisoners;
    std::vector<bool> prisoners_have_been_in_the_room_indicators;

private:
    std::uniform_int_distribution<int32_t> distribution_;
};

class DedicatedCounterPrisoner : public PrisonerBase {
public:
    using PrisonerBase::PrisonerBase;

    PrisonerClaim TakeAction(PrisonerInput input) override {
        if (prisoner_id == 0) {
            if (input.light->IsOn()) {
                input.light->TurnOff();
                ++times_turned_off_the_light;
            }
            if (times_turned_off_the_light == n_prisoners - 1) {
                return PrisonerClaim::claim_that_everyone_has_been_in_the_room;
            }
        } else {
            if (not has_turned_on_the_light and input.light->IsOff()) {
                input.light->TurnOn();
                has_turned_on_the_light = true;
            }
        }
        return PrisonerClaim::claim_nothing;
    }

    bool has_turned_on_the_light = false;
    int32_t times_turned_off_the_light = 0;
};

class TokenPrisoner : public PrisonerBase {
public:
    TokenPrisoner(int32_t prisoner_id, int32_t n_prisoners)
        : PrisonerBase{prisoner_id, n_prisoners} {
        auto n_prisoners_with_2_tokens =
            (1 << GetClosestNotSmallerPowerOf2(n_prisoners)) - n_prisoners;
        if (prisoner_id < n_prisoners_with_2_tokens) {
            n_tokens = 2;
        } else {
            n_tokens = 1;
        }
    }

    static int32_t GetClosestNotSmallerPowerOf2(int32_t number) {
        return std::ceil(log2(number));
    }

    [[nodiscard]] int32_t GetStageIndex(int32_t day_number) const {
        int32_t first_cycle_interval = n_prisoners * 7;
        int32_t next_cycles_interval = n_prisoners * 3;
        auto n_stages = GetClosestNotSmallerPowerOf2(n_prisoners);

        if (day_number < n_stages * first_cycle_interval) {
            return day_number / first_cycle_interval;
        } else {
            auto tail = day_number - n_stages * first_cycle_interval;
            return tail / next_cycles_interval % n_stages;
        }
    }

    [[nodiscard]] bool IsLastDayOfTheStage(int32_t day_number) const {
        return GetStageIndex(day_number) != GetStageIndex(day_number + 1);
    }

    void MaybeTurnOffLight(PrisonerInput input) {
        if (input.light->IsOff()) {
            return;
        }
        auto stage_index = GetStageIndex(input.day_number);
        auto exchange_rate = 1 << stage_index;
        bool have_matching_bit = n_tokens & exchange_rate;
        if (IsLastDayOfTheStage(input.day_number) or have_matching_bit) {
            n_tokens += exchange_rate;
            input.light->TurnOff();
        }
    }

    void MaybeTurnOnLight(PrisonerInput input) {
        if (input.light->IsOn()) {
            return;
        }
        auto next_day_stage_index = GetStageIndex(input.day_number + 1);
        auto next_day_exchange_rate = 1 << next_day_stage_index;
        auto have_matching_bit = n_tokens & next_day_exchange_rate;
        if (have_matching_bit) {
            n_tokens -= next_day_exchange_rate;
            input.light->TurnOn();
        }
    }

    [[nodiscard]] bool ShouldClaimThatEveryoneHasBeenInTheRoom() const {
        return n_tokens == 1 << GetClosestNotSmallerPowerOf2(n_prisoners);
    }

    PrisonerClaim TakeAction(PrisonerInput input) override {
        MaybeTurnOffLight(input);
        MaybeTurnOnLight(input);

        if (ShouldClaimThatEveryoneHasBeenInTheRoom()) {
            return PrisonerClaim::claim_that_everyone_has_been_in_the_room;
        } else {
            return PrisonerClaim::claim_nothing;
        }
    }

    int32_t n_tokens = 0;
};

template <class Prisoner>
void Test() {
    for (int n_prisoners = 1; n_prisoners <= 100; ++n_prisoners) {
        Prison<Prisoner>(n_prisoners).Run();
    }
}

template <class Prisoner>
void RunPrisonSimulations(int32_t n_prisoners, int32_t n_simulations) {

    Test<Prisoner>();

    std::vector<double> days_prison_ran_for;
    for (int i = 0; i < n_simulations; ++i) {
        auto prison = Prison<Prisoner>(n_prisoners);
        days_prison_ran_for.push_back(prison.Run());
    }

    double days_mean =
        std::reduce(days_prison_ran_for.begin(), days_prison_ran_for.end()) / n_simulations;
    double days_std = 0;
    for (auto i : days_prison_ran_for) {
        days_std += (i - days_mean) * (i - days_mean);
    }
    days_std = std::sqrt(days_std / n_simulations);

    std::cout << "Days mean:\t" << static_cast<int32_t>(days_mean);
    std::cout << "\nDays std:\t" << days_std;
}

int main(int argc, char *argv[]) {
    // Usage: prisoner_class_name [n_prisoners] [n_simulations]

    int32_t n_prisoners = 100;
    int32_t n_simulations = 1000;

    if (argc == 1) {
        throw std::invalid_argument{"Provide Prisoner class name to use."};
    }
    if (argc >= 3) {
        std::istringstream iss{argv[2]};
        iss >> n_prisoners;
    }
    if (argc >= 4) {
        std::istringstream iss{argv[3]};
        iss >> n_simulations;
    }

    if (std::strcmp(argv[1], "DedicatedCounterPrisoner") == 0) {
        RunPrisonSimulations<DedicatedCounterPrisoner>(n_prisoners, n_simulations);
    } else if (std::strcmp(argv[1], "TokenPrisoner") == 0) {
        RunPrisonSimulations<TokenPrisoner>(n_prisoners, n_simulations);
    } else {
        throw std::invalid_argument{"Unknown Prisoner class name."};
    }

    return 0;
}
