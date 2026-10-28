#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <algorithm>
#include <cctype>
#include "LoadAndSaveData.hpp"

LoadAndSaveData::LoadAndSaveData(DataStructures* Data){
	// It is received a pointer to the Data
	this->Data = Data;
}

void LoadAndSaveData::LoadConfiguration(std::string FilePath){
	std::cout << "------------------------------------------------------------" << std::endl;
	std::cout << "->Loading configuration file....." << std::endl;
	std::ifstream ConfigurationFile;
	std::string Line;
	ConfigurationFile.open(FilePath);
	// In case the file does not exist of the path is wrong, the execution of the program is interrupted
	if (!ConfigurationFile) {
		std::cout << "Unable to open the configuration file." << std::endl;
		exit(1);
	}

	// Parser genérico (formato generalizado): cada línea es "Clave=Valor" y se divide
	// en el PRIMER '='. El orden de las líneas NO importa (a diferencia del antiguo
	// parser posicional), con la única excepción de las líneas "OperationalMode=...",
	// que se numeran OperationalMode1, OperationalMode2, ... según su orden de
	// aparición en el archivo.
	// - Líneas vacías o que comienzan con '#' se ignoran (comentarios).
	int operationalModeCount = 0;
	while (std::getline(ConfigurationFile, Line)){
		// Eliminar retorno de carro (archivos creados en Windows) y espacios al final
		while (!Line.empty() && (Line.back() == '\r' || Line.back() == ' ' || Line.back() == '\t')) Line.pop_back();
		if (Line.empty() || Line[0] == '#') continue;

		std::size_t pos = Line.find('=');
		if (pos == std::string::npos){
			std::cout << "WARNING: Ignoring malformed configuration line: " << Line << std::endl;
			continue;
		}
		std::string key = Line.substr(0, pos);
		std::string value = Line.substr(pos + 1);

		if (key == "OperationalMode"){
			operationalModeCount += 1;
			(*(*this->Data).GetConfiguration())["OperationalMode" + std::to_string(operationalModeCount)] = value;
		} else {
			(*(*this->Data).GetConfiguration())[key] = value;
		}
	}
	ConfigurationFile.close();

	auto config = (*this->Data).GetConfiguration();

	// Validación de presencia de las claves requeridas por el framework.
	for (const std::string required : {"NumberOfThreads", "ControlUPL", "WarmStart",
					   "FileNameBase", "FileNameExtension",
					   "MinX", "MaxX", "MinY", "MaxY", "MinZ", "MaxZ",
					   "DefaultRockTonnage", "DefaultMetalTonnage", "DefaultRockType",
					   "NumberOfScenarios", "NumberOfPeriods", "AnnualDiscountRate",
					   "MiningCapacity", "MiningCost",
					   "NumberOfMetals", "EffectiveMetalPrices", "ProcessingCapacity",
					   "NumberOfRockTypes", "RockTypes", "NumberOfOperationalModes"}){
		if (config->count(required) == 0){
			std::cout << "ERROR: Missing required configuration key: " << required << std::endl;
			exit(1);
		}
	}
	if (operationalModeCount != stoi((*config)["NumberOfOperationalModes"])){
		std::cout << "ERROR: NumberOfOperationalModes=" << (*config)["NumberOfOperationalModes"]
			  << " but " << operationalModeCount << " 'OperationalMode=' lines were found." << std::endl;
		exit(1);
	}

    Data->LoadPeriodParameters();
    for (int m = 1; m <= operationalModeCount; ++m) {
        std::map<std::string, std::string> fields;
        for (const auto& token : Split(config->at("OperationalMode" + std::to_string(m)), ';')) {
            auto pos = token.find('=');
            if (pos != std::string::npos) fields[token.substr(0, pos)] = token.substr(pos + 1);
        }
        for (const auto& key : {"ProcessingRate", "ProcessingCost"}) {
            if (!fields.count(key)) throw std::invalid_argument("Missing " + std::string(key) + " in mode " + std::to_string(m));
            auto values = ParsePeriodValues(fields.at(key), stoi(config->at("NumberOfPeriods")), std::string(key) + " mode " + std::to_string(m), std::string(key) == "ProcessingRate");
            Data->VariablePeriodParameters = Data->VariablePeriodParameters ||
                !std::all_of(values.begin(), values.end(), [&](double v) { return v == values.front(); });
        }
    }
	// Validaciones de consistencia de los parámetros generalizados.
	{
		std::vector<std::string> RockNames = this->Split((*config)["RockTypes"], ',');
		if (static_cast<int>(RockNames.size()) != stoi((*config)["NumberOfRockTypes"])){
			std::cout << "ERROR: RockTypes must contain " << (*config)["NumberOfRockTypes"]
				  << " names (NumberOfRockTypes)." << std::endl;
			exit(1);
		}
		bool DefaultRockTypeIsValid = false;
		for (const std::string& RockName : RockNames)
			if (RockName == (*config)["DefaultRockType"]) DefaultRockTypeIsValid = true;
		if (!DefaultRockTypeIsValid){
			std::cout << "ERROR: DefaultRockType=" << (*config)["DefaultRockType"]
				  << " is not declared in RockTypes." << std::endl;
			exit(1);
		}
		std::vector<std::string> Prices = this->Split((*config)["EffectiveMetalPrices"], ',');
		if (static_cast<int>(Prices.size()) != stoi((*config)["NumberOfMetals"])){
			std::cout << "ERROR: EffectiveMetalPrices must contain " << (*config)["NumberOfMetals"]
				  << " values (NumberOfMetals)." << std::endl;
			exit(1);
		}
	}

	// NumberOfThreads: si el usuario indicó "all" (case-insensitive), reemplazar por el
	// número máximo de hilos hardware disponibles. Si hardware_concurrency() retorna 0
	// (no detectable), se usa 1 como fallback seguro.
	{
		std::string ThreadValue = (*config)["NumberOfThreads"];
		std::string ThreadValueLower = ThreadValue;
		std::transform(ThreadValueLower.begin(), ThreadValueLower.end(), ThreadValueLower.begin(),
		               [](unsigned char c){ return std::tolower(c); });
		if (ThreadValueLower == "all"){
			unsigned int MaxThreads = std::thread::hardware_concurrency();
			if (MaxThreads == 0) MaxThreads = 1;
			(*config)["NumberOfThreads"] = std::to_string(MaxThreads);
			std::cout << "  NumberOfThreads=all detected. Using " << MaxThreads
			          << " hardware threads." << std::endl;
		}
	}
	// ControlUPL: factor de penalización (>0) aplicado al NoProcessing de los bloques
	// que no pueden ser extraídos por restricción de precedencia geométrica de cono
	// invertido a 45°. Amplifica la negatividad del valor para que MineFlow no
	// extienda implícitamente el dominio más allá de [MinX,MaxX] × [MinY,MaxY] al
	// calcular el UPL. Se lee como float; valores ≤ 0 invertirían la intención.
	if (stof((*config)["ControlUPL"]) <= 0.0f){
		std::cout << "ControlUPL must be strictly positive (got "
		          << (*config)["ControlUPL"] << ")." << std::endl;
		exit(1);
	}

	std::cout << "  OK" << std::endl << std::endl;
}

