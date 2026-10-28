#include <iostream>
#include <string>
#include <math.h>
#include <algorithm>
#include <limits>
#include "DataStructures.hpp"

DataStructures::DataStructures(){}

void DataStructures::LoadPeriodParameters() {
    int n = std::stoi(Configuration.at("NumberOfPeriods"));
    Investments = ParsePeriodValues(Configuration.count("Investment") ? Configuration.at("Investment") : "0", n, "Investment");
    MiningCosts = ParsePeriodValues(Configuration.at("MiningCost"), n, "MiningCost");
    MiningCapacities = ParsePeriodValues(Configuration.at("MiningCapacity"), n, "MiningCapacity");
    ProcessingCapacities = ParsePeriodValues(Configuration.at("ProcessingCapacity"), n, "ProcessingCapacity");
    VariablePeriodParameters = false;
    for (const auto* series : {&MiningCosts, &MiningCapacities, &ProcessingCapacities})
        VariablePeriodParameters = VariablePeriodParameters ||
            !std::all_of(series->begin(), series->end(), [&](double v) { return v == series->front(); });
    const double rate = std::stod(Configuration.at("AnnualDiscountRate"));
    if (!std::isfinite(rate) || rate <= -1) throw std::invalid_argument("Invalid AnnualDiscountRate");
    TotalDiscountedInvestment = 0;
    for (std::size_t t = 0; t < Investments.size(); ++t)
        TotalDiscountedInvestment += Investments[t] / std::pow(1 + rate, t);
}

double DataStructures::DiscountedInvestment() const {
    return TotalDiscountedInvestment;
}

void DataStructures::Initializing(){

	this->NPV = 0;
	this->GlobalOptimizedNPV = 0;

	this->ExpectedMiningCost = 0;

	this->TotalDurationDW = 0;
	this->TotalDurationSM = 0;
	this->TotalDurationCopy = 0;

	// Remember that the first period is 0, that's why the FOR is until NumberOfPeriods-1
	for (int Period = 0; Period <= stoi(this->Configuration["NumberOfPeriods"]); Period++){
		if (Period < stoi(this->Configuration["NumberOfPeriods"])){
			this->MaximumMiningTonnageOfPeriod.push_back (MiningCapacities.at(Period));
			this->MiningTonnageOfPeriod.push_back (0.0);
		} else {
            // The VND also moves blocks to/from the unextracted (fictitious) period.
            this->MaximumMiningTonnageOfPeriod.push_back(std::numeric_limits<double>::infinity());
            this->MiningTonnageOfPeriod.push_back(0.0);
		}
		MineBlockSet NewMineBlockSet;
		
		this->BlocksInPeriod.insert (this->BlocksInPeriod.begin(), NewMineBlockSet);
		// ############################################# Actualizado: matheurística ############################################
		// Inicializa los atributos que serán utilizados por el ProcessingOptimizer.
		// El valor del procesamiento de cada período comienza en 0 (incluido el período ficticio
		// 'NumberOfPeriods', cuyo valor descontado al infinito es siempre 0).
		this->GlobalValueEachPeriod.push_back(0);
		this->GlobalNumberOfBlocksPerRockType.push_back(std::vector <std::map <std::string, int>>());
		this->GlobalEnoughTypeRocksForMixing.push_back(std::vector <bool>());
		for (int Scenario = 0; Scenario < stoi(this->Configuration["NumberOfScenarios"]); Scenario++){
			this->GlobalNumberOfBlocksPerRockType[Period].push_back(std::map <std::string, int>());
			this->GlobalEnoughTypeRocksForMixing[Period].push_back(false);
		}
		// ######################################################################################################################
	}

	// Both BlocksInPeriod and ExtractionPeriodOfBlock are initializing consifering a 'NumberOfPeriods + 1'
	for (TripleIndexToBlockMap::iterator XIterator = this->OreBody.begin(); XIterator != this->OreBody.end(); ++XIterator) {
		for (DoubleIndexToBlockMap::iterator YIterator = XIterator->second.begin(); YIterator !=  XIterator->second.end(); ++YIterator) {
			for (IndexToBlockMap::iterator ZIterator = YIterator->second.begin(); ZIterator !=  YIterator->second.end(); ++ZIterator) {
				// Se pregunta si el bloque pertenece al UPL
				std::unordered_set <int>::const_iterator got = this->ConsecutivePositions.find((&ZIterator->second)->ConsecutivePosition);
				if (got != this->ConsecutivePositions.end()){
					// Dado que es parte del UPL, entonces se agrega al período ficticio y se establecen las relaciones correspondientes.
					this->BlocksInPeriod[stoi(this->Configuration["NumberOfPeriods"])].insert(&ZIterator->second);
                    this->MiningTonnageOfPeriod.back() += GetRockTonnageOfMineBlock(&ZIterator->second);
				}
				
				this->ExtractionPeriodOfBlock[&ZIterator->second] = stoi(this->Configuration["NumberOfPeriods"]);
				this->LastestImmediatePredecessorPeriodOfBlock[&ZIterator->second] = 0;
				this->EarliestImmediateSuccessorPeriodOfBlock[&ZIterator->second] = stoi(this->Configuration["NumberOfPeriods"]);
			}
		}
	}
}

