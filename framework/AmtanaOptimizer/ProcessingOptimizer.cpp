#include "ProcessingOptimizer.hpp"
#include <fstream>
#include <iomanip>

// =====================================================================================
//  ProcessingOptimizer generalizado (matheurística greedy) integrado al framework de
//  planificación minera (VND + UPL). Ver comentario de cabecera en el .hpp.
//
//  La heurística greedy del paper se conserva intacta:
//    1. Cada bloque genera una entrada (Valor/Hora) en el multimap de cada par
//       (modo, roca) en que puede participar.
//    2. En cada iteración se elige el modo con mayor Valor/Hora combinado
//       (suma ponderada por los ratios de mezcla de sus rocas activas).
//    3. Se procesa simultáneamente una porción de los mejores bloques de cada roca
//       activa del modo, manteniendo las proporciones w del modo. La cantidad queda
//       limitada por el bloque que se agota primero o por la capacidad de planta.
//
//  Para N = 2 rocas y M = 2 modos, esta formulación es matemáticamente equivalente a
//  los "Caso 1 / Caso 2" del código original: ambos casos correspondían a las dos
//  identidades posibles del mínimo  x = min_k (disponible_k / ratio_k).
// =====================================================================================

namespace {
	// Divide un string por un delimitador.
	std::vector<std::string> splitString(const std::string& s, char delimiter) {
		std::stringstream ss(s);
		std::string item;
		std::vector<std::string> out;
		while (std::getline(ss, item, delimiter)) {
			out.push_back(item);
		}
		return out;
	}
}

