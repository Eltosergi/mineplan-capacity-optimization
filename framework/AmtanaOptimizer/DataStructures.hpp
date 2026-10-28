#ifndef DataStructures_hpp
#define DataStructures_hpp

#pragma once
#include <string>
#include "PeriodParameters.hpp"
#include <map>
#include <set>
#include <vector>
#include <list>
#include <forward_list>
#include <unordered_set>

#include <chrono>


// =====================================================================================
//  Versión generalizada a K metales.
//
//  Cada ScenarioBlock almacena el tonelaje de roca, el tonelaje de cada uno de los K
//  metales (vector MetalTonnages) y el tipo de roca del bloque EN ESE ESCENARIO
//  (incertidumbre en los dominios geológicos, cambio del 29-04-2026).
// =====================================================================================
struct ScenarioBlock{
	double RockTonnage;
	// Tonelaje de cada metal contenido en el bloque (generalizado a K metales).
	std::vector <double> MetalTonnages;
	// Tipo de roca del bloque en este escenario.
	std::string RockType;
	// Constructor genérico: K metales.
	ScenarioBlock(double rockTonnage, std::vector <double> metalTonnages, std::string rockType)
		: RockTonnage(rockTonnage), MetalTonnages(std::move(metalTonnages)), RockType(std::move(rockType)) {}
	// Constructor de compatibilidad con el formato antiguo (2 metales).
	ScenarioBlock(double rockTonnage, double metalTonnage, double metalTonnage2, std::string rockType)
		: RockTonnage(rockTonnage), MetalTonnages({metalTonnage, metalTonnage2}), RockType(std::move(rockType)) {}
};


struct MineBlock{
	int XIndex, YIndex, ZIndex;
	int ConsecutivePosition; // Este atributo es para establecer la conexión con el software MineFlow.
	std::vector <ScenarioBlock> ScenarioVector;
	// El atributo ExpectedPriceOfMiningBlockInPeriod no se ocupa. Se podría eliminar.
	std::vector <double> ExpectedCostOfMiningBlockInPeriod, ExpectedPriceOfMiningBlockInPeriod;
	std::forward_list <MineBlock*> ImmediatePredecessorBlocks, ImmediateSuccessorBlocks;
};


// TripleIndexToBlockMap is a kind of matrix in where the orebody is inside
typedef std::map <int, MineBlock> IndexToBlockMap;
typedef std::map <int, IndexToBlockMap> DoubleIndexToBlockMap;
typedef std::map <int, DoubleIndexToBlockMap> TripleIndexToBlockMap;

// These structs are for defining when (period) the mineblocks will be extracted
typedef std::set <MineBlock*> MineBlockSet;
typedef std::vector <MineBlockSet> MineBlockSetVector;

//Abstract data type definitions
typedef std::forward_list<double> FloatList;
typedef std::vector<FloatList> FloatListVector;
typedef std::vector<double> FloatVector;
typedef std::vector<bool> BooleanVector;

//Contextualized data type definitions
struct RockAndMetalTonnage {double RockTonnage; double MetalTonnage; RockAndMetalTonnage(): RockTonnage(0), MetalTonnage(0) {}}; 
typedef std::map <double, RockAndMetalTonnage> GradeToRockAndMetalTonnageMap;
typedef std::forward_list <GradeToRockAndMetalTonnageMap> GradeToRockAndMetalTonnageMapList;
typedef std::vector <GradeToRockAndMetalTonnageMapList> GradeToRockAndMetalTonnageMapListVector;
typedef GradeToRockAndMetalTonnageMap::iterator GradeToRockAndMetalTonnageMapIterator;
typedef std::forward_list <GradeToRockAndMetalTonnageMapIterator> GradeToRockAndMetalTonnageMapIteratorList;
typedef std::vector <GradeToRockAndMetalTonnageMapIteratorList> GradeToRockAndMetalTonnageMapIteratorListVector;
typedef std::forward_list <RockAndMetalTonnage> RockAndMetalTonnageList;
typedef std::vector <RockAndMetalTonnageList> RockAndMetalTonnageListVector;

class DataStructures{
private:
    double TotalDiscountedInvestment = 0;
	// 'Configuration' stores the parameters read from the configuration file 
	std::map <std::string, std::string> Configuration;
	// 'TripleIndexToBlockMap' stores the mineblocks of the orebody. It is like a matrix
	TripleIndexToBlockMap OreBody;
	// 'MaximumMiningTonnageOfPeriod' stores the maximum mining tonnage of a period. Initially, they are all equals based on corresponding parameter 'MiningCapacity'
	// 'MiningTonnageOfPeriod' stores the current mining tonnage of a period. Initially, it stores only 0
	std::vector <double> MaximumMiningTonnageOfPeriod, MiningTonnageOfPeriod;
	// This attribute storage the period when the MineBlock was extracted. Intially they are all in the 'NumberOfPeriod + 1'
	std::map <MineBlock*, int> ExtractionPeriodOfBlock;
	//
	std::map <MineBlock*, int> LastestImmediatePredecessorPeriodOfBlock, EarliestImmediateSuccessorPeriodOfBlock;
	// 
	double ExpectedMiningCost;
	// 'BlocksInPeriod' stores the mineblocks extracted devided by the different periods. Initially they are all in the period 'NumberOfPeriods + 1'
	MineBlockSetVector BlocksInPeriod;