void LoadAndSaveData::LoadOreBody(){
	//TripleIndexToBlockMap DataLoaded;
	
	std::cout << "------------------------------------------------------------" << std::endl;
	std::cout << "->Creating the structure of the orebody and establishing connections of precedence between mineblocks....." << std::endl;
	// A Matrix is created, and it is fill with default mineblocks.
	// Convention: Z=1 is the bottom of the deposit and Z=MaxZ is the surface.
	// The ZIndex is iterated from MaxZ (surface) down to MinZ (bottom) so that
	// the predecessors of each block (the blocks above, at ZIndex + 1) already
	// exist in memory when the precedence connections are established.
	//std::cout << (*(*this->Data).GetConfiguration())["MinZ"] << std::endl;
	for (int ZIndex = stoi((*(*this->Data).GetConfiguration())["MaxZ"]); ZIndex >= stoi((*(*this->Data).GetConfiguration())["MinZ"]); ZIndex--){
		for (int XIndex = stoi((*(*this->Data).GetConfiguration())["MinX"]); XIndex <= stoi((*(*this->Data).GetConfiguration())["MaxX"]); XIndex++){
			for (int YIndex = stoi((*(*this->Data).GetConfiguration())["MinY"]); YIndex <= stoi((*(*this->Data).GetConfiguration())["MaxY"]); YIndex++){
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex] = MineBlock();
				//MineBlock NewMineBlock;
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].XIndex = XIndex;
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].YIndex = YIndex;
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ZIndex = ZIndex;

				// Setting the default amount of RockTonnage, MetalTonnage and RockType
				for (int Scenarios = 0; Scenarios < stoi((*(*this->Data).GetConfiguration())["NumberOfScenarios"]); Scenarios++){
					// Generalizado a K metales: vector de tamaño NumberOfMetals con DefaultMetalTonnage.
					std::vector <double> DefaultMetals(stoi((*(*this->Data).GetConfiguration())["NumberOfMetals"]), stof((*(*this->Data).GetConfiguration())["DefaultMetalTonnage"]));
					(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ScenarioVector.push_back (ScenarioBlock(stof((*(*this->Data).GetConfiguration())["DefaultRockTonnage"]), DefaultMetals, (*(*this->Data).GetConfiguration())["DefaultRockType"]));
				}

				// This is for ensuring that the upper layer (ZIndex + 1) already exists.
				// Blocks at the surface (ZIndex == MaxZ) have no predecessors above
				// and therefore skip this block.
				if (ZIndex < stoi((*(*this->Data).GetConfiguration())["MaxZ"])){
					// Above
					(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
					(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex + 1]);
					//(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&NewMineBlock);
					// Up, Left, Above
					// This if is for ensuring that we are inside the limits of the orebody
					if ((XIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinX"])) and (YIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinY"]))){
						(*(*this->Data).GetOreBody())[XIndex - 1][YIndex - 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex - 1][YIndex - 1][ZIndex + 1]);
					}
					// Up, Above
					if (XIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinX"])){
						(*(*this->Data).GetOreBody())[XIndex - 1][YIndex][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex - 1][YIndex][ZIndex + 1]);
					}
					// Up, Right, Above
					if ((XIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinX"])) and (YIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxY"]))){
						(*(*this->Data).GetOreBody())[XIndex - 1][YIndex + 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex - 1][YIndex + 1][ZIndex + 1]);
					}
					// Left, Above
					if (YIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinY"])){
						(*(*this->Data).GetOreBody())[XIndex][YIndex - 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex - 1][ZIndex + 1]);
					}
					// Right, Above
					if (YIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxY"])){
						(*(*this->Data).GetOreBody())[XIndex][YIndex + 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex + 1][ZIndex + 1]);
					}
					// Down, Left, Above
					if ((XIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxX"])) and (YIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinY"]))){
						(*(*this->Data).GetOreBody())[XIndex + 1][YIndex - 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex + 1][YIndex - 1][ZIndex + 1]);
					}
					// Down, Above
					if (XIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxX"])){
						(*(*this->Data).GetOreBody())[XIndex + 1][YIndex][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex + 1][YIndex][ZIndex + 1]);
					}
					// Down, Right, Above
					if ((XIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxX"])) and (YIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxY"]))){
						(*(*this->Data).GetOreBody())[XIndex + 1][YIndex + 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex + 1][YIndex + 1][ZIndex + 1]);
					}

				}
			}
		}
	}

	std::cout << "  OK" << std::endl;
	std::cout << "->Loading data from files....." << std::endl;
	
	// It is assumed that the files goes from 00 to NumberOfScenarios - 1
	// Besides, it is assumed that the information is found in each other line of the file
	// The scenario files use the convention Z=1 at the bottom and Z=MaxZ at the surface,
	// matching the internal convention. Therefore the ZIndex column is read directly
	// without any transformation.
	for (int Scenario = 0; Scenario < stoi((*(*this->Data).GetConfiguration())["NumberOfScenarios"]) ; Scenario++){
		std::ifstream TheFile;
		std::string Line;
		if (Scenario < 10){
			std::cout << "  " + (*(*this->Data).GetConfiguration())["FileNameBase"] + "0" + std::to_string(Scenario) + (*(*this->Data).GetConfiguration())["FileNameExtension"] << std::endl;
			TheFile.open((*(*this->Data).GetConfiguration())["FileNameBase"] + "0" + std::to_string(Scenario) + (*(*this->Data).GetConfiguration())["FileNameExtension"]);
		} else {
			std::cout << "  " + (*(*this->Data).GetConfiguration())["FileNameBase"] + std::to_string(Scenario) + (*(*this->Data).GetConfiguration())["FileNameExtension"]<< std::endl;
			TheFile.open((*(*this->Data).GetConfiguration())["FileNameBase"] + std::to_string(Scenario) + (*(*this->Data).GetConfiguration())["FileNameExtension"]);
		}
		//In case that the file does not exist or the name is wrong, the execution of the
		//program is interrupted
		if (!TheFile) {
			std::cout << "Unable to open file of the data." << std::endl;
			exit(1);
		}
		int count = 0;
		// Start reading the lines of the file
		while (TheFile >> Line){
			std::vector<std::string> SplittedString = this->Split(Line, ',');
			
			// The first time a mine block is detected, is created.
			// After the first time, the mine block only adds values in its ScenarioParcelData (ParcelData list).
			//std::cout << SplittedString[0] + " - " + SplittedString[1] + " - " + SplittedString[2] + " / " + SplittedString[3] + " - " + SplittedString[4] << std::endl;
			// Due to the mineblock in the position X, Y, Z already has default values, the agent needs to remove it. For that, remove the first element of the vector and insert a new element at the endo of the list. After NumberOfScenarios iterations, the complete list is replace with the new values.
			(*(*this->Data).GetOreBody())[stoi(SplittedString[0])][stoi(SplittedString[1])][stoi(SplittedString[2])].ScenarioVector.erase ((*(*this->Data).GetOreBody())[stoi(SplittedString[0])][stoi(SplittedString[1])][stoi(SplittedString[2])].ScenarioVector.begin());
			// Formato de línea generalizado: X,Y,Z,RockTonnage,Metal_1,...,Metal_K[,RockTypeName]
			// La columna final con el nombre del tipo de roca es OPCIONAL: si está ausente, el
			// bloque recibe el DefaultRockType. Recordar que el tipo de roca es POR ESCENARIO
			// (incertidumbre en los dominios geológicos).
			{
				int NumberOfMetals = stoi((*(*this->Data).GetConfiguration())["NumberOfMetals"]);
				if (static_cast<int>(SplittedString.size()) < 4 + NumberOfMetals){
					std::cout << "ERROR: Block data line has fewer columns than expected (X,Y,Z,RockTonnage and " << NumberOfMetals << " metals): " << Line << std::endl;
					exit(1);
				}
				std::vector <double> MetalTonnages;
				for (int Metal = 0; Metal < NumberOfMetals; Metal++){
					MetalTonnages.push_back (stof(SplittedString[4 + Metal]));
				}
				std::string RockType = (static_cast<int>(SplittedString.size()) > 4 + NumberOfMetals)
						     ? SplittedString[4 + NumberOfMetals]
						     : (*(*this->Data).GetConfiguration())["DefaultRockType"];
				(*(*this->Data).GetOreBody())[stoi(SplittedString[0])][stoi(SplittedString[1])][stoi(SplittedString[2])].ScenarioVector.push_back (ScenarioBlock(stof(SplittedString[3]), MetalTonnages, RockType));
			}
			count++;
		}
		TheFile.close();	// The file is closed
	}

	std::cout << "  OK" << "\n";
	std::cout << "->Calculating the UPL....." << "\n";

	// Se obtiene el menor costo de procesamiento entre todos los modos de operación.
	// Este valor se utiliza únicamente para el cálculo del valor del bloque que
	// alimenta a MineFlow (UPL), donde se busca el escenario más optimista para
	// no descartar bloques que pudieran ser rentables en algún modo.
	int NumberOfOperationalModes = stoi((*(*this->Data).GetConfiguration())["NumberOfOperationalModes"]);
	double MinProcessingCost = 0.0;
    const double MinMiningCost = *std::min_element(Data->MiningCosts.begin(), Data->MiningCosts.end());
	for (int NumberType = 0; NumberType < NumberOfOperationalModes; NumberType++){
		// Formato generalizado del modo: "Name=1;ProcessingRate=425;ProcessingCost=13;Recoveries=0.89,0.89;w=0.70,0.30"
		// Se buscan los campos por NOMBRE (misma convención que en el constructor de ProcessingOptimizer).
		std::map <std::string, std::string> Fields;
		for (const std::string& Token : this->Split((*(*this->Data).GetConfiguration())["OperationalMode" + std::to_string(NumberType + 1)], ';')){
			std::size_t pos = Token.find('=');
			if (pos != std::string::npos) Fields[Token.substr(0, pos)] = Token.substr(pos + 1);
		}
		if (Fields.count("ProcessingCost") == 0){
			std::cout << "ERROR: Field 'ProcessingCost' missing in OperationalMode" << (NumberType + 1) << "." << std::endl;
			exit(1);
		}
		auto costs = ParsePeriodValues(Fields["ProcessingCost"], stoi(Data->GetConfiguration()->at("NumberOfPeriods")), "ProcessingCost");
        double CurrentProcessingCost = *std::min_element(costs.begin(), costs.end());
		if (NumberType == 0 or CurrentProcessingCost < MinProcessingCost){
			MinProcessingCost = CurrentProcessingCost;
		}
	}

	// Cacheo de constantes geométricas y del parámetro ControlUPL, usadas en el
	// triple loop para detectar bloques no-extraíbles por precedencia.
	int MinX = stoi((*(*this->Data).GetConfiguration())["MinX"]);
	int MaxX = stoi((*(*this->Data).GetConfiguration())["MaxX"]);
	int MinY = stoi((*(*this->Data).GetConfiguration())["MinY"]);
	int MaxY = stoi((*(*this->Data).GetConfiguration())["MaxY"]);
	int MaxZ = stoi((*(*this->Data).GetConfiguration())["MaxZ"]);
	float ControlUPL = stof((*(*this->Data).GetConfiguration())["ControlUPL"]);
	
	int Position = 0;
	for (int Scenario = 0; Scenario < stoi((*(*this->Data).GetConfiguration())["NumberOfScenarios"]) ; Scenario++){
		std::ofstream BlockValuesPerScenarioFile;
		BlockValuesPerScenarioFile.open("dataForUPL/scenario" + std::to_string(Scenario) + ".dat");
		// La razón por la que los FOR están Z, Y, X, es debido a que así lo requiere MineFlow.
		// El For ZIndex va de MinZ a MaxZ (de fondo a superficie), coincidiendo con la
		// convención de MineFlow que considera el nivel 1 como el fondo.
		for (int ZIndex = stoi((*(*this->Data).GetConfiguration())["MinZ"]); ZIndex <= stoi((*(*this->Data).GetConfiguration())["MaxZ"]); ZIndex++){
			for (int YIndex = stoi((*(*this->Data).GetConfiguration())["MinY"]); YIndex <= stoi((*(*this->Data).GetConfiguration())["MaxY"]); YIndex++){
				for (int XIndex = stoi((*(*this->Data).GetConfiguration())["MinX"]); XIndex <= stoi((*(*this->Data).GetConfiguration())["MaxX"]); XIndex++){
					// El siguiente código se debe asignar sólo una vez (No depende de los escenarios).
					if (Scenario == 0) {
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ConsecutivePosition = Position;
						Position++;

						// Se calculan los valores y costos descontados
						(*this->Data).CalculateExpectedCostOfMiningBlockInPeriod(&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						//(*this->Data).CalculateExpectedPriceOfMiningBlockInPeriod(&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
					}
					// El no procesar sólo considera el costo de haber extraído el bloque.
					int NoProcessing = -(int)((*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ScenarioVector[Scenario].RockTonnage * MinMiningCost);

					// Recordar que aquí estamos obteniendo el UPL para reducir el espacio de búsqueda de la solución.
					// Es por esta razón que se asume recuperación perfecta (igual a 1). 
					// Además, por la misma razón, se asume el costo más bajo de procesamiento de los modos de operación a fin de incluir más bloques.
					// Generalizado a K metales: suma de Metal_k × Precio_k (recuperación perfecta = 1).
					double ProcessingRevenue = 0.0;
					{
						std::vector<std::string> PriceTokens = this->Split((*(*this->Data).GetConfiguration())["EffectiveMetalPrices"], ',');
						for (int Metal = 0; Metal < stoi((*(*this->Data).GetConfiguration())["NumberOfMetals"]); Metal++){
							ProcessingRevenue += (*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ScenarioVector[Scenario].MetalTonnages[Metal] * 1.0 * stof(PriceTokens[Metal]);
						}
					}
					int Processing = (int)(ProcessingRevenue - (*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ScenarioVector[Scenario].RockTonnage * MinProcessingCost + NoProcessing);

					// Extracción geométrica: bajo precedencia de cono invertido a 45°
					// (9 bloques superiores requeridos), el bloque (XIndex, YIndex, ZIndex)
					// es extraíble sólo si su distancia mínima a los bordes laterales del
					// dominio es al menos igual a su profundidad bajo la superficie
					// (MaxZ - ZIndex). En caso contrario, MineFlow tendría que extender
					// implícitamente el dominio fuera de [MinX,MaxX] × [MinY,MaxY] para
					// satisfacer las precedencias, lo cual no debe ocurrir.
					int DistanceToLateralBorder = std::min({XIndex - MinX, MaxX - XIndex, YIndex - MinY, MaxY - YIndex});
					int DepthBelowSurface = MaxZ - ZIndex;
					bool IsExtractable = (DistanceToLateralBorder >= DepthBelowSurface);

					if (!IsExtractable){
						// Penalización ControlUPL × NoProcessing (Opción A: el bloque
						// no-extraíble se escribe siempre con este valor, sin comparar
						// con Processing). NoProcessing < 0, ControlUPL > 0 ⇒ valor muy negativo.
						BlockValuesPerScenarioFile << std::to_string((int)(ControlUPL * NoProcessing)) + "\n";
					} else if (Processing > NoProcessing){
						BlockValuesPerScenarioFile << std::to_string(Processing) + "\n";
					} else {
						BlockValuesPerScenarioFile << std::to_string(NoProcessing) + "\n";
					}
				}
			}
		}
		BlockValuesPerScenarioFile.close();
		// Se llama MineFlow. Considerar que siempre se busca un ángulo de 45 grados. Esto puede cambiar en el futuro si se agrega el ángulo al archivo
		// de configuraciones.
		std::string command = "MineFlow/mineflow --regular " + (*(*this->Data).GetConfiguration())["MaxX"] + " " + (*(*this->Data).GetConfiguration())["MaxY"] + " " + (*(*this->Data).GetConfiguration())["MaxZ"] + " 45 " + "dataForUPL/scenario" + std::to_string(Scenario) + ".dat dataForUPL/UPLscenario" + std::to_string(Scenario) + ".txt";
		const char * commandConstChar = command.c_str();
		std::cout << command << std::endl;
		int result = std::system(commandConstChar);

		// Ahora se obtienen los bloques que MineFlow determinó que eran parte del UPL y se agregan a la estructura de datos ConsecutivePositions.
		std::ifstream UPLScenarioFile;
		std::string Line;
		UPLScenarioFile.open("dataForUPL/UPLscenario" + std::to_string(Scenario) + ".txt");
		if (!UPLScenarioFile) {
			std::cout << "Unable to open dataForUPL/UPLscenario" + std::to_string(Scenario) + ".txt" << "\n";
			exit(1);
		}
		while (UPLScenarioFile >> Line){
			// ConsecutivePositions es un unordered set. Esta estructura de datos no acepta duplicados, así que no importa
			// si dos o más escenarios tienen los mismos bloques.
			(*(*this->Data).GetConsecutivePositions()).insert (stoi(Line));
		}

		UPLScenarioFile.close();
	}

	std::cout << "  The number of blocks considered for the optimizacion is: " << (*(*this->Data).GetConsecutivePositions()).size() << std::endl;
	
	std::cout << "  OK" << "\n" << "\n";
}

// This function splits a string based on a specific char.
std::vector<std::string> LoadAndSaveData::Split(std::string strToSplit, char delimiter){
	std::stringstream ss(strToSplit);
	std::string item;
	std::vector<std::string> splittedStrings;
	while (getline(ss, item, delimiter)){
		splittedStrings.push_back(item);
	}
	return splittedStrings;
}

void LoadAndSaveData::Validation(){
	std::cout << "------------------------------------------------------------" << std::endl;

	// WarmStart controla si se carga la solución inicial desde sensor/result.txt
	// (true) o si la optimización parte desde la solución vacía generada por
	// DataStructures::Initializing(), donde todos los bloques del UPL están en
	// el período ficticio NumberOfPeriods (false). Cualquier valor distinto de
	// "true" -- incluido el caso de no estar definido -- se interpreta como false.
	bool WarmStart = ((*(*this->Data).GetConfiguration())["WarmStart"] == "true");

	if (WarmStart){
		std::cout << "->Loading warm-start initial solution from file..." << std::endl;
	} else {
		std::cout << "->WarmStart disabled. Starting from empty initial solution..." << std::endl;
	}

	// Inicialización que debe ocurrir siempre, independientemente de WarmStart.
	(*(*this->Data).GetNPV()) = 0;
	(*(*this->Data).GetExpectedMiningCost()) = 0;
	//(*(*this->Data).GetExpectedMiningPrice()) = 0;
	std::vector<double> MiningCapacity;
	for (int Period = 0; Period < stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]); Period++){
		MiningCapacity.push_back (0.0);
	}

	if (WarmStart){
		std::ifstream TheFile;
		std::string Line;
		TheFile.open("sensor/result.txt");
		//TheFile << "Period,XIndex,YIndex,ZIndex\n";

		if (!TheFile) {
			std::cout << "Unable to open file of the data." << std::endl;
			exit(1);
		}

		// The first line contains the tiles
		TheFile >> Line;
		// Start reading the data
		while (TheFile >> Line){
			std::vector<std::string> SplittedString = this->Split(Line, ',');
			if (SplittedString.size() == 4){
				//(*(*this->Data).GetNPV()) += -(*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])].ExpectedCostOfMiningBlockInPeriod[stoi(SplittedString[0])] + (*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])].ExpectedPriceOfMiningBlockInPeriod[stoi(SplittedString[0])];
				(*(*this->Data).GetNPV()) += -(*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])].ExpectedCostOfMiningBlockInPeriod[stoi(SplittedString[0])];
				MiningCapacity[stoi(SplittedString[0])] += (*this->Data).GetRockTonnageOfMineBlock(&(*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])]);
				(*(*this->Data).GetBlocksInPeriod())[stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"])].erase(&(*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])]);
				(*(*this->Data).GetBlocksInPeriod())[stoi(SplittedString[0])].insert(&(*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])]);
				(*(*this->Data).GetExtractionPeriodOfBlock())[&(*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])]] = stoi(SplittedString[0]);
				(*(*this->Data).GetMiningTonnageOfPeriod())[stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"])] -= (*this->Data).GetRockTonnageOfMineBlock(&(*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])]);
				(*(*this->Data).GetMiningTonnageOfPeriod())[stoi(SplittedString[0])] += (*this->Data).GetRockTonnageOfMineBlock(&(*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])]);
				(*(*this->Data).GetExpectedMiningCost()) += (*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])].ExpectedCostOfMiningBlockInPeriod[stoi(SplittedString[0])] - (*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])].ExpectedCostOfMiningBlockInPeriod[stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"])];
				//(*(*this->Data).GetExpectedMiningPrice()) += (*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])].ExpectedPriceOfMiningBlockInPeriod[stoi(SplittedString[0])] - (*(*this->Data).GetOreBody())[stoi(SplittedString[1])][stoi(SplittedString[2])][stoi(SplittedString[3])].ExpectedPriceOfMiningBlockInPeriod[stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"])];
			}
		}
		TheFile.close();
	}

	//Initiate LastestImmediatePredecessorPeriodOfBlock
        for (int periodIndex = 0; periodIndex <= stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]); periodIndex++){
                for (MineBlockSet::iterator mineBlockIterator1 = (*(*this->Data).GetBlocksInPeriod())[periodIndex].begin(); mineBlockIterator1 != (*(*this->Data).GetBlocksInPeriod())[periodIndex].end(); ++mineBlockIterator1){
                        (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator1] = 0;
                        for (std::forward_list<MineBlock*>::iterator mineBlockIterator2 = (*mineBlockIterator1)->ImmediatePredecessorBlocks.begin(); mineBlockIterator2 != (*mineBlockIterator1)->ImmediatePredecessorBlocks.end(); ++mineBlockIterator2){
                                if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2] > (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator1]){
                                        (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator1] = (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2];
				}
			}
                        if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator1] < (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator1]){
                                std::cout << "Initial solution violates block precedence constraint in period " << periodIndex << ": " << (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator1] << "<" << (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator1] << std::endl;

                                std::cout << "Block: " << (*mineBlockIterator1)->XIndex << " , " << (*mineBlockIterator1)->YIndex << " , " << (*mineBlockIterator1)->ZIndex << std::endl;
			}
                }
	}

	//Initiate EarliestImmediateSuccessorPeriodOfBlock
        for (int periodIndex = 0; periodIndex <= stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]); periodIndex++){
                for (MineBlockSet::iterator mineBlockIterator1 = (*(*this->Data).GetBlocksInPeriod())[periodIndex].begin(); mineBlockIterator1 != (*(*this->Data).GetBlocksInPeriod())[periodIndex].end(); ++mineBlockIterator1){
                        //(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator1] = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) - 1;
			
                        (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator1] = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]);
                        for (std::forward_list<MineBlock*>::iterator mineBlockIterator2 = (*mineBlockIterator1)->ImmediateSuccessorBlocks.begin(); mineBlockIterator2 != (*mineBlockIterator1)->ImmediateSuccessorBlocks.end(); ++mineBlockIterator2){
                                if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2] < (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator1]){
                                        (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator1] = (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2];
				}
			}
                        if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator1] > (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator1]){
                                std::cout << "Initial solution violates block succession constraint in period " << periodIndex << ": " << (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator1] << ">" << (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator1] << std::endl;

                                std::cout << "Block: " << (*mineBlockIterator1)->XIndex << " , " << (*mineBlockIterator1)->YIndex << " , " << (*mineBlockIterator1)->ZIndex << std::endl;
			}
                }
	}


	std::cout << "->Calculating MiningCapacity Constraint" << std::endl;
	for (int Period = 0; Period < stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]); Period++){
		std::cout << "  Period: " << Period << " -> Total Mining Capacity: " + std::to_string(Data->MiningCapacities.at(Period)) + " - Period Mining Capacity: " + std::to_string(MiningCapacity[Period]) << std::endl;
        if (MiningCapacity[Period] > Data->MiningCapacities.at(Period) + 1e-6 * std::max(1.0, Data->MiningCapacities.at(Period)))
            throw std::invalid_argument("Initial plan exceeds MiningCapacity in period " + std::to_string(Period));
	}

	// NOTA: La inicialización de GlobalNumberOfBlocksPerRockType y
	// GlobalEnoughTypeRocksForMixing con la solución inicial fue trasladada a
	// ProcessingOptimizer::InitializeGlobalProcessingState(), que main.cpp invoca
	// inmediatamente después de construir el optimizador. De esa forma el conteo y la
	// condición de modos ejecutables ("por modo") viven en un único lugar.

	std::cout << "  OK" << std::endl << std::endl;
}