ProcessingOptimizer::ProcessingOptimizer(DataStructures* data) : Data(data), error(0.00000001) {
	auto config = Data->GetConfiguration();

	// ---------------------------------------------------------------------------------
	// Variables numéricas generales
	// ---------------------------------------------------------------------------------
	NumberOfPeriods = std::stoi(config->at("NumberOfPeriods"));
	NumberOfScenarios = std::stoi(config->at("NumberOfScenarios"));
	NumberOfRockTypes = std::stoi(config->at("NumberOfRockTypes"));
	NumberOfOperationalModes = std::stoi(config->at("NumberOfOperationalModes"));
	NumberOfMetals = std::stoi(config->at("NumberOfMetals"));
	DefaultRockTonnage = std::stod(config->at("DefaultRockTonnage"));
	AnnualDiscountRate = std::stod(config->at("AnnualDiscountRate"));
	ProcessingCapacity = Data->ProcessingCapacities.at(0);

	// ---------------------------------------------------------------------------------
	// Precios efectivos de los K metales: "EffectiveMetalPrices=13228,0"
	// ---------------------------------------------------------------------------------
	{
		auto tokens = splitString(config->at("EffectiveMetalPrices"), ',');
		if (static_cast<int>(tokens.size()) != NumberOfMetals) {
			std::cout << "ERROR: EffectiveMetalPrices must contain " << NumberOfMetals
				  << " values (NumberOfMetals)." << std::endl;
			exit(-1);
		}
		for (const auto& t : tokens) EffectiveMetalPrices.push_back(std::stod(t));
	}

	// ---------------------------------------------------------------------------------
	// Tipos de roca: "RockTypes=A,B,C"  (el orden define el índice de cada roca)
	// ---------------------------------------------------------------------------------
	{
		auto tokens = splitString(config->at("RockTypes"), ',');
		if (static_cast<int>(tokens.size()) != NumberOfRockTypes) {
			std::cout << "ERROR: RockTypes must contain " << NumberOfRockTypes
				  << " names (NumberOfRockTypes)." << std::endl;
			exit(-1);
		}
		for (int r = 0; r < NumberOfRockTypes; ++r) {
			RockTypeNames[tokens[r]] = r;
			RockTypeOrder.push_back(tokens[r]);
		}
	}

	// ---------------------------------------------------------------------------------
	// Validación: los tipos de roca leídos desde los archivos de escenario deben
	// coincidir exactamente con los nombres declarados en la configuración. Se valida
	// aquí (una sola pasada sobre el orebody) para abortar de forma temprana y con un
	// mensaje claro, en lugar de fallar en medio del VND.
	// ---------------------------------------------------------------------------------
	for (TripleIndexToBlockMap::iterator XIterator = Data->GetOreBody()->begin(); XIterator != Data->GetOreBody()->end(); ++XIterator) {
		for (DoubleIndexToBlockMap::iterator YIterator = XIterator->second.begin(); YIterator != XIterator->second.end(); ++YIterator) {
			for (IndexToBlockMap::iterator ZIterator = YIterator->second.begin(); ZIterator != YIterator->second.end(); ++ZIterator) {
				for (size_t Scenario = 0; Scenario < ZIterator->second.ScenarioVector.size(); ++Scenario) {
					const std::string& RockType = ZIterator->second.ScenarioVector[Scenario].RockType;
					if (RockTypeNames.count(RockType) == 0) {
						std::cout << "ERROR: El tipo de roca \"" << RockType << "\" del bloque ("
							  << ZIterator->second.XIndex << ", " << ZIterator->second.YIndex << ", "
							  << ZIterator->second.ZIndex << ") en el escenario " << Scenario
							  << " no existe en RockTypes del archivo de configuración." << std::endl;
						std::cout << "Los tipos de roca válidos son: ";
						for (const auto& Valid : RockTypeOrder) std::cout << "\"" << Valid << "\" ";
						std::cout << std::endl;
						std::cout << "Class: ProcessingOptimizer." << std::endl;
						std::cout << "Method: ProcessingOptimizer(DataStructures*)." << std::endl;
						exit(-1);
					}
				}
			}
		}
	}

	// ---------------------------------------------------------------------------------
	// Modos de operación:
	// "OperationalModeI" = "Name=1;ProcessingRate=425;ProcessingCost=13;Recoveries=0.89,0.89;w=0.70,0.30"
	// ---------------------------------------------------------------------------------
	for (int m = 0; m < NumberOfOperationalModes; ++m) {
		std::string key = "OperationalMode" + std::to_string(m + 1);
		if (config->count(key) == 0) {
			std::cout << "ERROR: Missing configuration line for " << key << "." << std::endl;
			exit(-1);
		}
		std::map<std::string, std::string> fields;
		for (const auto& token : splitString(config->at(key), ';')) {
			auto pos = token.find('=');
			if (pos != std::string::npos) {
				fields[token.substr(0, pos)] = token.substr(pos + 1);
			}
		}
		for (const std::string required : {"Name", "ProcessingRate", "ProcessingCost", "Recoveries", "w"}) {
			if (fields.count(required) == 0) {
				std::cout << "ERROR: Field '" << required << "' missing in " << key << "." << std::endl;
				exit(-1);
			}
		}
		ModeNames.push_back(fields["Name"]);
		PeriodRates.push_back(ParsePeriodValues(fields["ProcessingRate"], NumberOfPeriods, "ProcessingRate", true));
        ProcessingRates.push_back(PeriodRates.back()[0]);
		PeriodCosts.push_back(ParsePeriodValues(fields["ProcessingCost"], NumberOfPeriods, "ProcessingCost"));
        ProcessingCosts.push_back(PeriodCosts.back()[0]);

		// Recuperaciones: una por metal.
		auto recTokens = splitString(fields["Recoveries"], ',');
		if (static_cast<int>(recTokens.size()) != NumberOfMetals) {
			std::cout << "ERROR: 'Recoveries' in " << key << " must contain " << NumberOfMetals
				  << " values (NumberOfMetals)." << std::endl;
			exit(-1);
		}
		std::vector<float> recoveries;
		for (const auto& t : recTokens) recoveries.push_back(std::stof(t));
		Recoveries.push_back(std::move(recoveries));

		// Porcentajes de mezcla: uno por tipo de roca, en el orden de RockTypes (0 = no usa la roca).
		auto wTokens = splitString(fields["w"], ',');
		if (static_cast<int>(wTokens.size()) != NumberOfRockTypes) {
			std::cout << "ERROR: 'w' in " << key << " must contain " << NumberOfRockTypes
				  << " values (NumberOfRockTypes)." << std::endl;
			exit(-1);
		}
		std::vector<float> percentages;
		bool anyPositive = false;
		for (const auto& t : wTokens) {
			float w = std::stof(t);
			if (w < 0.0f) {
				std::cout << "ERROR: Negative blending percentage in " << key << "." << std::endl;
				exit(-1);
			}
			anyPositive = anyPositive || (w > 0.0f);
			percentages.push_back(w);
		}
		if (!anyPositive) {
			std::cout << "ERROR: All blending percentages are zero in " << key << "." << std::endl;
			exit(-1);
		}
		ProcessingPercentages.push_back(std::move(percentages));
	}

	buildModeConfigs();

	// ---------------------------------------------------------------------------------
	// Estructuras de trabajo
	// ---------------------------------------------------------------------------------
	// Un multimap por cada par (modo, roca)
	OrePerMode.resize(NumberOfOperationalModes * NumberOfRockTypes);
	RunnableMode.assign(NumberOfOperationalModes, false);

	ResetResults();
}