	// Variable que almacena el NPV de la simulación.
	double NPV;
	// Este atributo almacena el conjunto de bloques que MineFlow indica que son parte del UPL
	std::unordered_set <int> ConsecutivePositions;

	// ##################################### Actualizado: matheurística ##########################################
	// El siguiente atributo almacena el mejor valor encontrado por la optimización del procesamiento
	// (matheurística greedy). Este valor NO contiene los costos de minado. Es sólo la optimización.
	double GlobalOptimizedNPV;
	// El siguiente atributo almacena los valores optimizados de procesamiento en cada período
	// (promedio sobre escenarios). De esa manera no tienen que ser recalculados en cada iteración.
	std::vector <double> GlobalValueEachPeriod;
	// Atributo que almacena la cantidad de bloques de cada tipo de roca por período y escenario.
	std::vector <std::vector <std::map <std::string, int>>> GlobalNumberOfBlocksPerRockType;
	// Atributo que indica, por período y escenario, si al menos un modo de operación es ejecutable
	// (es decir, si todos los tipos de roca con w > 0 de algún modo están presentes).
	std::vector <std::vector <bool>> GlobalEnoughTypeRocksForMixing;
	//
	// NOTA: Las estructuras del método simplex / Dantzig-Wolfe (GlobalBase, GlobalcB, GlobalColumnsA,
	// GlobalA, Globalc, Globalb, GlobalB1, GlobalSubProblemA, GlobalSubProblemc y
	// GlobalBlocksPendingToBeDeleted) fueron ELIMINADAS: el nuevo ProcessingOptimizer (matheurística
	// greedy, Leiva et al. 2025, Minerals 15(427)) recalcula cada período/escenario desde los conjuntos
	// de bloques vigentes y no necesita estado matricial persistente. Cualquier código externo que
	// dependiera de ellas (e.g. validate.cpp vía GetB1b) debe migrar a los acumuladores públicos del
	// nuevo ProcessingOptimizer (ProcessingUnderMode, ProcessingPerRockAndMode, AmountOfMetal).
	// ###########################################################################################################
	
	double TotalDurationDW;
	double TotalDurationSM;
	double TotalDurationCopy;

public:
    // Expanded, validated period inputs, loaded before orebody valuation.
    std::vector<double> Investments, MiningCosts, MiningCapacities, ProcessingCapacities;
    bool VariablePeriodParameters = false;
    void LoadPeriodParameters();
    double DiscountedInvestment() const;
	// Constructor
	DataStructures();

	void Initializing();

	std::map <std::string, std::string>* GetConfiguration();

	void CalculateExpectedCostOfMiningBlockInPeriod(MineBlock*);

	double GetRockTonnageOfMineBlock(MineBlock*);

	std::vector <double>* GetMaximumMiningTonnageOfPeriod();

	std::vector <double>* GetMiningTonnageOfPeriod();

	TripleIndexToBlockMap* GetOreBody();

	MineBlockSetVector* GetBlocksInPeriod();

	std::map <MineBlock*, int>* GetExtractionPeriodOfBlock();

	std::map <MineBlock*, int>* GetLastestImmediatePredecessorPeriodOfBlock();

	std::map <MineBlock*, int>* GetEarliestImmediateSuccessorPeriodOfBlock();

	double* GetExpectedMiningCost();

	double* GetNPV();

	std::unordered_set <int>* GetConsecutivePositions();

	// ##################################### Actualizado: matheurística ##########################################
	// El siguiente método obtiene el mejor valor encontrado por la optimización del procesamiento.
	// Este valor NO contiene los costos de minado. Es sólo la optimización.
	double* GetGlobalOptimizedNPV();
	// El siguiente método obtiene los valores optimizados de procesamiento en cada período.
	std::vector <double>* GetGlobalValueEachPeriod();
	// El siguiente método es para obtener el atributo que almacena la cantidad de bloques de cada
	// tipo de roca por período y escenario.
	std::vector <std::vector <std::map <std::string, int>>>* GetGlobalNumberOfBlocksPerRockType();
	// El siguiente método indica, por período y escenario, si al menos un modo de operación es ejecutable.
	std::vector <std::vector <bool>>* GetGlobalEnoughTypeRocksForMixing();
	// ###########################################################################################################
	double* GetTotalDurationDW();
	double* GetTotalDurationSM();
	double* GetTotalDurationCopy();
};

#endif /* DataStructures_hpp */