void LoadAndSaveData::SaveMinePlan(){
	std::cout << "------------------------------------------------------------" << std::endl;
	std::cout << "->Saving results in file..." << std::endl;
	std::ofstream TheFile;
	TheFile.open("sensor/result.txt");
	TheFile << "Period,XIndex,YIndex,ZIndex\n";
	for (int Period = 0; Period < stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]); Period++){
		//std::cout << "Number Of Elements Period: " + std::to_string(Period) + " is " + std::to_string((*(*this->Data).GetBlocksInPeriod())[Period].size()) << std::endl;
		for (MineBlockSet::iterator BlockSetIterator = (*(*this->Data).GetBlocksInPeriod())[Period].begin(); BlockSetIterator != (*(*this->Data).GetBlocksInPeriod())[Period].end(); ++BlockSetIterator){
			TheFile << std::to_string(Period) + "," + std::to_string((*BlockSetIterator)->XIndex) + "," + std::to_string((*BlockSetIterator)->YIndex) + "," + std::to_string((*BlockSetIterator)->ZIndex) + "\n"; // + "," + std::to_string(acumulado) + "\n";
		}
	}

	//TheFile << "NPV: " + std::to_string((*(*this->Data).GetNPV())) + "\n";
	TheFile.close();

	std::cout << "  OK" << std::endl << std::endl;
}