std::map <std::string, std::string>* DataStructures::GetConfiguration(){
	return (&this->Configuration);
}

// Este método debe ser cambiado en algún momento, dado que no se está calculando bien el costo.
// Si todos los bloques tienen el mismo tamaño, entonces funciona. En caso contrario, no.
void DataStructures::CalculateExpectedCostOfMiningBlockInPeriod(MineBlock* mineBlock){
	double AverageRockTonnage = 0;
	for (int Scenarios = 0; Scenarios < mineBlock->ScenarioVector.size(); Scenarios++){
		AverageRockTonnage += mineBlock->ScenarioVector[Scenarios].RockTonnage;
	}
	if (AverageRockTonnage != 0){
		AverageRockTonnage = AverageRockTonnage / mineBlock->ScenarioVector.size();
	}
	for (int Period=0; Period <= stoi(this->Configuration["NumberOfPeriods"]); Period++){
		if (mineBlock->ExpectedCostOfMiningBlockInPeriod.size() == static_cast<std::size_t>(stoi(this->Configuration["NumberOfPeriods"]) + 1)){
			if (Period < stoi(this->Configuration["NumberOfPeriods"])){
				mineBlock->ExpectedCostOfMiningBlockInPeriod[Period] = (AverageRockTonnage * MiningCosts.at(Period)) / (pow (1 + stod(this->Configuration["AnnualDiscountRate"]), Period));
			} else {
				mineBlock->ExpectedCostOfMiningBlockInPeriod[Period] = 0.0;
			}
		} else {
			if (Period < stoi(this->Configuration["NumberOfPeriods"])){
				mineBlock->ExpectedCostOfMiningBlockInPeriod.push_back ((AverageRockTonnage * MiningCosts.at(Period)) / (pow (1 + stod(this->Configuration["AnnualDiscountRate"]), Period)));
			} else {
				mineBlock->ExpectedCostOfMiningBlockInPeriod.push_back (0.0);
			}
		}
	}
}

double DataStructures::GetRockTonnageOfMineBlock(MineBlock* mineBlock){
	double AverageRockTonnage = 0;
	for (int Scenarios = 0; Scenarios < mineBlock->ScenarioVector.size(); Scenarios++){
		AverageRockTonnage += mineBlock->ScenarioVector[Scenarios].RockTonnage;
	}
	if (AverageRockTonnage != 0){
		AverageRockTonnage = AverageRockTonnage / mineBlock->ScenarioVector.size();
	}
	return (AverageRockTonnage);
}

std::vector <double>* DataStructures::GetMaximumMiningTonnageOfPeriod(){
	return (&this->MaximumMiningTonnageOfPeriod);
}

std::vector <double>* DataStructures::GetMiningTonnageOfPeriod(){
	return (&this->MiningTonnageOfPeriod);
}

TripleIndexToBlockMap* DataStructures::GetOreBody(){
	return (&this->OreBody);
}

MineBlockSetVector* DataStructures::GetBlocksInPeriod(){
	return (&this->BlocksInPeriod);
}

std::map <MineBlock*, int>* DataStructures::GetExtractionPeriodOfBlock(){
	return (&this->ExtractionPeriodOfBlock);
}

std::map <MineBlock*, int>* DataStructures::GetLastestImmediatePredecessorPeriodOfBlock(){
	return (&this->LastestImmediatePredecessorPeriodOfBlock);
}

std::map <MineBlock*, int>* DataStructures::GetEarliestImmediateSuccessorPeriodOfBlock(){
	return (&this->EarliestImmediateSuccessorPeriodOfBlock);
}

double* DataStructures::GetExpectedMiningCost(){
	return (&this->ExpectedMiningCost);
}

double* DataStructures::GetNPV(){
	return (&this->NPV);
}

std::unordered_set <int>* DataStructures::GetConsecutivePositions(){
	return(&this->ConsecutivePositions);
}

// ##################################### Actualizado: matheurística ##########################################

double* DataStructures::GetGlobalOptimizedNPV(){
	return (&this->GlobalOptimizedNPV);
}

std::vector <double>* DataStructures::GetGlobalValueEachPeriod(){
	return (&this->GlobalValueEachPeriod);
}

std::vector <std::vector <std::map <std::string, int>>>* DataStructures::GetGlobalNumberOfBlocksPerRockType(){
	return (&this->GlobalNumberOfBlocksPerRockType);
}

std::vector <std::vector <bool>>* DataStructures::GetGlobalEnoughTypeRocksForMixing(){
	return (&this->GlobalEnoughTypeRocksForMixing);
}

// ##########################################################################################################

double* DataStructures::GetTotalDurationDW(){
	return (&this->TotalDurationDW);
}

double* DataStructures::GetTotalDurationSM(){
	        return (&this->TotalDurationSM);
}

double* DataStructures::GetTotalDurationCopy(){
	        return (&this->TotalDurationCopy);
}