// Para cada modo: rocas activas (w > 0) ordenadas por porcentaje decreciente y ratios
// respecto de la roca de referencia (la de mayor porcentaje).
// Nota: los ratios se calculan con división float (como en la versión original) y luego
// se almacenan como double, para reproducir exactamente la aritmética del código 2x2.
void ProcessingOptimizer::buildModeConfigs() {
	ModeConfigs.assign(NumberOfOperationalModes, ModeConfig());

	for (int m = 0; m < NumberOfOperationalModes; ++m) {
		std::vector<std::pair<float, int>> rocks;	// (porcentaje, índice de roca)
		for (int r = 0; r < NumberOfRockTypes; ++r) {
			if (ProcessingPercentages[m][r] > 0.0f) {
				rocks.emplace_back(ProcessingPercentages[m][r], r);
			}
		}
		// Orden por porcentaje decreciente. Empates: índice de roca menor primero
		// (estable y determinista; coincide con el ">=" del código original).
		std::stable_sort(rocks.begin(), rocks.end(),
				 [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });

		ModeConfig cfg;
		cfg.rocks.reserve(rocks.size());
		cfg.ratio.reserve(rocks.size());
		for (size_t k = 0; k < rocks.size(); ++k) {
			cfg.rocks.push_back(rocks[k].second);
			// División en float, como el original, ampliada después a double.
			float ratioFloat = rocks[k].first / rocks[0].first;
			cfg.ratio.push_back(static_cast<double>(ratioFloat));
			cfg.ratioSum += static_cast<double>(ratioFloat);
		}
		ModeConfigs[m] = std::move(cfg);
	}
}

void ProcessingOptimizer::ResetResults() {
	ProcessingUnderMode.assign(NumberOfOperationalModes, 0.0);
	ProcessingPerRockAndMode.assign(NumberOfRockTypes, std::vector<double>(NumberOfOperationalModes, 0.0));
	AmountOfMetal.assign(NumberOfMetals, 0.0);
}

// =====================================================================================
//  Utilitarios de conteo de tipos de roca y modos ejecutables
// =====================================================================================

void ProcessingOptimizer::CountRockTypes(const MineBlockSet& Base, const MineBlockSet* Exclude,
					 const MineBlockSet* Include, int Scenario,
					 std::map <std::string, int>& Counts) const {
	Counts.clear();
	for (const auto& Name : RockTypeOrder) Counts[Name] = 0;

	for (MineBlockSet::const_iterator BlockIterator = Base.begin(); BlockIterator != Base.end(); ++BlockIterator) {
		if (Exclude != nullptr && Exclude->find(*BlockIterator) != Exclude->end()) continue;
		Counts[(*BlockIterator)->ScenarioVector[Scenario].RockType] += 1;
	}
	if (Include != nullptr) {
		for (MineBlockSet::const_iterator BlockIterator = Include->begin(); BlockIterator != Include->end(); ++BlockIterator) {
			Counts[(*BlockIterator)->ScenarioVector[Scenario].RockType] += 1;
		}
	}
}

// Un modo es ejecutable en el período/escenario si TODAS sus rocas activas (w > 0)
// están presentes. (Condición "por modo", que generaliza la antigua condición global
// que exigía la presencia de todos los tipos de roca declarados.)
bool ProcessingOptimizer::ComputeRunnableModes(const std::map <std::string, int>& Counts) {
	bool anyRunnable = false;
	for (int m = 0; m < NumberOfOperationalModes; ++m) {
		bool runnable = !ModeConfigs[m].rocks.empty();
		for (int rock : ModeConfigs[m].rocks) {
			std::map <std::string, int>::const_iterator it = Counts.find(RockTypeOrder[rock]);
			if (it == Counts.end() || it->second == 0) {
				runnable = false;
				break;
			}
		}
		RunnableMode[m] = runnable;
		anyRunnable = anyRunnable || runnable;
	}
	return anyRunnable;
}

// =====================================================================================
//  Construcción de entradas y núcleo greedy
// =====================================================================================

// Crea las entradas de un bloque en los multimaps de todos los modos ejecutables
// que usan su tipo de roca (w[modo][roca] > 0).
void ProcessingOptimizer::insertBlockEntries(MineBlock* block, const ScenarioBlock& scenario, double discountFactor) {

	std::map<std::string, int>::iterator itRock = RockTypeNames.find(scenario.RockType);
	if (itRock == RockTypeNames.end()) {
		std::cout << "ERROR: Type of Rock NOT found: '" << scenario.RockType << "'." << std::endl;
		std::cout << "Class: ProcessingOptimizer. Method: insertBlockEntries." << std::endl;
		exit(-1);
	}
	const int rock = itRock->second;

	EntryPool.emplace_back();
	BlockEntry& owner = EntryPool.back();
	owner.Block = block;
	owner.RockType = rock;

	for (int m = 0; m < NumberOfOperationalModes; ++m) {
		if (!RunnableMode[m] || ProcessingPercentages[m][rock] <= 0.0f) {
			continue;
		}

		// Nota: como en la versión original, las horas de procesamiento se calculan con
		// DefaultRockTonnage (bloques de masa uniforme). Si en el futuro los bloques
		// tienen masas distintas, reemplazar por scenario.RockTonnage.
		double hours = DefaultRockTonnage / ProcessingRates[m];
		double value = 0.0;
		for (int j = 0; j < NumberOfMetals; ++j) {
			value += scenario.MetalTonnages[j] * Recoveries[m][j] * EffectiveMetalPrices[j];
		}
		value = (value - scenario.RockTonnage * ProcessingCosts[m]) * discountFactor;

		OreModeMap::iterator it = OrePerMode[flat(m, rock)].insert(std::pair<double, OreMode>(value / hours, OreMode()));
		OreMode& entry = it->second;
		entry.Hours = hours;
		entry.Value = value;
		entry.Fraction = 0;
		entry.State = 0;
		entry.OreBlock = block;
		entry.Owner = &owner;
		owner.Entries.push_back(&entry);
	}

	// Si la roca no participa en ningún modo ejecutable, el bloque no entra a la optimización.
	if (owner.Entries.empty()) {
		EntryPool.pop_back();
	}
}