// =====================================================================
// LoadOreBodyForValidation: variante de LoadOreBody usada por el validador
// externo (validate.cpp). Ver declaración en el .hpp para diferencias clave.
// =====================================================================
void LoadAndSaveData::LoadOreBodyForValidation(int ScenarioIndex){
	// Precondición: NumberOfScenarios debe haber sido sobrescrito a "1" en memoria
	// antes de llamar a este método (validate.cpp es responsable de hacerlo).
	// Validamos para detectar errores tempranos en el caller.
	if (stoi((*(*this->Data).GetConfiguration())["NumberOfScenarios"]) != 1){
		std::cout << "LoadOreBodyForValidation requires NumberOfScenarios == 1 in the in-memory configuration. Abort." << std::endl;
		exit(1);
	}
	if (ScenarioIndex < 0){
		std::cout << "LoadOreBodyForValidation: invalid scenario index " + std::to_string(ScenarioIndex) + ". Abort." << std::endl;
		exit(1);
	}

	std::cout << "------------------------------------------------------------" << std::endl;
	std::cout << "->Creating the structure of the orebody and establishing connections of precedence between mineblocks....." << std::endl;
	// El bloque siguiente es idéntico al de LoadOreBody (creación de la matriz de
	// bloques y relaciones de precedencia bajo cono invertido a 45°). Se duplica
	// intencionalmente para no introducir riesgo en el camino caliente de
	// optimización; refactorizar a un helper común queda como deuda menor.
	// Convención: Z=1 es el fondo del depósito y Z=MaxZ la superficie.
	for (int ZIndex = stoi((*(*this->Data).GetConfiguration())["MaxZ"]); ZIndex >= stoi((*(*this->Data).GetConfiguration())["MinZ"]); ZIndex--){
		for (int XIndex = stoi((*(*this->Data).GetConfiguration())["MinX"]); XIndex <= stoi((*(*this->Data).GetConfiguration())["MaxX"]); XIndex++){
			for (int YIndex = stoi((*(*this->Data).GetConfiguration())["MinY"]); YIndex <= stoi((*(*this->Data).GetConfiguration())["MaxY"]); YIndex++){
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex] = MineBlock();
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].XIndex = XIndex;
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].YIndex = YIndex;
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ZIndex = ZIndex;

				// ScenarioVector se inicializa con UN solo ScenarioBlock default
				// (NumberOfScenarios == 1, validado al inicio de la función).
				for (int Scenarios = 0; Scenarios < stoi((*(*this->Data).GetConfiguration())["NumberOfScenarios"]); Scenarios++){
					// Generalizado a K metales: vector de tamaño NumberOfMetals con DefaultMetalTonnage.
					std::vector <double> DefaultMetals(stoi((*(*this->Data).GetConfiguration())["NumberOfMetals"]), stof((*(*this->Data).GetConfiguration())["DefaultMetalTonnage"]));
					(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ScenarioVector.push_back (ScenarioBlock(stof((*(*this->Data).GetConfiguration())["DefaultRockTonnage"]), DefaultMetals, (*(*this->Data).GetConfiguration())["DefaultRockType"]));
				}

				// Precedencias bajo cono invertido a 45° (9 bloques por capa superior).
				if (ZIndex < stoi((*(*this->Data).GetConfiguration())["MaxZ"])){
					// Above
					(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
					(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex + 1]);
					// Up, Left, Above
					if ((XIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinX"])) and (YIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinY"]))){
						(*(*this->Data).GetOreBody())[XIndex - 1][YIndex - 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex - 1][YIndex - 1][ZIndex + 1]);
					}
					// Up, Above
					if (XIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinX"])){
						(*(*this->Data).GetOreBody())[XIndex - 1][YIndex][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex - 1][YIndex][ZIndex + 1]);
					}
					// Up, Right, Above
					if ((XIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinX"])) and (YIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxY"]))){
						(*(*this->Data).GetOreBody())[XIndex - 1][YIndex + 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex - 1][YIndex + 1][ZIndex + 1]);
					}
					// Left, Above
					if (YIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinY"])){
						(*(*this->Data).GetOreBody())[XIndex][YIndex - 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex - 1][ZIndex + 1]);
					}
					// Right, Above
					if (YIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxY"])){
						(*(*this->Data).GetOreBody())[XIndex][YIndex + 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex + 1][ZIndex + 1]);
					}
					// Down, Left, Above
					if ((XIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxX"])) and (YIndex - 1 >= stoi((*(*this->Data).GetConfiguration())["MinY"]))){
						(*(*this->Data).GetOreBody())[XIndex + 1][YIndex - 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex + 1][YIndex - 1][ZIndex + 1]);
					}
					// Down, Above
					if (XIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxX"])){
						(*(*this->Data).GetOreBody())[XIndex + 1][YIndex][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex + 1][YIndex][ZIndex + 1]);
					}
					// Down, Right, Above
					if ((XIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxX"])) and (YIndex + 1 <= stoi((*(*this->Data).GetConfiguration())["MaxY"]))){
						(*(*this->Data).GetOreBody())[XIndex + 1][YIndex + 1][ZIndex + 1].ImmediateSuccessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
						(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ImmediatePredecessorBlocks.push_front (&(*(*this->Data).GetOreBody())[XIndex + 1][YIndex + 1][ZIndex + 1]);
					}
				}
			}
		}
	}

	std::cout << "  OK" << std::endl;
	std::cout << "->Loading data from a single scenario file (validation mode)....." << std::endl;

	// Carga del archivo del escenario solicitado. Misma convención de nombre que
	// LoadOreBody (FileNameBase + "0i" + FileNameExtension si i < 10).
	{
		std::ifstream TheFile;
		std::string Line;
		std::string ScenarioFilePath;
		if (ScenarioIndex < 10){
			ScenarioFilePath = (*(*this->Data).GetConfiguration())["FileNameBase"] + "0" + std::to_string(ScenarioIndex) + (*(*this->Data).GetConfiguration())["FileNameExtension"];
		} else {
			ScenarioFilePath = (*(*this->Data).GetConfiguration())["FileNameBase"] + std::to_string(ScenarioIndex) + (*(*this->Data).GetConfiguration())["FileNameExtension"];
		}
		std::cout << "  " + ScenarioFilePath << std::endl;
		TheFile.open(ScenarioFilePath);
		if (!TheFile) {
			std::cout << "Unable to open scenario file " + ScenarioFilePath << std::endl;
			exit(1);
		}

		while (TheFile >> Line){
			std::vector<std::string> SplittedString = this->Split(Line, ',');
			// ScenarioVector tiene un único slot (NumberOfScenarios == 1): se borra
			// el default y se reemplaza con los valores leídos del archivo.
			(*(*this->Data).GetOreBody())[stoi(SplittedString[0])][stoi(SplittedString[1])][stoi(SplittedString[2])].ScenarioVector.erase ((*(*this->Data).GetOreBody())[stoi(SplittedString[0])][stoi(SplittedString[1])][stoi(SplittedString[2])].ScenarioVector.begin());
			// Formato de línea generalizado (igual que en LoadOreBody): X,Y,Z,Rock,M_1..M_K[,RockType].
			{
				int NumberOfMetals = stoi((*(*this->Data).GetConfiguration())["NumberOfMetals"]);
				if (static_cast<int>(SplittedString.size()) < 4 + NumberOfMetals){
					std::cout << "ERROR: Block data line has fewer columns than expected (X,Y,Z,RockTonnage and " << NumberOfMetals << " metals): " << Line << std::endl;
					exit(1);
				}
				std::vector <double> MetalTonnages;
				for (int Metal = 0; Metal < NumberOfMetals; Metal++){
					MetalTonnages.push_back (stof(SplittedString[4 + Metal]));
				}
				std::string RockType = (static_cast<int>(SplittedString.size()) > 4 + NumberOfMetals)
						     ? SplittedString[4 + NumberOfMetals]
						     : (*(*this->Data).GetConfiguration())["DefaultRockType"];
				(*(*this->Data).GetOreBody())[stoi(SplittedString[0])][stoi(SplittedString[1])][stoi(SplittedString[2])].ScenarioVector.push_back (ScenarioBlock(stof(SplittedString[3]), MetalTonnages, RockType));
			}
		}
		TheFile.close();
	}

	std::cout << "  OK" << std::endl;
	std::cout << "->Assigning ConsecutivePosition and costs (skipping MineFlow / UPL)....." << std::endl;

	// Asignación de ConsecutivePosition + cálculo de costos descontados de minado.
	// Mismo orden de iteración (Z, Y, X de fondo a superficie) y misma numeración
	// consecutiva que LoadOreBody, para que los IDs sean coherentes con cualquier
	// estructura que dependa de ellos (e.g. result.txt no usa IDs consecutivos,
	// usa coordenadas, así que el orden no afecta el warm-start; pero mantenerlo
	// idéntico evita sorpresas).
	// A diferencia de LoadOreBody, NO se invoca MineFlow ni se escriben archivos
	// en dataForUPL/. Todas las posiciones se insertan en ConsecutivePositions
	// para que Initializing() no descarte ningún bloque.
	int Position = 0;
	for (int ZIndex = stoi((*(*this->Data).GetConfiguration())["MinZ"]); ZIndex <= stoi((*(*this->Data).GetConfiguration())["MaxZ"]); ZIndex++){
		for (int YIndex = stoi((*(*this->Data).GetConfiguration())["MinY"]); YIndex <= stoi((*(*this->Data).GetConfiguration())["MaxY"]); YIndex++){
			for (int XIndex = stoi((*(*this->Data).GetConfiguration())["MinX"]); XIndex <= stoi((*(*this->Data).GetConfiguration())["MaxX"]); XIndex++){
				(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex].ConsecutivePosition = Position;
				(*this->Data).CalculateExpectedCostOfMiningBlockInPeriod(&(*(*this->Data).GetOreBody())[XIndex][YIndex][ZIndex]);
				(*(*this->Data).GetConsecutivePositions()).insert (Position);
				Position++;
			}
		}
	}

	std::cout << "  The number of blocks considered for the validation is: " << (*(*this->Data).GetConsecutivePositions()).size() << std::endl;
	std::cout << "  OK" << std::endl << std::endl;
}
