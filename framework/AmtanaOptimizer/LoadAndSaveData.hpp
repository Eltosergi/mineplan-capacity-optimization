#ifndef LoadAndSaveData_hpp
#define LoadAndSaveData_hpp

#pragma once
#include <string>
#include <map>
#include <cmath>
#include <vector>
#include "DataStructures.hpp"

class LoadAndSaveData{
private:
	DataStructures* Data;

public:
	// Constructor
	LoadAndSaveData(DataStructures*);

	void LoadConfiguration(std::string);

	void LoadOreBody();

	// Variante de LoadOreBody usada exclusivamente por el validador externo (ver
	// validate.cpp). Diferencias respecto a LoadOreBody:
	//   - Carga un único archivo de escenario, correspondiente al índice indicado,
	//     con la misma convención de nombre que LoadOreBody (FileNameBase + "0i" +
	//     FileNameExtension si i < 10; sin padding si i >= 10).
	//   - Exige NumberOfScenarios == 1 en la configuración.
	//   - NO invoca MineFlow ni escribe archivos en dataForUPL/. Inserta todas las
	//     ConsecutivePosition asignadas en ConsecutivePositions. La validación
	//     geométrica de extractabilidad se considera delegada al optimizador que
	//     generó el result.txt cargado posteriormente por Validation().
	void LoadOreBodyForValidation(int);

	std::vector<std::string> Split(std::string, char);

	void Validation();

	void SaveMinePlan();
};

#endif /* LoadAndSaveData_hpp */
