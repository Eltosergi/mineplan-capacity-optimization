#include <iostream>
#include <string>
#include <set>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include "DataStructures.hpp"
#include "LoadAndSaveData.hpp"
#include "ProcessingOptimizer.hpp"
#include "Optimization.hpp"

int main (int argc, char**argv) {
	std::cout << "#################################################" << std::endl;
	std::cout << "##                                             ##" << std::endl;
	std::cout << "##           MINE PLANNING SIMULATOR           ##" << std::endl;
	std::cout << "##           Author: Aldo Quelopana            ##" << std::endl;
	std::cout << "##         Email: aldo.quelopana@ucn.cl        ##" << std::endl;
	std::cout << "##  Matheuristic version: N rocks / M modes /  ##" << std::endl;
	std::cout << "##               K metals (2026)               ##" << std::endl;
	std::cout << "##                                             ##" << std::endl;
	std::cout << "#################################################" << std::endl;
	if (argv[1] == NULL){
		std::cout << "Abort: Configuration file missing. Try again..." << std::endl;
		std::cout << "For example ./simulator.out configuration.txt" <<std::endl;
		return (0);
	}

	time_t Beginning, EndInitialSolution, End;
	time(&Beginning);

	DataStructures Data = DataStructures();

	LoadAndSaveData Files = LoadAndSaveData(&Data);
    try {
	Files.LoadConfiguration(argv[1]);
	Files.LoadOreBody();
	Data.Initializing();

	Files.Validation();

	ProcessingOptimizer OptimizerInstance = ProcessingOptimizer(&Data);

	// Inicializa el estado global de la optimización del procesamiento a partir de la
	// solución inicial vigente (vacía o cargada con WarmStart): conteo de tipos de roca
	// por período/escenario, modos ejecutables, GlobalValueEachPeriod, GlobalOptimizedNPV
	// y el NPV total de la solución inicial.
	OptimizerInstance.InitializeGlobalProcessingState();

	Optimization OptimatedSolution = Optimization(&Data, &OptimizerInstance);
	OptimatedSolution.GetOptimizedSolution();

	Files.SaveMinePlan();
    OptimizerInstance.SavePeriodReport("sensor/period_results.csv");
	// The following code is for correcting the deviations generated during the optimization
	
	time(&End);

	// Resultado final: se envia al ejecutor (ejecutor.py) por el pipe que
	// este abrio y cuyo descriptor llega en argv[2], en vez de imprimirlo
	// aqui. Se escriben dos "double" binarios crudos (NPV y tiempo), no
	// texto: el ejecutor los recibe como numeros (struct.unpack), sin
	// parsear ninguna linea ni tocar disco.
	double ResultValues[2] = { *Data.GetNPV(), difftime (End, Beginning) };

	int ResultFd = (argc > 2) ? std::atoi(argv[2]) : -1;
	if (ResultFd >= 0) {
		if (write(ResultFd, ResultValues, sizeof(ResultValues)) != (ssize_t) sizeof(ResultValues)) {
			std::cerr << "ADVERTENCIA: no se pudo enviar el resultado por el pipe al ejecutor." << std::endl;
		}
		close(ResultFd);
	} else {
		std::cout << "Optimized Solution NPV: " + std::to_string(*Data.GetNPV()) << std::endl;
		std::cout << "Computation Time: " << difftime (End, Beginning) << " s" << std::endl;
	}
	//std::cout << "Computation Time - Copy: " << *Data.GetTotalDurationCopy() << " [s]." << std::endl;
	//std::cout << "Computation Time - DW: " << *Data.GetTotalDurationDW() << " [s]." << std::endl;
	//std::cout << "Computation Time - SM: " << *Data.GetTotalDurationSM() << " [s]." << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << std::endl;
        return 1;
    }
	return (0);
}
