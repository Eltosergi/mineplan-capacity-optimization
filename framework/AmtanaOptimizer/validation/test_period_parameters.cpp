// g++ -std=c++17 -O2 -pthread -I. DataStructures.cpp LoadAndSaveData.cpp
// ProcessingOptimizer.cpp Optimization.cpp validation/test_period_parameters.cpp -o build/test-periods
#include "DataStructures.hpp"
#include "LoadAndSaveData.hpp"
#include "ProcessingOptimizer.hpp"
#include "Optimization.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

void close(double actual, double expected) {
    if (std::abs(actual - expected) > 1e-7 * std::max(1.0, std::abs(expected))) {
        std::cerr << actual << " != " << expected << '\n';
        std::abort();
    }
}

int main() {
    assert(ParsePeriodValues(" 3 ", 3, "test") == std::vector<double>({3, 3, 3}));
    assert(ParsePeriodValues("0, 2,3", 3, "test") == std::vector<double>({0, 2, 3}));
    for (const auto text : {"", "1,2", "1,2,3,4", "1,2,", "1,,3", "-1", "nan", "inf", "2x"}) {
        bool rejected = false;
        try { ParsePeriodValues(text, 3, "test"); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    bool rejected = false;
    try { ParsePeriodValues("0", 3, "rate", true); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    DataStructures data;
    LoadAndSaveData files(&data);
    files.LoadConfiguration("validation/examples/configuration.txt");
    assert(data.Investments == std::vector<double>({0}));
    auto& c = *data.GetConfiguration();
    c["NumberOfPeriods"] = "3";
    c["NumberOfScenarios"] = "1";
    c["AnnualDiscountRate"] = "0.1";
    c["Investment"] = "100,11,0";
    c["MiningCapacity"] = "1,2,0";
    c["MiningCost"] = "1,3,5";
    c["ProcessingCapacity"] = "0.5,0.25,0";
    c["NumberOfRockTypes"] = "1";
    c["RockTypes"] = "R1";
    c["NumberOfMetals"] = "1";
    c["EffectiveMetalPrices"] = "20";
    c["NumberOfOperationalModes"] = "2";
    c["OperationalMode1"] = "Name=A;ProcessingRate=1,2,3;ProcessingCost=2,4,6;Recoveries=1;w=1";
    c["OperationalMode2"] = "Name=B;ProcessingRate=1;ProcessingCost=10;Recoveries=1;w=1";
    data.LoadPeriodParameters();
    close(data.DiscountedInvestment(), 110);
    data.Initializing();
    assert(data.GetMaximumMiningTonnageOfPeriod()->size() == 4);
    close(data.GetMaximumMiningTonnageOfPeriod()->at(0), 1);
    close(data.GetMaximumMiningTonnageOfPeriod()->at(1), 2);
    close(data.GetMaximumMiningTonnageOfPeriod()->at(2), 0);
    MineBlock block;
    block.ScenarioVector.emplace_back(1, std::vector<double>{1}, "R1");
    data.CalculateExpectedCostOfMiningBlockInPeriod(&block);
    data.CalculateExpectedCostOfMiningBlockInPeriod(&block);
    assert(block.ExpectedCostOfMiningBlockInPeriod.size() == 4);
    close(block.ExpectedCostOfMiningBlockInPeriod[1], 3 / 1.1);
    close(block.ExpectedCostOfMiningBlockInPeriod[3], 0);
    ProcessingOptimizer optimizer(&data);
    optimizer.InitializeGlobalProcessingState();
    close(*data.GetNPV(), -110);
    (*data.GetBlocksInPeriod())[0].insert(&block);
    (*data.GetMiningTonnageOfPeriod())[0] = 1;
    *data.GetExpectedMiningCost() = 1;
    optimizer.InitializeGlobalProcessingState();
    close(*data.GetNPV(), -102);
    MineBlockSet moved{&block}, empty;
    close(optimizer.Test(0, moved, 1, empty), -110 + 5 / 1.1);
    close(optimizer.Test(0, moved, 2, empty), -110 - 5 / 1.21);
    close(optimizer.Test(0, moved, 3, empty), -110);
    // Hypothetical tests do not alter the original objective or block assignment.
    close(*data.GetNPV(), -102);
    assert((*data.GetBlocksInPeriod())[0].count(&block));
    optimizer.SavePeriodReport("build/test-period-report.csv");
    (*data.GetBlocksInPeriod())[0].erase(&block);
    (*data.GetBlocksInPeriod())[1].insert(&block);
    *data.GetExpectedMiningCost() = 3 / 1.1;
    optimizer.Execute(0, moved, 1, empty);
    close(*data.GetGlobalOptimizedNPV() - *data.GetExpectedMiningCost() - data.DiscountedInvestment(), -110 + 5 / 1.1);
    // The same block can be shifted back using the period-zero rate/cost again.
    close(optimizer.Test(1, moved, 0, empty), -102);
    // Full VND: a closed final period must not prevent extraction in period zero.
    for (int testCase = 0; testCase < 3; ++testCase) {
    DataStructures scheduled;
    *scheduled.GetConfiguration() = c;
    (*scheduled.GetConfiguration())["NumberOfThreads"] = "3";
    if (testCase == 1) (*scheduled.GetConfiguration())["MiningCapacity"] = "0,2,0";
    if (testCase == 2) (*scheduled.GetConfiguration())["Investment"] = "0";
    scheduled.LoadPeriodParameters();
    MineBlock& ore = (*scheduled.GetOreBody())[1][1][1];
    ore.XIndex = ore.YIndex = ore.ZIndex = 1;
    ore.ConsecutivePosition = 0;
    ore.ScenarioVector.emplace_back(1, std::vector<double>{1}, "R1");
    scheduled.GetConsecutivePositions()->insert(0);
    scheduled.CalculateExpectedCostOfMiningBlockInPeriod(&ore);
    scheduled.Initializing();
    ProcessingOptimizer plant(&scheduled);
    plant.InitializeGlobalProcessingState();
    Optimization schedule(&scheduled, &plant);
    schedule.GetOptimizedSolution();
    assert(scheduled.GetExtractionPeriodOfBlock()->at(&ore) == (testCase == 1 ? 1 : 0));
    close(*scheduled.GetNPV(), testCase == 1 ? -110 + 5 / 1.1 : testCase == 2 ? 8 : -102);
    if (testCase == 0) plant.SavePeriodReport("build/test-scheduled-report.csv");
    }
    std::cout << "Period parameter C++ tests passed\n";
}