double ProcessingOptimizer::GreedyScenario(int ScenarioIndex, bool Accumulate) {

	const int totalRockModes = NumberOfOperationalModes * NumberOfRockTypes;

	// Acumuladores LOCALES del escenario. La capacidad de planta se controla con estos
	// valores (cada escenario es un problema independiente con su propia capacidad).
	std::vector<double> LocalUnderMode(NumberOfOperationalModes, 0.0);
	std::vector<std::vector<double>> LocalPerRockAndMode(NumberOfRockTypes, std::vector<double>(NumberOfOperationalModes, 0.0));
	std::vector<double> LocalAmountOfMetal(NumberOfMetals, 0.0);

	// Iteradores inversos (mayor Valor/Hora primero) y banderas de término por (modo, roca).
	std::vector<OreModeMap::reverse_iterator> RIterators;
	std::vector<bool> RockModeEnd;
	RIterators.reserve(totalRockModes);
	RockModeEnd.reserve(totalRockModes);
	for (int i = 0; i < totalRockModes; ++i) {
		RIterators.push_back(OrePerMode[i].rbegin());
		RockModeEnd.push_back(false);
	}

	double CumulativeNPV = 0.0;
	const double tolerance = error * 1000;	// misma tolerancia que el original (1e-5)

	// Un bloque está completo si su fracción total procesada alcanza 1 (con tolerancia).
	auto isBlockComplete = [&](const BlockEntry* owner) -> bool {
		return owner->TotalFraction >= 1.0 - tolerance;
	};

	// Avanza el iterador de un (modo, roca) saltando entradas ya consumidas en otros
	// modos (State == 2) o cuyos bloques ya están completos; marca el fin si corresponde.
	auto normalizeIterator = [&](int idx) -> void {
		while (RIterators[idx] != OrePerMode[idx].rend() &&
		       (RIterators[idx]->second.State == 2 || isBlockComplete(RIterators[idx]->second.Owner))) {
			++RIterators[idx];
		}
		if (RIterators[idx] == OrePerMode[idx].rend()) {
			RockModeEnd[idx] = true;
		}
	};

	auto advanceIterator = [&](int idx) -> void {
		if (RIterators[idx] != OrePerMode[idx].rend()) {
			++RIterators[idx];
		}
		normalizeIterator(idx);
	};

	// Valor/Hora combinado del modo m: suma ponderada por los ratios de mezcla de los
	// mejores bloques disponibles de cada roca activa. Para N = 2 se reduce exactamente a
	// la métrica original: key(mayor) + key(menor) * rate.
	auto calculateValuePerHour = [&](int m) -> double {
		if (!RunnableMode[m]) {
			return -std::numeric_limits<double>::infinity();
		}
		const ModeConfig& cfg = ModeConfigs[m];
		double total = 0.0;
		for (size_t k = 0; k < cfg.rocks.size(); ++k) {
			const int idx = flat(m, cfg.rocks[k]);
			normalizeIterator(idx);
			if (RockModeEnd[idx]) {
				return -std::numeric_limits<double>::infinity();
			}
			total += RIterators[idx]->first * cfg.ratio[k];
		}
		return total;
	};

	// Procesa una fracción de un bloque bajo un modo, actualizando NPV y acumuladores.
	auto processBlock = [&](int modeIndex, int rockIndex, double fraction) -> void {
		OreMode& entry = RIterators[flat(modeIndex, rockIndex)]->second;

		entry.Fraction += fraction;
		entry.Owner->TotalFraction += fraction;
		CumulativeNPV += entry.Value * fraction;
        if (Accumulate) DiscountedProcessingCost += fraction * entry.OreBlock->ScenarioVector[ScenarioIndex].RockTonnage
            * ProcessingCosts[modeIndex] / std::pow(1 + AnnualDiscountRate, ActivePeriod);

		double tonnageIncrement = DefaultRockTonnage * fraction;
		LocalUnderMode[modeIndex] += tonnageIncrement;
		LocalPerRockAndMode[rockIndex][modeIndex] += tonnageIncrement;

		// Los metales del escenario en curso (cada escenario aporta sus propios metales).
		const std::vector<double>& metals = entry.OreBlock->ScenarioVector[ScenarioIndex].MetalTonnages;
		for (int j = 0; j < NumberOfMetals; ++j) {
			LocalAmountOfMetal[j] += fraction * metals[j];
		}
	};

	// Horas de planta consumidas por todos los modos (en este escenario).
	auto hoursUsed = [&]() -> double {
		double total = 0.0;
		for (int m = 0; m < NumberOfOperationalModes; ++m) {
			total += LocalUnderMode[m] / ProcessingRates[m];
		}
		return total;
	};

	// Valores por hora iniciales de cada modo
	std::vector<double> ValuePerHour(NumberOfOperationalModes);
	for (int m = 0; m < NumberOfOperationalModes; ++m) {
		ValuePerHour[m] = calculateValuePerHour(m);
	}

	while (true) {
		// Elegir el modo con mayor Valor/Hora (en empates gana el de menor índice,
		// igual que el ">=" entre A y B del código original).
		int bestMode = 0;
		for (int m = 1; m < NumberOfOperationalModes; ++m) {
			if (ValuePerHour[m] > ValuePerHour[bestMode]) {
				bestMode = m;
			}
		}

		if (!(ValuePerHour[bestMode] > 0) || hoursUsed() >= ProcessingCapacity) {
			break;
		}

		const ModeConfig& cfg = ModeConfigs[bestMode];
		const size_t numRocks = cfg.rocks.size();

		// x = fracción (en unidades de la roca de referencia) que se procesará.
		// La roca k consume ratio_k * x; el bloque limitante es el que minimiza
		// disponible_k / ratio_k. La capacidad de planta impone una cota adicional.
		double xBlocks = std::numeric_limits<double>::infinity();
		for (size_t k = 0; k < numRocks; ++k) {
			const OreMode& entry = RIterators[flat(bestMode, cfg.rocks[k])]->second;
			double available = 1.0 - entry.Owner->TotalFraction;
			if (available < 0.0) available = 0.0;
			double xCandidate = available / cfg.ratio[k];
			if (xCandidate < xBlocks) {
				xBlocks = xCandidate;
			}
		}

		// Cota por capacidad: toneladas adicionales = T * x * ratioSum;
		// horas adicionales = toneladas / ProcessingRate[modo].
		double remainingHours = ProcessingCapacity - hoursUsed();
		double xCapacity = remainingHours * ProcessingRates[bestMode] / (DefaultRockTonnage * cfg.ratioSum);
		bool maxCapacityReached = (xCapacity < xBlocks);
		double x = maxCapacityReached ? xCapacity : xBlocks;
		if (x < 0.0) x = 0.0;

		// Procesar simultáneamente las rocas activas del modo, manteniendo proporciones.
		for (size_t k = 0; k < numRocks; ++k) {
			processBlock(bestMode, cfg.rocks[k], cfg.ratio[k] * x);
		}

		// Actualizar estados y avanzar iteradores.
		for (size_t k = 0; k < numRocks; ++k) {
			const int idx = flat(bestMode, cfg.rocks[k]);
			OreMode& entry = RIterators[idx]->second;

			if (isBlockComplete(entry.Owner)) {
				// Bloque completo: marcar esta entrada como completada y sus entradas
				// hermanas (en los demás modos) como consumidas, para que se salten.
				for (OreMode* sibling : entry.Owner->Entries) {
					sibling->State = (sibling == &entry) ? 1 : 2;
				}
				if (!maxCapacityReached) {
					advanceIterator(idx);
				}
			} else {
				// Bloque parcial: todas sus entradas quedan en estado -1 y permanecen
				// al frente de sus multimaps para completarse en iteraciones futuras.
				for (OreMode* sibling : entry.Owner->Entries) {
					sibling->State = -1;
				}
			}
		}

		if (maxCapacityReached) {
			break;	// la capacidad de planta quedó copada
		}

		// Recalcular los valores por hora de todos los modos (los multimaps comparten bloques).
		for (int m = 0; m < NumberOfOperationalModes; ++m) {
			ValuePerHour[m] = calculateValuePerHour(m);
		}
	}

	// Acumulación en los resultados públicos de reporte (sólo Execute / Initialize).
	if (Accumulate) {
		for (int m = 0; m < NumberOfOperationalModes; ++m) {
			ProcessingUnderMode[m] += LocalUnderMode[m];
			for (int r = 0; r < NumberOfRockTypes; ++r) {
				ProcessingPerRockAndMode[r][m] += LocalPerRockAndMode[r][m];
			}
		}
		for (int j = 0; j < NumberOfMetals; ++j) {
			AmountOfMetal[j] += LocalAmountOfMetal[j];
		}
	}

	return CumulativeNPV;
}

