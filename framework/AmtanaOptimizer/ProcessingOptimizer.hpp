#ifndef ProcessingOptimizer_hpp
#define ProcessingOptimizer_hpp

#pragma once
#include <iostream>
#include <limits>
#include <map>
#include <list>
#include <deque>
#include <sstream>
#include <string>
#include <cmath>
#include <algorithm>
#include <vector>
#include "DataStructures.hpp"

// =====================================================================================
//  ProcessingOptimizer basado en la matheurística greedy (Leiva, Lespay, Quelopana,
//  Navarra 2025: "Optimization in Mineral Processing: A Novel Matheuristic for a
//  Variant of the Knapsack Problem", Minerals 15(427)).
//
//  Reemplaza la implementación anterior basada en descomposición de Dantzig-Wolfe y
//  Simplex Dual, conservando EXACTAMENTE la interfaz que utiliza la clase Optimization
//  (VND):
//
//      double Test(CurrentPeriod, TransferBlocksTo, TargetPeriod, TransferBlocksFrom)
//      void   Execute(CurrentPeriod, TransferBlocksTo, TargetPeriod, TransferBlocksFrom)
//
//  Semántica (idéntica a la versión anterior):
//    - TransferBlocksTo  : bloques que se insertarían en TargetPeriod (provenientes de
//                          CurrentPeriod).
//    - TransferBlocksFrom: bloques que se insertarían en CurrentPeriod (provenientes de
//                          TargetPeriod). Vacío => llamado desde Shift-Before/After;
//                          no vacío => llamado desde Exchange.
//    - Test()    evalúa el movimiento de forma HIPOTÉTICA, sin modificar ningún estado
//                global, y retorna el valor TOTAL de la solución resultante:
//                    -(CostoMineroEsperado') + GlobalOptimizedNPV'
//                que es lo que el VND compara contra *Data->GetNPV().
//    - Execute() se invoca DESPUÉS de que Optimization ya actualizó BlocksInPeriod /
//                ExtractionPeriodOfBlock, y consolida GlobalValueEachPeriod,
//                GlobalOptimizedNPV, GlobalNumberOfBlocksPerRockType y
//                GlobalEnoughTypeRocksForMixing para los dos períodos afectados.
//
//  Generalizaciones respecto del código 2x2 original:
//    - N tipos de roca, M modos de operación y K metales.
//    - El tipo de roca de cada bloque es POR ESCENARIO (ScenarioVector[s].RockType),
//      soportando incertidumbre en los dominios geológicos.
//    - Un modo es ejecutable en un período/escenario si TODAS sus rocas activas
//      (w > 0) están presentes; ya no se exige la presencia de todos los tipos de
//      roca declarados ("condición por modo").
//    - El valor de cada período es el PROMEDIO sobre escenarios del óptimo de cada
//      escenario (riesgo neutro), igual que el Dantzig-Wolfe anterior.
//
//  Estrategia de cálculo: recomputación completa. Cada llamada a Test()/Execute()
//  reconstruye el problema de knapsack de los períodos afectados a partir de los
//  conjuntos de bloques vigentes (más/menos los bloques transferidos en el caso de
//  Test). La matheurística es lo suficientemente rápida (O(n log n) por período y
//  escenario) para que esta estrategia sea práctica, y elimina por completo el estado
//  incremental que en la versión DW era fuente de deriva numérica.
//
//  Concurrencia: cada hilo del VND posee su PROPIA instancia de ProcessingOptimizer.
//  Test() sólo LEE DataStructures (los conjuntos hipotéticos se construyen sobre
//  estructuras locales de la instancia), por lo que es seguro ejecutarlo en paralelo.
//  Execute() escribe el estado global y debe invocarse desde un único hilo (como ya
//  hace Optimization).
//
//  Convención de índices ("modo-mayor"):
//      flat(m, r) = m * NumberOfRockTypes + r
// =====================================================================================

struct BlockEntry;

