// Class that optimizes the Initial Solution
// June 5, 2020 -> Creation. Implements VND based on Lamghari et al. 2014


#ifndef Optimization_hpp
#define Optimization_hpp

#pragma once
#include <forward_list>
#include "ProcessingOptimizer.hpp"
#include "DataStructures.hpp"

class Optimization{
private:
	DataStructures* Data;

	ProcessingOptimizer* ProcessingOptimizerInstance;
    double TestShiftBeforeTo(MineBlock&, ProcessingOptimizer&, int);
    int BestEarlierPeriod(MineBlock&, ProcessingOptimizer&);

public:
	// Constructor
	Optimization(DataStructures*, ProcessingOptimizer*);

	void GetOptimizedSolution();

	void MakePostponableMineBlockList(int, std::forward_list<MineBlock*>&);

	void MakeExpeditableMineBlockList(int, std::forward_list<MineBlock*>&);

	double TestShiftBefore(MineBlock&, ProcessingOptimizer&);

	void ExecuteShiftBefore(MineBlock&, std::vector<ProcessingOptimizer*>&);

	double TestExchange(MineBlock&, MineBlock&, ProcessingOptimizer&);

	void ExecuteExchange(MineBlock&, MineBlock&, std::vector<ProcessingOptimizer*>&);

	double TestShiftAfter(MineBlock&, ProcessingOptimizer&);

	void ExecuteShiftAfter(MineBlock&, std::vector<ProcessingOptimizer*>&);

	//void ComputeTransferredScenarioRockAndMetalTonnages (GradeToRockAndMetalTonnageMapList&, MineBlockSet&);

	//void DecrementDownstreamData (int, GradeToRockAndMetalTonnageMapList&);

	//void IncrementDownstreamData (int, GradeToRockAndMetalTonnageMapList&);

	//void CleanDownstreamData(int);

	//void IncrementDownstreamData (int, std::vector<ScenarioBlock>&);

	//void DecrementDownstreamData (int, std::vector<ScenarioBlock>&);

};

#endif	/* Optimization_hpp */