// =====================================================================================
//  Valor de un escenario y de un período
// =====================================================================================

double ProcessingOptimizer::ComputeScenarioValue(int PeriodIndex, int ScenarioIndex,
						 const MineBlockSet& Base, const MineBlockSet* Exclude,
						 const MineBlockSet* Include, bool Accumulate) {

    ActivePeriod = PeriodIndex;
    ProcessingCapacity = Data->ProcessingCapacities.at(PeriodIndex);
    for (int m = 0; m < NumberOfOperationalModes; ++m) {
        ProcessingRates[m] = PeriodRates[m].at(PeriodIndex);
        ProcessingCosts[m] = PeriodCosts[m].at(PeriodIndex);
    }
	// Modos ejecutables en este período/escenario (condición por modo).
	std::map <std::string, int> Counts;
	this->CountRockTypes(Base, Exclude, Include, ScenarioIndex, Counts);
	if (!this->ComputeRunnableModes(Counts)) {
		return 0.0;
	}

	// Limpieza de las estructuras de trabajo del escenario anterior.
	for (auto& oreMap : OrePerMode) oreMap.clear();
	EntryPool.clear();

	// Factor de descuento precalculado para este período.
	double discountFactor = 1.0 / std::pow(1 + AnnualDiscountRate, PeriodIndex);

	// Poblar los multimaps (Valor/Hora) con los bloques del conjunto hipotético.
	for (MineBlockSet::const_iterator BlockIterator = Base.begin(); BlockIterator != Base.end(); ++BlockIterator) {
		if (Exclude != nullptr && Exclude->find(*BlockIterator) != Exclude->end()) continue;
		this->insertBlockEntries(*BlockIterator, (*BlockIterator)->ScenarioVector[ScenarioIndex], discountFactor);
	}
	if (Include != nullptr) {
		for (MineBlockSet::const_iterator BlockIterator = Include->begin(); BlockIterator != Include->end(); ++BlockIterator) {
			this->insertBlockEntries(*BlockIterator, (*BlockIterator)->ScenarioVector[ScenarioIndex], discountFactor);
		}
	}

	// Resolución greedy del knapsack fraccionario del escenario.
	return this->GreedyScenario(ScenarioIndex, Accumulate);
}