// Una fila del multimap: representa "el bloque b procesado bajo el modo m".
struct OreMode{
	// Cantidad de horas que toma procesar el bloque completo bajo el modo.
	double Hours;
	// Valor (descontado) obtenido al procesar el bloque completo bajo el modo.
	double Value;
	// Fracción del bloque procesada bajo este modo.
	double Fraction;
	// Estado: 0 = sin tocar / -1 = parcialmente procesado / 1 = completado en este modo /
	//         2 = completado bajo otro modo (debe saltarse).
	int State;
	// Referencia a la estructura compartida del bloque.
	BlockEntry* Owner;
	// Referencia al bloque original.
	MineBlock* OreBlock;
};

// Estructura compartida por todas las entradas (una por modo) de un mismo bloque.
struct BlockEntry{
	// Fracción total del bloque procesada, sumando todos los modos.
	double TotalFraction = 0.0;
	// Punteros a las entradas OreMode del bloque (una por modo en que participa).
	std::vector <OreMode*> Entries;
	MineBlock* Block = nullptr;
	int RockType = -1;
};

typedef std::multimap <double, OreMode> OreModeMap;	// clave = Valor/Hora

// Configuración precalculada de un modo de operación: sus rocas activas (w > 0)
// ordenadas por porcentaje decreciente, y los ratios respecto de la roca de referencia.
struct ModeConfig{
	std::vector <int> rocks;	// índices de roca con w > 0, orden w decreciente
	std::vector <double> ratio;	// ratio[k] = w[rocks[k]] / w[rocks[0]]  (ratio[0] = 1)
	double ratioSum = 0.0;		// suma de los ratios (toneladas totales por unidad de referencia)
};

class ProcessingOptimizer{
public:
	// ------------------ Resultados de reporte (valores esperados sobre escenarios) ------------------
	// Reflejan el ÚLTIMO período calculado en modo "con acumulación" (Execute o
	// InitializeGlobalProcessingState). Test() NO los modifica de manera significativa
	// porque trabaja en modo sin acumulación.
	// Toneladas procesadas bajo cada modo de operación. Tamaño M.
	std::vector <double> ProcessingUnderMode;
	// Toneladas procesadas por tipo de roca y modo. Tamaño N x M.
	std::vector <std::vector <double>> ProcessingPerRockAndMode;
	// Cantidad procesada de cada metal. Tamaño K.
	std::vector <double> AmountOfMetal;

	// Reinicia los acumuladores de resultados.
	void ResetResults();
    void SavePeriodReport(const std::string& path);
    double DiscountedProcessingCost = 0;

	// Acceso a los nombres para reportes.
	const std::vector <std::string>& GetRockTypeOrder() const { return RockTypeOrder; }
	const std::vector <std::string>& GetModeNames() const { return ModeNames; }
	int GetNumberOfMetals() const { return NumberOfMetals; }
	int GetNumberOfRockTypes() const { return NumberOfRockTypes; }
	int GetNumberOfOperationalModes() const { return NumberOfOperationalModes; }

	// Constructor: lee y valida los parámetros del archivo de configuración
	// (formato generalizado: NumberOfMetals, EffectiveMetalPrices, RockTypes,
	// NumberOfOperationalModes y líneas OperationalMode).
	ProcessingOptimizer(DataStructures*);

	// Interfaz utilizada por la clase Optimization (VND). Ver comentario de cabecera.
	double Test(int, MineBlockSet&, int, MineBlockSet&);
	void Execute(int, MineBlockSet&, int, MineBlockSet&);

	// Inicialización del estado global de procesamiento a partir de la solución
	// inicial vigente en BlocksInPeriod (vacía o cargada por WarmStart). Calcula
	// GlobalNumberOfBlocksPerRockType, GlobalEnoughTypeRocksForMixing,
	// GlobalValueEachPeriod y GlobalOptimizedNPV para TODOS los períodos, y deja
	// *Data->GetNPV() = -CostoMineroEsperado + GlobalOptimizedNPV. Debe llamarse
	// una vez, después de LoadAndSaveData::Validation() y antes de la optimización.
	void InitializeGlobalProcessingState();

private:
	DataStructures* Data;