double ProcessingOptimizer::ComputePeriodValue(int PeriodIndex, const MineBlockSet& Base,
					       const MineBlockSet* Exclude, const MineBlockSet* Include,
					       bool Accumulate) {

	// En el período ficticio no hay que calcular nada (valores descontados al infinito son 0).
	if (PeriodIndex == NumberOfPeriods) {
		return 0.0;
	}

	if (Accumulate) {
		this->ResetResults();
        DiscountedProcessingCost = 0;
	}

	// El valor del período es el PROMEDIO de los óptimos de cada escenario
	// (riesgo neutro, misma convención que el Dantzig-Wolfe anterior).
	double ExpectedValue = 0.0;
	for (int Scenario = 0; Scenario < NumberOfScenarios; ++Scenario) {
		ExpectedValue += this->ComputeScenarioValue(PeriodIndex, Scenario, Base, Exclude, Include, Accumulate);
	}

	if (Accumulate) {
		DiscountedProcessingCost /= NumberOfScenarios;
		// Los acumuladores físicos también se expresan como valores esperados.
		for (int m = 0; m < NumberOfOperationalModes; ++m) {
			ProcessingUnderMode[m] /= NumberOfScenarios;
			for (int r = 0; r < NumberOfRockTypes; ++r) {
				ProcessingPerRockAndMode[r][m] /= NumberOfScenarios;
			}
		}
		for (int j = 0; j < NumberOfMetals; ++j) {
			AmountOfMetal[j] /= NumberOfScenarios;
		}
	}

	return ExpectedValue / NumberOfScenarios;
}

// =====================================================================================
//  Actualización del estado global de tipos de roca (sólo Execute / Initialize)
// =====================================================================================

void ProcessingOptimizer::UpdateGlobalRockTypeState(int PeriodIndex) {
	if (PeriodIndex == NumberOfPeriods) return;

	const MineBlockSet& Blocks = (*Data->GetBlocksInPeriod())[PeriodIndex];

	for (int Scenario = 0; Scenario < NumberOfScenarios; ++Scenario) {
		std::map <std::string, int>& Counts = (*Data->GetGlobalNumberOfBlocksPerRockType())[PeriodIndex][Scenario];
		this->CountRockTypes(Blocks, nullptr, nullptr, Scenario, Counts);

		// La bandera global indica si al menos un modo de operación es ejecutable.
		(*Data->GetGlobalEnoughTypeRocksForMixing())[PeriodIndex][Scenario] = this->ComputeRunnableModes(Counts);
	}
}

// =====================================================================================
//  Interfaz con el VND: Test / Execute
// =====================================================================================