	// Atributos relacionados con el archivo de configuración
	int NumberOfPeriods;
	int NumberOfScenarios;
	int NumberOfRockTypes;		// N
	int NumberOfOperationalModes;	// M
	int NumberOfMetals;		// K
	std::vector <double> EffectiveMetalPrices;	// tamaño K
	double DefaultRockTonnage;
	double AnnualDiscountRate;
	double ProcessingCapacity;
    int ActivePeriod = 0;
    std::vector<std::vector<double>> PeriodRates, PeriodCosts;
	std::map <std::string, int> RockTypeNames;	// nombre -> índice de roca
	std::vector <std::string> RockTypeOrder;	// índice de roca -> nombre
	std::vector <std::string> ModeNames;		// índice de modo -> nombre
	std::vector <std::vector <float>> Recoveries;		// [modo][metal]
	std::vector <double> ProcessingRates;			// [modo]
	std::vector <double> ProcessingCosts;			// [modo]
	std::vector <std::vector <float>> ProcessingPercentages;	// [modo][roca]
	std::vector <ModeConfig> ModeConfigs;			// [modo]

	// ------------------ Estado de trabajo POR INSTANCIA (scratch) ------------------
	// Un multimap por cada par (modo, roca): tamaño M x N, indexado con flat(m, r).
	// Se reconstruyen en cada llamada a ComputeScenarioValue.
	std::vector <OreModeMap> OrePerMode;
	// Almacenamiento estable de las estructuras compartidas por bloque.
	std::deque <BlockEntry> EntryPool;
	// Modos ejecutables en el período/escenario en curso.
	std::vector <bool> RunnableMode;

	// Tolerancia de error en los cálculos.
	double error;

	inline int flat(int mode, int rock) const { return mode * NumberOfRockTypes + rock; }

	void buildModeConfigs();

	// Cuenta los bloques por tipo de roca de un conjunto, para un escenario dado,
	// considerando exclusiones e inclusiones hipotéticas (pueden ser nullptr).
	void CountRockTypes(const MineBlockSet& Base, const MineBlockSet* Exclude,
			    const MineBlockSet* Include, int Scenario,
			    std::map <std::string, int>& Counts) const;

	// Determina los modos ejecutables a partir de un conteo de rocas. Llena
	// this->RunnableMode y retorna true si al menos un modo es ejecutable.
	bool ComputeRunnableModes(const std::map <std::string, int>& Counts);

	// Crea las entradas de un bloque en los multimaps de todos los modos ejecutables
	// que usan su tipo de roca (w[modo][roca] > 0), para el escenario dado.
	void insertBlockEntries(MineBlock* block, const ScenarioBlock& scenario, double discountFactor);

	// Núcleo greedy: resuelve el knapsack fraccionario del período/escenario en curso
	// (los multimaps OrePerMode ya deben estar poblados) y retorna el valor óptimo.
	// Si Accumulate es true, suma los resultados físicos (toneladas/metales) a los
	// acumuladores públicos de reporte.
	double GreedyScenario(int ScenarioIndex, bool Accumulate);

	// Valor óptimo del procesamiento de UN escenario de un período, para el conjunto
	// de bloques (Base \ Exclude) U Include.
	double ComputeScenarioValue(int PeriodIndex, int ScenarioIndex,
				    const MineBlockSet& Base, const MineBlockSet* Exclude,
				    const MineBlockSet* Include, bool Accumulate);

	// Valor esperado (promedio sobre escenarios) del procesamiento de un período.
	// Retorna 0 para el período ficticio (PeriodIndex == NumberOfPeriods).
	// Si Accumulate es true, reinicia y llena los acumuladores públicos de reporte
	// con los valores esperados del período.
	double ComputePeriodValue(int PeriodIndex, const MineBlockSet& Base,
				  const MineBlockSet* Exclude, const MineBlockSet* Include,
				  bool Accumulate);

	// Recuenta los tipos de roca de un período (todos los escenarios) desde
	// BlocksInPeriod y actualiza GlobalNumberOfBlocksPerRockType y
	// GlobalEnoughTypeRocksForMixing. SÓLO debe llamarse desde Execute() o
	// InitializeGlobalProcessingState() (escribe estado global).
	void UpdateGlobalRockTypeState(int PeriodIndex);
};

#endif /* ProcessingOptimizer_hpp */