// El TransferBlocksFrom contiene todos los bloques que se tienen que insertar en el CurrentPeriod.
// El TransferBlocksTo contiene todos los bloques que se tienen que insertar en el TargetPeriod.
//
// Test() es HIPOTÉTICO: no modifica BlocksInPeriod ni el estado global. Retorna el
// valor total de la solución resultante del movimiento:
//
//     -(ExpectedMiningCost + ΔCostoMinero) + (GlobalOptimizedNPV
//        - GlobalValueEachPeriod[Current] - GlobalValueEachPeriod[Target]
//        + NuevoValor(Current) + NuevoValor(Target))
//
// NOTA (corrección respecto del código DW anterior): en la rama Exchange, la versión
// anterior SUMABA el ΔCostoMinero en lugar de restarlo, lo que era inconsistente con
// la rama Shift y con la actualización de ExpectedMiningCost que realiza
// Optimization::ExecuteExchange. Aquí ambas ramas usan la fórmula -(EMC + Δ).
double ProcessingOptimizer::Test(int CurrentPeriod, MineBlockSet& TransferBlocksTo, int TargetPeriod, MineBlockSet& TransferBlocksFrom) {

	double NewCurrentValue = 0.0;
	double NewTargetValue = 0.0;

	// Conjunto hipotético del CurrentPeriod: pierde los TransferBlocksTo y recibe los
	// TransferBlocksFrom (este último sólo no-vacío en Exchange).
	if (CurrentPeriod != NumberOfPeriods) {
		NewCurrentValue = this->ComputePeriodValue(CurrentPeriod,
							   (*Data->GetBlocksInPeriod())[CurrentPeriod],
							   &TransferBlocksTo,
							   TransferBlocksFrom.empty() ? nullptr : &TransferBlocksFrom,
							   false);
	}

	// Conjunto hipotético del TargetPeriod: recibe los TransferBlocksTo y pierde los
	// TransferBlocksFrom.
	if (TargetPeriod != NumberOfPeriods) {
		NewTargetValue = this->ComputePeriodValue(TargetPeriod,
							  (*Data->GetBlocksInPeriod())[TargetPeriod],
							  TransferBlocksFrom.empty() ? nullptr : &TransferBlocksFrom,
							  &TransferBlocksTo,
							  false);
	}

	// Variación del costo minero esperado si el movimiento se ejecutara
	// (misma actualización que aplican ExecuteShiftBefore/After y ExecuteExchange).
	double ChangeInExpectedMiningCost = 0.0;
	for (MineBlockSet::iterator BlockIterator = TransferBlocksTo.begin(); BlockIterator != TransferBlocksTo.end(); ++BlockIterator) {
		ChangeInExpectedMiningCost += (*BlockIterator)->ExpectedCostOfMiningBlockInPeriod[TargetPeriod]
					    - (*BlockIterator)->ExpectedCostOfMiningBlockInPeriod[CurrentPeriod];
	}
	for (MineBlockSet::iterator BlockIterator = TransferBlocksFrom.begin(); BlockIterator != TransferBlocksFrom.end(); ++BlockIterator) {
		ChangeInExpectedMiningCost += (*BlockIterator)->ExpectedCostOfMiningBlockInPeriod[CurrentPeriod]
					    - (*BlockIterator)->ExpectedCostOfMiningBlockInPeriod[TargetPeriod];
	}

	return -Data->DiscountedInvestment() - ((*Data->GetExpectedMiningCost()) + ChangeInExpectedMiningCost)
	       + (*Data->GetGlobalOptimizedNPV()
		  - (*Data->GetGlobalValueEachPeriod())[CurrentPeriod]
		  - (*Data->GetGlobalValueEachPeriod())[TargetPeriod]
		  + NewCurrentValue + NewTargetValue);
}

// Execute() se invoca DESPUÉS de que Optimization actualizó BlocksInPeriod y
// ExtractionPeriodOfBlock, por lo que el estado vigente de los conjuntos de bloques
// ya refleja el movimiento. Los parámetros TransferBlocksTo/From se conservan por
// compatibilidad de interfaz, pero la recomputación se hace directamente desde los
// conjuntos globales.
void ProcessingOptimizer::Execute(int CurrentPeriod, MineBlockSet& TransferBlocksTo, int TargetPeriod, MineBlockSet& TransferBlocksFrom) {
	(void)TransferBlocksTo;
	(void)TransferBlocksFrom;

	if (CurrentPeriod == TargetPeriod) return;	// guardia defensiva (no ocurre en el VND)

	// Recuento de tipos de roca y modos ejecutables de los períodos afectados.
	this->UpdateGlobalRockTypeState(CurrentPeriod);
	this->UpdateGlobalRockTypeState(TargetPeriod);

	// El siguiente código actualiza el valor de la variable GlobalOptimizedNPV.
	// Primero se eliminan los valores que tenían antes los ValueEachPeriod en cada uno de los períodos.
	*Data->GetGlobalOptimizedNPV() = *Data->GetGlobalOptimizedNPV()
				       - ((*Data->GetGlobalValueEachPeriod())[CurrentPeriod]
					+ (*Data->GetGlobalValueEachPeriod())[TargetPeriod]);

	// Se recalculan los valores con la matheurística sobre los conjuntos vigentes.
	(*Data->GetGlobalValueEachPeriod())[CurrentPeriod] =
		this->ComputePeriodValue(CurrentPeriod, (*Data->GetBlocksInPeriod())[CurrentPeriod], nullptr, nullptr, true);
	(*Data->GetGlobalValueEachPeriod())[TargetPeriod] =
		this->ComputePeriodValue(TargetPeriod, (*Data->GetBlocksInPeriod())[TargetPeriod], nullptr, nullptr, true);

	// Luego se actualiza el valor optimizado global.
	*Data->GetGlobalOptimizedNPV() = *Data->GetGlobalOptimizedNPV()
				       + ((*Data->GetGlobalValueEachPeriod())[CurrentPeriod]
					+ (*Data->GetGlobalValueEachPeriod())[TargetPeriod]);
}

// =====================================================================================
//  Inicialización del estado global a partir de la solución inicial
// =====================================================================================

void ProcessingOptimizer::InitializeGlobalProcessingState() {
	std::cout << "------------------------------------------------------------" << std::endl;
	std::cout << "->Initializing processing optimization state (matheuristic)....." << std::endl;

	double OptimizedNPV = 0.0;
	for (int Period = 0; Period <= NumberOfPeriods; ++Period) {
		this->UpdateGlobalRockTypeState(Period);

		double PeriodValue = 0.0;
		if (Period != NumberOfPeriods) {
			PeriodValue = this->ComputePeriodValue(Period, (*Data->GetBlocksInPeriod())[Period], nullptr, nullptr, true);
			std::cout << "  Period: " << Period
				  << " -> Blocks: " << (*Data->GetBlocksInPeriod())[Period].size()
				  << " - Discounted processing value (without mining costs): "
				  << std::to_string(PeriodValue) << std::endl;
		}
		(*Data->GetGlobalValueEachPeriod())[Period] = PeriodValue;
		OptimizedNPV += PeriodValue;
	}
	*Data->GetGlobalOptimizedNPV() = OptimizedNPV;

	// NPV total de la solución inicial: valor de procesamiento menos costos de minado.
	// Con la solución vacía esto es 0; con WarmStart deja el NPV consistente con la
	// solución cargada (la versión DW anterior no lograba esto).
	*Data->GetNPV() = -Data->DiscountedInvestment() - (*Data->GetExpectedMiningCost()) + OptimizedNPV;

	std::cout << "  Initial solution -> Expected mining cost: " << std::to_string(*Data->GetExpectedMiningCost())
		  << " - Processing value: " << std::to_string(OptimizedNPV)
		  << " - NPV: " << std::to_string(*Data->GetNPV()) << std::endl;
	std::cout << "  OK" << std::endl << std::endl;
}

void ProcessingOptimizer::SavePeriodReport(const std::string& path) {
    std::ofstream report(path);
    if (!report) throw std::runtime_error("Cannot write period report: " + path);
    report << std::setprecision(17);
    report << "Period,Investment,DiscountedInvestment,MiningCapacity,RockExtracted,MiningCost,DiscountedMiningCost,ProcessingCapacity,ProcessingHours,TotalOreProcessed,DiscountedProcessingCost,DiscountedValueNoMiningCost,PeriodTotal,CumulativeNPV";
    // Use ordinal columns so user-provided mode names cannot break CSV headers.
    for (int m = 0; m < NumberOfOperationalModes; ++m)
        report << ",ProcessingRate_Mode" << m + 1 << ",ProcessingCost_Mode" << m + 1 << ",Ore_Mode" << m + 1;
    report << '\n';
    double cumulative = 0;
    for (int t = 0; t < NumberOfPeriods; ++t) {
        double processing = ComputePeriodValue(t, (*Data->GetBlocksInPeriod())[t], nullptr, nullptr, true);
        double discount = std::pow(1 + AnnualDiscountRate, t);
        double tonnes = (*Data->GetMiningTonnageOfPeriod())[t];
        double mining = tonnes * Data->MiningCosts[t] / discount;
        double investment = Data->Investments[t] / discount;
        double hours = 0, ore = 0;
        for (int m = 0; m < NumberOfOperationalModes; ++m) {
            ore += ProcessingUnderMode[m];
            hours += ProcessingUnderMode[m] / PeriodRates[m][t];
        }
        double total = processing - mining - investment;
        cumulative += total;
        report << t << ',' << Data->Investments[t] << ',' << investment << ',' << Data->MiningCapacities[t]
               << ',' << tonnes << ',' << Data->MiningCosts[t] << ',' << mining << ',' << Data->ProcessingCapacities[t]
               << ',' << hours << ',' << ore << ',' << DiscountedProcessingCost << ',' << processing
               << ',' << total << ',' << cumulative;
        for (int m = 0; m < NumberOfOperationalModes; ++m)
            report << ',' << PeriodRates[m][t] << ',' << PeriodCosts[m][t] << ',' << ProcessingUnderMode[m];
        report << '\n';
    }
    if (!report) throw std::runtime_error("Failed writing period report: " + path);
}
