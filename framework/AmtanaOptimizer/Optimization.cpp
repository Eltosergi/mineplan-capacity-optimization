#include <limits>
#include <iostream>
#include <string>
#include <ctime>
#include <thread>
#include <memory>
#include "ProcessingOptimizer.hpp"
#include "Optimization.hpp"

Optimization::Optimization(DataStructures* data, ProcessingOptimizer* processingOptimizerInstance){
	this->Data = data;
	this->ProcessingOptimizerInstance = processingOptimizerInstance;
}

void Optimization::GetOptimizedSolution(){
	
	std::cout << "->Optimizing the solution....." << std::endl;

	time_t Inicio, Termino;
	double CandidateObjectiveValue, TestObjectiveValue;
	int PassesWithoutImprovement = 0, MaximumPassesWithoutImprovement = 3;
	int tries = 0;
	bool SolutionHasImproved, SolutionHasImprovedInCurrentNeighbourhood;
	bool VisitNeighbourhood1 = true, VisitNeighbourhood2 = true, VisitNeighbourhood3 = true;
	std::vector <bool> VisitPeriodInNeighbourhood1(stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) + 1, true), VisitPeriodInNeighbourhood2(stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) + 1, true), VisitPeriodInNeighbourhood3(stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) + 1, true);
	std::forward_list <MineBlock*> PostponableMineBlockList, ExpeditableMineBlockList;
	MineBlock *CandidateBlock, *CandidateBlock2;

	// ProcessingOptimizer objects for being used by threads
	int NumberOfThreads = stoi((*(*this->Data).GetConfiguration())["NumberOfThreads"]) - 1;
	if (NumberOfThreads < 0){
		std::cout << "Abort: The number of threads cannot be less than 1." << std::endl;
		exit (-1);
	}
	std::vector <ProcessingOptimizer*> ProcessingOptimizerThreadVector;
    std::vector<std::unique_ptr<ProcessingOptimizer>> ProcessingOptimizerOwners;
	for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++){
        ProcessingOptimizerOwners.emplace_back(new ProcessingOptimizer(this->Data));
        ProcessingOptimizerThreadVector.push_back(ProcessingOptimizerOwners.back().get());
		//(ProcessingOptimizerThreadVector.back())->NPV();
	}

	do{
		SolutionHasImproved = false;
		// Exchange
		if (VisitNeighbourhood1){
			std::cout << "  ->Entering Exchange Neighbourhood....." << std::endl;
			VisitNeighbourhood1 = false;
			time(&Inicio);

			do { // Beging of the While (SolutionHasImprovedInCurrentNeighbourhood)

				SolutionHasImprovedInCurrentNeighbourhood = false;

				// In this case the range is until 'NumberOfPeriod - 1', because we are optimizing the solution; we don't
				// considered the time beyond.
				for (int PeriodIndex = 0; PeriodIndex < stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]); PeriodIndex++){
					if (VisitPeriodInNeighbourhood1[PeriodIndex]){
						std::cout << "Exchange -> Period: " + std::to_string(PeriodIndex) << std::endl;
						time(&Inicio);
						CandidateObjectiveValue = -std::numeric_limits<double>::infinity();
						this->MakePostponableMineBlockList(PeriodIndex, PostponableMineBlockList);
						this->MakeExpeditableMineBlockList(PeriodIndex + 1, ExpeditableMineBlockList);

						double TestObjectiveValue;
						MineBlock* PreCandidateBlock;
						
						for (std::forward_list<MineBlock*>::iterator MineBlockListIterator1 = PostponableMineBlockList.begin(); MineBlockListIterator1 != PostponableMineBlockList.end(); ++MineBlockListIterator1){
							for (std::forward_list<MineBlock*>::iterator MineBlockListIterator2 = ExpeditableMineBlockList.begin(); MineBlockListIterator2 != ExpeditableMineBlockList.end(); ++MineBlockListIterator2){
															
								std::vector <double> TestObjectiveValueThreadVector;
								std::vector <MineBlock*> PreCandidateBlockThreadVector;
								std::vector <std::thread> ThreadExchangeVector;

								for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++)
									TestObjectiveValueThreadVector.push_back (-std::numeric_limits<double>::infinity());

								for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++){
									if (MineBlockListIterator2 != ExpeditableMineBlockList.end()){
										
										auto FunctionThreadExchange = [&](MineBlock MineBlock1, MineBlock MineBlock2, int Position){
											TestObjectiveValueThreadVector[Position] = this->TestExchange((*(*this->Data).GetOreBody())[MineBlock1.XIndex][MineBlock1.YIndex][MineBlock1.ZIndex], (*(*this->Data).GetOreBody())[MineBlock2.XIndex][MineBlock2.YIndex][MineBlock2.ZIndex], *ProcessingOptimizerThreadVector[Position]);
										};

										ThreadExchangeVector.push_back (std::thread (FunctionThreadExchange, **MineBlockListIterator1, **MineBlockListIterator2, ThreadNumber));

										PreCandidateBlockThreadVector.push_back (*MineBlockListIterator2);
										MineBlockListIterator2++;
									} else {
										TestObjectiveValueThreadVector[ThreadNumber] = -std::numeric_limits<double>::infinity();
									}

								}
								
								if (MineBlockListIterator2 != ExpeditableMineBlockList.end()){
                                                                        TestObjectiveValue = this->TestExchange(**MineBlockListIterator1, **MineBlockListIterator2, *this->ProcessingOptimizerInstance);
									PreCandidateBlock = (*MineBlockListIterator2);

								} else {
									TestObjectiveValue = -std::numeric_limits<double>::infinity();
								}
								
								for (std::vector<std::thread>::iterator ThreadExchangeIterator = ThreadExchangeVector.begin(); ThreadExchangeIterator != ThreadExchangeVector.end(); ThreadExchangeIterator++)
									(*ThreadExchangeIterator).join();
								
								double TestObjectiveValueThread = -std::numeric_limits<double>::infinity();
								MineBlock* PreCandidateBlockThread;
																
								for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++){
									if (std::isfinite(TestObjectiveValueThreadVector[ThreadNumber]) && TestObjectiveValueThreadVector[ThreadNumber] >= TestObjectiveValueThread){
										TestObjectiveValueThread = TestObjectiveValueThreadVector[ThreadNumber];
										PreCandidateBlockThread = PreCandidateBlockThreadVector[ThreadNumber];
									}
								}

								if (TestObjectiveValue >= TestObjectiveValueThread and TestObjectiveValue >= CandidateObjectiveValue){
									CandidateObjectiveValue = TestObjectiveValue;
									CandidateBlock = (*MineBlockListIterator1);
									CandidateBlock2 = PreCandidateBlock;
								}
								if (TestObjectiveValueThread > TestObjectiveValue and TestObjectiveValueThread >= CandidateObjectiveValue){
									CandidateObjectiveValue = TestObjectiveValueThread;
									CandidateBlock = (*MineBlockListIterator1);
									CandidateBlock2 = PreCandidateBlockThread;
								}

								//if (flag){
								//	std::cout << "CandidateObjectiveValue: " + std::to_string(CandidateObjectiveValue) << std::endl;
								//}

								if (MineBlockListIterator2 == ExpeditableMineBlockList.end()){
									break;
								}
							}
							//std::cout << "Sale del for de más adentro" << std::endl;
						}

						// If candidate solution supercedes incumbent, then candidate becomes the new incumbent
						
						//std::cout << "CandidateObjectiveValue: " << std::to_string(CandidateObjectiveValue) << std::endl;
						//std::cout << "CandidateObjectiveValue: " << std::to_string(CandidateObjectiveValue) << " NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
						if (CandidateObjectiveValue >= (*(*this->Data).GetNPV())){

							//std::cout << "CandidateObjectiveValue: " << std::to_string(CandidateObjectiveValue) << " NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
							if ((CandidateObjectiveValue - (*(*this->Data).GetNPV())) > std::max(1.0, std::abs(*Data->GetNPV() + Data->DiscountedInvestment())) * 0.000001){
								//std::cout << "Entro" << std::endl;
								SolutionHasImproved = true;
								SolutionHasImprovedInCurrentNeighbourhood = true;
								
								std::cout << "Exchange de: " << (*CandidateBlock).XIndex << " - " << (*CandidateBlock).YIndex << " - " << (*CandidateBlock).ZIndex << " y: " << (*CandidateBlock2).XIndex << " - " << (*CandidateBlock2).YIndex << " - " << (*CandidateBlock2).ZIndex << std::endl;
								std::cout << "Valor Antes: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
								std::cout << "Valor Despues: " + std::to_string(CandidateObjectiveValue) << std::endl;
								//tries++;
								//if (tries == 3) exit (-1);
								(*(*this->Data).GetNPV()) = CandidateObjectiveValue;
							}

							//std::cout << "Exchange de: " << (*CandidateBlock).XIndex << " - " << (*CandidateBlock).YIndex << " - " << (*CandidateBlock).ZIndex << " y: " << (*CandidateBlock2).XIndex << " - " << (*CandidateBlock2).YIndex << " - " << (*CandidateBlock2).ZIndex << std::endl;
							//if ((*CandidateBlock).XIndex == 2 and (*CandidateBlock).YIndex == 14 and (*CandidateBlock).ZIndex == 3){
							//	std::cout << "Encontrado en Exchange -> Bloque 1" << std::endl;
							//	exit (-1);
							//}
							//if ((*CandidateBlock2).XIndex == 2 and (*CandidateBlock2).YIndex == 14 and (*CandidateBlock2).ZIndex == 3){
							//       	std::cout << "Encontrado en Exchange -> Bloque 2" << std::endl;
							//	exit (-1);
							//}
							//std::cout << "Valor Antes NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;	
							this->ExecuteExchange(*CandidateBlock, *CandidateBlock2, ProcessingOptimizerThreadVector);
							//if ((*(*this->Data).GetMaximumMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock]] < (*(*this->Data).GetMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock]]){
							//	std::cout << "Exchange -> Mining Tonnage Of Period: " + std::to_string((*(*this->Data).GetMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock]]) << std::endl;
							//}
							//if ((*(*this->Data).GetMaximumMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock2]] < (*(*this->Data).GetMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock2]]){
							//	std::cout << "Exchange -> Mining Tonnage Of Period: " + std::to_string((*(*this->Data).GetMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock2]]) << std::endl;
							//}
							VisitNeighbourhood2 = true;
							VisitNeighbourhood3 = true;
							if (PeriodIndex > 0){
								VisitPeriodInNeighbourhood1[PeriodIndex - 1] = true;
								VisitPeriodInNeighbourhood2[PeriodIndex - 1] = true;
								VisitPeriodInNeighbourhood3[PeriodIndex - 1] = true;
							}
							VisitPeriodInNeighbourhood1[PeriodIndex] = true;
							VisitPeriodInNeighbourhood2[PeriodIndex] = true;
							VisitPeriodInNeighbourhood3[PeriodIndex] = true;
							VisitPeriodInNeighbourhood1[PeriodIndex + 1] = true;
							VisitPeriodInNeighbourhood2[PeriodIndex + 1] = true;
							VisitPeriodInNeighbourhood3[PeriodIndex + 1] = true;
						} else {
							VisitPeriodInNeighbourhood1[PeriodIndex] = false;
						}
					}
					time(&Termino);
					//std::cout << "    Period Time: " << difftime (Termino, Inicio) << " s" << std::endl;
				} // End For PeriodIndex
			} while (SolutionHasImprovedInCurrentNeighbourhood);
			time(&Termino);

			std::cout << "    OK - Time: " << difftime(Termino, Inicio) << " s." << std::endl;
		} // End of the If (VisitNeighbourhood1)
		
		// Shift-After
		if (VisitNeighbourhood2){
			std::cout << "  ->Entering Shift-After Neighbourhood....." << std::endl;
			VisitNeighbourhood2 = false;
			time(&Inicio);

			do{ // Begin of the While (SolutionHasImprovedInCurrentNeighbourhood)
				SolutionHasImprovedInCurrentNeighbourhood = false;
				double TestObjectiveValue;
				//double TestObjectiveValueThread;
			        MineBlock* PreCandidateBlock;
				//MineBlock* PreCandidateBlockThread;

				for (int PeriodIndex = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) - 1; PeriodIndex >= 0; PeriodIndex--) {
					if (VisitPeriodInNeighbourhood2[PeriodIndex]){
						std::cout << "Shift-After -> Period: " + std::to_string(PeriodIndex) << std::endl;

						CandidateObjectiveValue = -std::numeric_limits<double>::infinity();
						
						for (MineBlockSet::iterator MineBlockSetIterator = (*(*this->Data).GetBlocksInPeriod())[PeriodIndex].begin(); MineBlockSetIterator != (*((*this->Data).GetBlocksInPeriod()))[PeriodIndex].end(); ++MineBlockSetIterator){
							std::vector <double> TestObjectiveValueThreadVector;
							std::vector <MineBlock*> PreCandidateBlockThreadVector;
							std::vector <std::thread> ThreadShiftAfterVector;

							for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++)
									TestObjectiveValueThreadVector.push_back (-std::numeric_limits<double>::infinity());
				
							for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++){
								if (MineBlockSetIterator != (*((*this->Data).GetBlocksInPeriod()))[PeriodIndex].end()){
									auto FunctionThreadShiftAfter = [&](MineBlock MineBlock1, int Position){
										TestObjectiveValueThreadVector[Position] = this->TestShiftAfter((*(*this->Data).GetOreBody())[MineBlock1.XIndex][MineBlock1.YIndex][MineBlock1.ZIndex], *ProcessingOptimizerThreadVector[Position]);
									};

									ThreadShiftAfterVector.push_back (std::thread (FunctionThreadShiftAfter, **MineBlockSetIterator, ThreadNumber));

									PreCandidateBlockThreadVector.push_back (*MineBlockSetIterator);
									MineBlockSetIterator++;
								} else {

									TestObjectiveValueThreadVector[ThreadNumber] = -std::numeric_limits<double>::infinity();
								}
							}
							
							if (MineBlockSetIterator != (*((*this->Data).GetBlocksInPeriod()))[PeriodIndex].end()){
								TestObjectiveValue = this->TestShiftAfter(**MineBlockSetIterator, *this->ProcessingOptimizerInstance);
								PreCandidateBlock = (*MineBlockSetIterator);
							} else {
								TestObjectiveValue = -std::numeric_limits<double>::infinity();
							}

							for (std::vector<std::thread>::iterator ThreadShiftAfterIterator = ThreadShiftAfterVector.begin(); ThreadShiftAfterIterator != ThreadShiftAfterVector.end(); ThreadShiftAfterIterator++)
								(*ThreadShiftAfterIterator).join();

							double TestObjectiveValueThread = -std::numeric_limits<double>::infinity();
							MineBlock* PreCandidateBlockThread;

							for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++){
								if (std::isfinite(TestObjectiveValueThreadVector[ThreadNumber]) && TestObjectiveValueThreadVector[ThreadNumber] >= TestObjectiveValueThread){
									TestObjectiveValueThread = TestObjectiveValueThreadVector[ThreadNumber];
									PreCandidateBlockThread = PreCandidateBlockThreadVector[ThreadNumber];
								}
							}

							if (TestObjectiveValue >= TestObjectiveValueThread and TestObjectiveValue >= CandidateObjectiveValue){
								CandidateObjectiveValue = TestObjectiveValue;
								CandidateBlock = PreCandidateBlock;
							}
							if (TestObjectiveValueThread > TestObjectiveValue and TestObjectiveValueThread >= CandidateObjectiveValue){
								CandidateObjectiveValue = TestObjectiveValueThread;
								CandidateBlock = PreCandidateBlockThread;
							}

							if (MineBlockSetIterator == (*((*this->Data).GetBlocksInPeriod()))[PeriodIndex].end()){
								break;
							}
						}
						//std::cout << "CandidateObjectiveValue: " << std::to_string(CandidateObjectiveValue) << " NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
						// If candidate solution supercedes incumbent, then candidate becomes the new incumbent
						if (CandidateObjectiveValue >= (*((*this->Data).GetNPV()))){

							//std::cout << "CandidateObjectiveValue: " << std::to_string(CandidateObjectiveValue) << " NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
							if ((CandidateObjectiveValue - (*((*this->Data).GetNPV()))) > std::max(1.0, std::abs(*Data->GetNPV() + Data->DiscountedInvestment())) * 0.000001){
								SolutionHasImproved = true;
								SolutionHasImprovedInCurrentNeighbourhood = true;
								std::cout << "Valor Antes: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
								(*((*this->Data).GetNPV())) = CandidateObjectiveValue;
								std::cout << "Valor Despues: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
							}

							//if ((*CandidateBlock).XIndex == 1 and (*CandidateBlock).YIndex == 14 and (*CandidateBlock).ZIndex == 2) std::cout << "Encontrado en Shift-After" << std::endl;
							//std::cout << "Shif-After de: " << (*CandidateBlock).XIndex << " - " << (*CandidateBlock).YIndex << " - " << (*CandidateBlock).ZIndex << std::endl;
							//std::cout << "Shift-After -> Valor Antes NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
							this->ExecuteShiftAfter(*CandidateBlock, ProcessingOptimizerThreadVector);
							//if ((*(*this->Data).GetMaximumMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock] + 1] < (*(*this->Data).GetMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock] + 1]){
							//	std::cout << "Shift-After -> Mining Tonnage Of Period: " + std::to_string((*(*this->Data).GetMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock] + 1]) << std::endl;
							//}
							//std::cout << "Shift-After -> Valor Despues NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
							VisitNeighbourhood1 = true;
							VisitNeighbourhood3 = true;
							if (PeriodIndex > 0){
								VisitPeriodInNeighbourhood1[PeriodIndex - 1] = true;
								VisitPeriodInNeighbourhood2[PeriodIndex - 1] = true;
								VisitPeriodInNeighbourhood3[PeriodIndex - 1] = true;
							}
							VisitPeriodInNeighbourhood1[PeriodIndex] = true;
							VisitPeriodInNeighbourhood2[PeriodIndex] = true;
							VisitPeriodInNeighbourhood3[PeriodIndex] = true;
							VisitPeriodInNeighbourhood1[PeriodIndex + 1] = true;
							VisitPeriodInNeighbourhood2[PeriodIndex + 1] = true;
							VisitPeriodInNeighbourhood3[PeriodIndex + 1] = true;

						} else {
							VisitPeriodInNeighbourhood2[PeriodIndex] = false;
						}
					} // Enf If (VisitPeriodIn Neighbourhood2[periodIndex])
					//std::cout << "Llega "  + std::to_string(PeriodIndex)<< std::endl;
				} // End For PeriodIndex

			} while (SolutionHasImprovedInCurrentNeighbourhood);
			time(&Termino);

			std::cout << "    OK - Time: " << difftime(Termino, Inicio) << " s." << std::endl;
			//std::cout << "    OK" << std::endl;
		} // End of the If (VisitNeighbourhood2)
		
		// Shift-Before
		if (VisitNeighbourhood3){
			std::cout << "  ->Entering Shift-Before Neighbourhood....." << std::endl;
			VisitNeighbourhood3 = false;
			time(&Inicio);

			do{ // Begin of While (SolutionHasImprovedInCurrentNeighbourhood)
				SolutionHasImprovedInCurrentNeighbourhood = false;
				double TestObjectiveValue;
				//double TestObjectiveValueThread;
				MineBlock* PreCandidateBlock;
				//MineBlock* PreCandidateBlockThread;

				for (int PeriodIndex = 1; PeriodIndex <= stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]); PeriodIndex++){
					if (VisitPeriodInNeighbourhood3[PeriodIndex]){
						std::cout << "Shift-Before -> Period: " + std::to_string(PeriodIndex) << std::endl;

						CandidateObjectiveValue = -std::numeric_limits<double>::infinity();
						//std::cout << "Cantidad de bloques en el Period: " << (*(*this->Data).GetBlocksInPeriod())[PeriodIndex].size() << std::endl;

						// Find candidate solution
						for (MineBlockSet::iterator MineBlockSetIterator = (*(*this->Data).GetBlocksInPeriod())[PeriodIndex].begin(); MineBlockSetIterator != (*(*this->Data).GetBlocksInPeriod())[PeriodIndex].end(); ++MineBlockSetIterator){
							std::vector <double> TestObjectiveValueThreadVector;
							std::vector <MineBlock*> PreCandidateBlockThreadVector;
							std::vector <std::thread> ThreadShiftBeforeVector;
							for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++)
								TestObjectiveValueThreadVector.push_back (-std::numeric_limits<double>::infinity());

							for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++){
								if (MineBlockSetIterator != (*((*this->Data).GetBlocksInPeriod()))[PeriodIndex].end()){
									auto FunctionThreadShiftBefore = [&](MineBlock MineBlock1, int Position){
										TestObjectiveValueThreadVector[Position] = this->TestShiftBefore((*(*this->Data).GetOreBody())[MineBlock1.XIndex][MineBlock1.YIndex][MineBlock1.ZIndex], *ProcessingOptimizerThreadVector[Position]);
									};

									ThreadShiftBeforeVector.push_back(std::thread (FunctionThreadShiftBefore, **MineBlockSetIterator, ThreadNumber));
									PreCandidateBlockThreadVector.push_back (*MineBlockSetIterator);
									MineBlockSetIterator++;
								} else {
									TestObjectiveValueThreadVector[ThreadNumber] = -std::numeric_limits<double>::infinity();
								}
							}

							if (MineBlockSetIterator != (*((*this->Data).GetBlocksInPeriod()))[PeriodIndex].end()){
								TestObjectiveValue = this->TestShiftBefore(**MineBlockSetIterator, *this->ProcessingOptimizerInstance);
								PreCandidateBlock = (*MineBlockSetIterator);
							} else {
								TestObjectiveValue = -std::numeric_limits<double>::infinity();
							}

							for (std::vector<std::thread>::iterator ThreadShiftBeforeIterator = ThreadShiftBeforeVector.begin(); ThreadShiftBeforeIterator != ThreadShiftBeforeVector.end(); ThreadShiftBeforeIterator++)
								(*ThreadShiftBeforeIterator).join();

							double TestObjectiveValueThread = -std::numeric_limits<double>::infinity();
							MineBlock* PreCandidateBlockThread;

							for (int ThreadNumber = 0; ThreadNumber < NumberOfThreads; ThreadNumber++){
								if (std::isfinite(TestObjectiveValueThreadVector[ThreadNumber]) && TestObjectiveValueThreadVector[ThreadNumber] >= TestObjectiveValueThread){
									TestObjectiveValueThread = TestObjectiveValueThreadVector[ThreadNumber];
									PreCandidateBlockThread = PreCandidateBlockThreadVector[ThreadNumber];
								}
							}

							if (TestObjectiveValue >= TestObjectiveValueThread and TestObjectiveValue >= CandidateObjectiveValue){
								CandidateObjectiveValue = TestObjectiveValue;
								CandidateBlock = PreCandidateBlock;
							}
							if (TestObjectiveValueThread > TestObjectiveValue and TestObjectiveValueThread >= CandidateObjectiveValue){
								CandidateObjectiveValue = TestObjectiveValueThread;
								CandidateBlock = PreCandidateBlockThread;
							}

							if (MineBlockSetIterator == (*((*this->Data).GetBlocksInPeriod()))[PeriodIndex].end()){
								break;
							}
						}
						//std::cout << "CandidateObjectiveValue: " << std::to_string(CandidateObjectiveValue) << " NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
						// If candidate solution supercedes incumbent, then candidate becomes the new incumbent
						if (CandidateObjectiveValue >= (*(*this->Data).GetNPV())){

							//std::cout << "CandidateObjectiveValue: " << std::to_string(CandidateObjectiveValue) << " NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
							if ((CandidateObjectiveValue - (*(*this->Data).GetNPV())) > std::max(1.0, std::abs(*Data->GetNPV() + Data->DiscountedInvestment())) * 0.000001){
								SolutionHasImproved = true;
								SolutionHasImprovedInCurrentNeighbourhood = true;
								std::cout << "Valor Antes: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
								std::cout << "Valor Despues: " + std::to_string(CandidateObjectiveValue) << std::endl;
								(*(*this->Data).GetNPV()) = CandidateObjectiveValue;
							}
							//std::cout << "Shift-Before -> Valor Antes NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;

							//std::cout << "Shif-Before de: " << (*CandidateBlock).XIndex << " - " << (*CandidateBlock).YIndex << " - " << (*CandidateBlock).ZIndex << std::endl;
							this->ExecuteShiftBefore(*CandidateBlock, ProcessingOptimizerThreadVector);
                            if (Data->VariablePeriodParameters) {
                                std::fill(VisitPeriodInNeighbourhood1.begin(), VisitPeriodInNeighbourhood1.end(), true);
                                std::fill(VisitPeriodInNeighbourhood2.begin(), VisitPeriodInNeighbourhood2.end(), true);
                                std::fill(VisitPeriodInNeighbourhood3.begin(), VisitPeriodInNeighbourhood3.end(), true);
                            }
							//if ((*(*this->Data).GetMaximumMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock] - 1] < (*(*this->Data).GetMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock] - 1]){
							//	std::cout << "Shift-Before -> Mining Tonnage Of Period: " + std::to_string((*(*this->Data).GetMiningTonnageOfPeriod())[(*(*this->Data).GetExtractionPeriodOfBlock())[CandidateBlock] - 1]) << std::endl;
							//}
							//std::cout << "Shift-Before -> Valor Despues NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;
							VisitNeighbourhood1 = true;
							VisitNeighbourhood2 = true;
							VisitPeriodInNeighbourhood1[PeriodIndex - 1] = true; //may need override "false" from previous pass
							VisitPeriodInNeighbourhood2[PeriodIndex - 1] = true;
							VisitPeriodInNeighbourhood3[PeriodIndex - 1] = true;
							VisitPeriodInNeighbourhood1[PeriodIndex] = true;
							VisitPeriodInNeighbourhood2[PeriodIndex] = true;
							VisitPeriodInNeighbourhood3[PeriodIndex] = true;
							//if (PeriodIndex < stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) - 1){
								
							if (PeriodIndex < stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"])){
								VisitPeriodInNeighbourhood1[PeriodIndex + 1] = true;
								VisitPeriodInNeighbourhood2[PeriodIndex + 1] = true;
								VisitPeriodInNeighbourhood3[PeriodIndex + 1] = true;
							}
						} else {
							VisitPeriodInNeighbourhood3[PeriodIndex] = false;
						}
					}
				} // End For PeriodIndex
			} while (SolutionHasImprovedInCurrentNeighbourhood);
			
			time(&Termino);

			//std::cout << "    OK" << std::endl;
			//std::cout << "SolutionHasImproved: " << SolutionHasImproved << std::endl;
			//std::cout << "PassesWithoutImprovement: " << PassesWithoutImprovement << std::endl;
			std::cout << "    OK - Time: " << difftime(Termino, Inicio) << " s." << std::endl;
			//std::cout << "    Ok" << std::endl;
			
		} // End of the If (VisitNeighbourhood3)
		
		/*std::cout << "Despues de Shift Before" << std::endl;
		std::cout << "SolutionHasImproved: " << SolutionHasImproved << std::endl;
		std::cout << "PassesWithoutImprovement: " << PassesWithoutImprovement << std::endl;
		std::cout << "Visitar Exchange: " << VisitNeighbourhood1 << std::endl;
		std::cout << "Visitar Shift-After: " << VisitNeighbourhood2 << std::endl;
		std::cout << "Visitar Shift-Before: " << VisitNeighbourhood3 << std::endl;*/

		if (SolutionHasImproved){
			PassesWithoutImprovement = 0;
		} else {
			PassesWithoutImprovement++;
		}
	} while (PassesWithoutImprovement < MaximumPassesWithoutImprovement);

	//std::cout << "  Optimized NPV: " + std::to_string((*(*this->Data).GetNPV())) << std::endl;

}

void Optimization::MakePostponableMineBlockList(int periodIndex, std::forward_list<MineBlock*>& postponableMineBlockList){
	//Clear existing content of list
	postponableMineBlockList.clear();

	for (MineBlockSet::iterator mineBlockIterator = (*(*this->Data).GetBlocksInPeriod())[periodIndex].begin(); mineBlockIterator != (*(*this->Data).GetBlocksInPeriod())[periodIndex].end(); ++mineBlockIterator){
		if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator] < (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator]){
			postponableMineBlockList.push_front(*mineBlockIterator);
		}
	}
}

void Optimization::MakeExpeditableMineBlockList(int periodIndex, std::forward_list<MineBlock*>& expeditableMineBlockList){
	expeditableMineBlockList.clear();

	for (MineBlockSet::iterator mineBlockIterator = (*(*this->Data).GetBlocksInPeriod())[periodIndex].begin(); mineBlockIterator != (*(*this->Data).GetBlocksInPeriod())[periodIndex].end(); ++mineBlockIterator){
		//if ((*mineBlockIterator)->XIndex == 1 and (*mineBlockIterator)->YIndex == 16 and (*mineBlockIterator)->ZIndex == 1){
		//	std::cout << "ERROR.................... ERROR" << std::endl;
		//}
		if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator] > (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator]){
			expeditableMineBlockList.push_front(*mineBlockIterator);
		}
	}
}

int Optimization::BestEarlierPeriod(MineBlock& b, ProcessingOptimizer& optimizer) {
    const int current = Data->GetExtractionPeriodOfBlock()->at(&b);
    int best = current - 1;
    if (!Data->VariablePeriodParameters) return best;
    double value = -std::numeric_limits<double>::infinity();
    // Variable economics or closed periods must not block entry into earlier periods.
    for (int target = current - 1; target >= 0; --target) {
        double candidate = TestShiftBeforeTo(b, optimizer, target);
        if (candidate > value) { value = candidate; best = target; }
    }
    return best;
}

double Optimization::TestShiftBefore(MineBlock& b, ProcessingOptimizer& optimizer) {
    return TestShiftBeforeTo(b, optimizer, BestEarlierPeriod(b, optimizer));
}

double Optimization::TestShiftBeforeTo(MineBlock& b, ProcessingOptimizer& processingOptimizerInstance, int TargetPeriod){
	// TransferredBlocks storages all the mineblocks which have been considered for moving
	MineBlockSet TransferredBlocks;
	// ActiveBlocks is a list of the mineblocks to be analyze
	// UnexploredBlocks is a list with the mineblocks that probably will be added to the ActiveBlocks list
	std::forward_list <MineBlock*> ActiveBlocks, UnexploredBlocks;
	// CurrentPeriod is the period where the mineblock is extracted
	int CurrentPeriod = (*(*this->Data).GetExtractionPeriodOfBlock())[&b];
	// TargetPeriod may skip closed or uneconomic periods.
	// RemainingMineTonnageCapacity storages the difference between the maximum mining tonnage of the period minus the current mining tonnage extracted of the period
	double RemainingMineTonnageCapacity = (*(*this->Data).GetMaximumMiningTonnageOfPeriod())[TargetPeriod] - (*(*this->Data).GetMiningTonnageOfPeriod())[TargetPeriod];
	double TransferredMiningTonnage = 0;
	// ActiveBlocks is initialized with the mineblock given as parameter
	ActiveBlocks.push_front (&b);
	// TransferredBlocks is initialized with the mineblock given as parameter
	TransferredBlocks.insert (&b);
	//bool flag = false;

	do{
		// This FOR calculates the tonnage of all mineblocks which the algorithm will try to move
		for (std::forward_list<MineBlock*>::iterator MineBlockIteratorList = ActiveBlocks.begin(); MineBlockIteratorList != ActiveBlocks.end(); ++MineBlockIteratorList){
			TransferredMiningTonnage += (*this->Data).GetRockTonnageOfMineBlock(*MineBlockIteratorList);
		}

		// If the algorithm is trying to move more tonnage than the plant is able to process, then the execution is blocked
		if (TransferredMiningTonnage > RemainingMineTonnageCapacity)
			return (-std::numeric_limits<double>::infinity());

		//std::cout << "Antes del segundo do" << std::endl; 
		do{
			// The following FOR counts the number of predecessors that the block has. In case this number is below 9 means one or more boundaries have been reached; therefore, the solution is not feasible.
			double NumberOfPredecessors = 0;

			for (std::forward_list<MineBlock*>::iterator MineBlockIteratorList = ActiveBlocks.front()->ImmediatePredecessorBlocks.begin(); MineBlockIteratorList != ActiveBlocks.front()->ImmediatePredecessorBlocks.end(); ++MineBlockIteratorList){
				// Se pregunta si el bloque pertenece al UPL. Basta con que uno sólo no sea parte del ULP para que se deseche la opción.
				std::unordered_set <int>::const_iterator got = (*(*this->Data).GetConsecutivePositions()).find((*MineBlockIteratorList)->ConsecutivePosition);
				if ((got == (*(*this->Data).GetConsecutivePositions()).end()) or (stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) != CurrentPeriod and (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] == stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]))){
				//if (stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) != CurrentPeriod and (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] == stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"])){
					return (-std::numeric_limits<double>::infinity());
				}
                int predecessorPeriod = Data->GetExtractionPeriodOfBlock()->at(*MineBlockIteratorList);
                if (predecessorPeriod > TargetPeriod && predecessorPeriod != CurrentPeriod)
                    return -std::numeric_limits<double>::infinity();
				NumberOfPredecessors++;
			}
			// If NumberOfPredecessors == 0 means that the block is at the surface (Z == MaxZ),
			// so it has no blocks above to be extracted first. In that case the loop body
			// is skipped entirely and the count stays at 0, which is a valid scenario.
			// Only one block is needed to invalidate the entire attempt.
			if (NumberOfPredecessors != 0 and NumberOfPredecessors < 9){
				return (-std::numeric_limits<double>::infinity());
			}

			// This FOR takes the first mineblock of the forward_list ActiveBlocks, and identifies the predecessors
			for (std::forward_list<MineBlock*>::iterator MineBlockIteratorList = ActiveBlocks.front()->ImmediatePredecessorBlocks.begin(); MineBlockIteratorList != ActiveBlocks.front()->ImmediatePredecessorBlocks.end(); ++MineBlockIteratorList){
				// This IF makes sure that the mineblocks to be considered must be from the period Current (that is part of the VND)
				// Besides, It avoids that a mineblock can be considered more than once
					if (((*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] == CurrentPeriod) and (TransferredBlocks.find(*MineBlockIteratorList) == TransferredBlocks.end())){

					//if ((*MineBlockIteratorList)->XIndex == 1 and (*MineBlockIteratorList)->YIndex == 13 and (*MineBlockIteratorList)->ZIndex == 2) std::cout << "Encontrado en Shif-Before" << std::endl;
					//std::cout << "Uno" << std::endl;
					// The predecessor blocks of the mineblock, are added to the list UnexploredBlocks
					UnexploredBlocks.push_front(*MineBlockIteratorList);
					//std::cout << "Dos" << std::endl;
					// The predecessor blocks are now considered as transferred
					TransferredBlocks.insert(*MineBlockIteratorList);
				}
			}
			//std::cout << "Despues del For" << std::endl;

			// It is deleted the first mineblock of the forward_list ActiveBlocks
			ActiveBlocks.pop_front();
		} while (!ActiveBlocks.empty());
		//std::cout << "Despues del segundo do" << std::endl;

		// The mineblocks storages in UnexploredBlocks are moved to ActiveBlocks to be analyzed in a future loop of the Do-While
		ActiveBlocks.swap(UnexploredBlocks);

	} while (!ActiveBlocks.empty());

	// Processing stream
	MineBlockSet Empty;
	return (processingOptimizerInstance.Test(CurrentPeriod, TransferredBlocks, TargetPeriod, Empty));
}

void Optimization::ExecuteShiftBefore(MineBlock& b, std::vector<ProcessingOptimizer*>& processingOptimizerInstanceVector){
	int CurrentPeriod = (*(*this->Data).GetExtractionPeriodOfBlock())[&b];
	int TargetPeriod = BestEarlierPeriod(b, *ProcessingOptimizerInstance);
	std::forward_list <MineBlock*> ActiveBlocks, UnexploredBlocks;
	MineBlockSet TransferredBlocks;

	ActiveBlocks.push_front(&b);
	TransferredBlocks.insert(&b);
	// The Initial change is carried out
	//std::cout << "Change carried out!" << std::endl;
	(*(*this->Data).GetBlocksInPeriod())[CurrentPeriod].erase(&b);
	(*(*this->Data).GetBlocksInPeriod())[TargetPeriod].insert(&b);
	(*(*this->Data).GetExtractionPeriodOfBlock())[&b] = TargetPeriod;

	do{
		do{
			for (std::forward_list<MineBlock*>::iterator MineBlockListIterator = ActiveBlocks.front()->ImmediatePredecessorBlocks.begin(); MineBlockListIterator != ActiveBlocks.front()->ImmediatePredecessorBlocks.end(); ++MineBlockListIterator){
				if ((*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockListIterator] == CurrentPeriod){
					UnexploredBlocks.push_front(*MineBlockListIterator);
					TransferredBlocks.insert(*MineBlockListIterator);
					(*(*this->Data).GetBlocksInPeriod())[CurrentPeriod].erase(*MineBlockListIterator);
					(*(*this->Data).GetBlocksInPeriod())[TargetPeriod].insert(*MineBlockListIterator);
					(*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockListIterator] = TargetPeriod;
				}
			}
			ActiveBlocks.pop_front();

		} while (!ActiveBlocks.empty());

		ActiveBlocks.swap(UnexploredBlocks);

	} while (!ActiveBlocks.empty());

	for (MineBlockSet::iterator BlockSetIterator = TransferredBlocks.begin(); BlockSetIterator != TransferredBlocks.end(); ++BlockSetIterator){
		for (std::forward_list<MineBlock*>::iterator MineBlockListIterator = (*BlockSetIterator)->ImmediateSuccessorBlocks.begin(); MineBlockListIterator != (*BlockSetIterator)->ImmediateSuccessorBlocks.end(); ++MineBlockListIterator){
			if (CurrentPeriod == (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*MineBlockListIterator]){
				(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*MineBlockListIterator] = 0;
				for (std::forward_list<MineBlock*>::iterator MineBlockListIterator2 = (*MineBlockListIterator)->ImmediatePredecessorBlocks.begin(); MineBlockListIterator2 != (*MineBlockListIterator)->ImmediatePredecessorBlocks.end(); ++MineBlockListIterator2){
					if ((*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockListIterator2] > (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*MineBlockListIterator]){
						(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*MineBlockListIterator] = (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockListIterator2];
					}
				}
			}
		}
	}

	for (MineBlockSet::iterator BlockSetIterator = TransferredBlocks.begin(); BlockSetIterator != TransferredBlocks.end(); ++BlockSetIterator){
		for (std::forward_list<MineBlock*>::iterator MineBlockListIterator = (*BlockSetIterator)->ImmediatePredecessorBlocks.begin(); MineBlockListIterator != (*BlockSetIterator)->ImmediatePredecessorBlocks.end(); ++MineBlockListIterator){
			if (TargetPeriod < (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*MineBlockListIterator]){
				(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*MineBlockListIterator] = TargetPeriod;
			}
		}
	}

	for (MineBlockSet::iterator BlockSetIterator = TransferredBlocks.begin(); BlockSetIterator != TransferredBlocks.end(); ++BlockSetIterator){
		(*(*this->Data).GetMiningTonnageOfPeriod())[CurrentPeriod] -= (*this->Data).GetRockTonnageOfMineBlock(*BlockSetIterator);
		(*(*this->Data).GetMiningTonnageOfPeriod())[TargetPeriod] += (*this->Data).GetRockTonnageOfMineBlock(*BlockSetIterator);
		(*(*this->Data).GetExpectedMiningCost()) += (*BlockSetIterator)->ExpectedCostOfMiningBlockInPeriod[TargetPeriod] - (*BlockSetIterator)->ExpectedCostOfMiningBlockInPeriod[CurrentPeriod];
		//(*(*this->Data).GetExpectedMiningPrice()) += (*BlockSetIterator)->ExpectedPriceOfMiningBlockInPeriod[TargetPeriod] - (*BlockSetIterator)->ExpectedPriceOfMiningBlockInPeriod[CurrentPeriod];
	}
	//std::cout << "Termina el Execute Shift-Before" << std::endl;
        //Update processing optimization data
	MineBlockSet Empty;
	(*this->ProcessingOptimizerInstance).Execute(CurrentPeriod, TransferredBlocks, TargetPeriod, Empty);

	/*for (std::vector<ProcessingOptimizer*>::iterator processingOptimizerIterator = processingOptimizerInstanceVector.begin(); processingOptimizerIterator != processingOptimizerInstanceVector.end(); processingOptimizerIterator++)
		(*processingOptimizerIterator)->Execute(CurrentPeriod, TransferredBlocks, TargetPeriod, Empty);*/
	//processingOptimizerInstance.Execute(CurrentPeriod, TransferredBlocks, TargetPeriod, Empty);
}

double Optimization::TestExchange(MineBlock& b, MineBlock& bp, ProcessingOptimizer& processingOptimizerInstance){
	int initialPeriodOfB = (*(*this->Data).GetExtractionPeriodOfBlock())[&b];
	int initialPeriodOfBPrime = (*(*this->Data).GetExtractionPeriodOfBlock())[&bp];
		
	//Verify that mine tonnage constraint is satisfied. Otherwise, return (-infinity)
	if ((*(*this->Data).GetMiningTonnageOfPeriod())[initialPeriodOfB] + (*this->Data).GetRockTonnageOfMineBlock(&bp) - (*this->Data).GetRockTonnageOfMineBlock(&b) > (*(*this->Data).GetMaximumMiningTonnageOfPeriod())[initialPeriodOfB] or (*(*this->Data).GetMiningTonnageOfPeriod())[initialPeriodOfBPrime] + (*this->Data).GetRockTonnageOfMineBlock(&b) - (*this->Data).GetRockTonnageOfMineBlock(&bp) > (*(*this->Data).GetMaximumMiningTonnageOfPeriod())[initialPeriodOfBPrime]){
		return (-std::numeric_limits<double>::infinity());
	}

	double NumberOfPredecessors = 0;

	for (std::forward_list<MineBlock*>::iterator MineBlockIteratorList = b.ImmediatePredecessorBlocks.begin(); MineBlockIteratorList != b.ImmediatePredecessorBlocks.end(); ++MineBlockIteratorList){
		// Se pregunta si el bloque pertenece al UPL. Basta con que uno sólo no sea parte del ULP para que se deseche la opción.
		std::unordered_set <int>::const_iterator got = (*(*this->Data).GetConsecutivePositions()).find((*MineBlockIteratorList)->ConsecutivePosition);

		if (got == (*(*this->Data).GetConsecutivePositions()).end() or (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] == stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) or (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] > initialPeriodOfB){
		//if ((*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] == stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) or (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] > initialPeriodOfB){
			return (-std::numeric_limits<double>::infinity());
		}
		NumberOfPredecessors++;
	}
	// If NumberOfPredecessors == 0 means that the block is at the surface (Z == MaxZ),
	// so it has no blocks above to be extracted first. In that case the loop body
	// is skipped entirely and the count stays at 0, which is a valid scenario.
	// Only one block is needed to invalidate the entire attempt.
	if (NumberOfPredecessors != 0 and NumberOfPredecessors < 9){
		//std::cout << "Se corta dentro de la cantidad de Number of Predecessors: " + std::to_string(NumberOfPredecessors) << std::endl;
		return (-std::numeric_limits<double>::infinity());
	}

	NumberOfPredecessors = 0;

	for (std::forward_list<MineBlock*>::iterator MineBlockIteratorList = bp.ImmediatePredecessorBlocks.begin(); MineBlockIteratorList != bp.ImmediatePredecessorBlocks.end(); ++MineBlockIteratorList){
		// Se pregunta si el bloque pertenece al UPL. Basta con que uno sólo no sea parte del ULP para que se deseche la opción.
		std::unordered_set <int>::const_iterator got = (*(*this->Data).GetConsecutivePositions()).find((*MineBlockIteratorList)->ConsecutivePosition);

		if (got == (*(*this->Data).GetConsecutivePositions()).end() or (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] == stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) or (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] > initialPeriodOfBPrime){
		//if ((*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] == stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) or (*(*this->Data).GetExtractionPeriodOfBlock())[*MineBlockIteratorList] > initialPeriodOfBPrime){
			return (-std::numeric_limits<double>::infinity());
		}
		NumberOfPredecessors++;
	}
	// Only one block is needed to invalidate the entire attempt.
	if (NumberOfPredecessors != 0 and NumberOfPredecessors < 9){
		return (-std::numeric_limits<double>::infinity());
	}
	
	//Verify that predecessor excavation contraints are satisfied. Otherwise, return (-infinity)
	if (initialPeriodOfB < initialPeriodOfBPrime){
		//Block b cannot be exchanged forward with bp if bp is a successor of b
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = b.ImmediateSuccessorBlocks.begin(); mineBlockIterator != b.ImmediateSuccessorBlocks.end(); ++mineBlockIterator)
			if (*mineBlockIterator == &bp)
				return (-std::numeric_limits<double>::infinity());
		
		//Block b cannot be postponed beyond the its (immediate) successors
		//Note: this comparison is not necessary if b has been predetermined to be postponable (see MakePostponableMineBlockList method)
		if (initialPeriodOfBPrime > (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[&b])
			return (-std::numeric_limits<double>::infinity());

		//Block bp cannot be expedited ahead of its (immediate) predecessors
		//Note: this comparison is not necessary if bp has been predetermined to be expeditable (see MakeExpeditableMineBlockList method)
		if (initialPeriodOfB < (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[&bp])
			return (-std::numeric_limits<double>::infinity());
	}
	else{
		//Block b cannot be exchanged backward with bp if bp is a predecessor of b
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = b.ImmediatePredecessorBlocks.begin(); mineBlockIterator != b.ImmediatePredecessorBlocks.end(); ++mineBlockIterator)
			if (*mineBlockIterator == &bp)
				return (-std::numeric_limits<double>::infinity());

		//Block b cannot be expedited ahead of its (immediate) predecessors
		//Note: this comparison is not necessary if b has been predetermined to be expeditable (see MakeExpeditableMineBlockList method)
		if (initialPeriodOfBPrime < (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[&b])
			return (-std::numeric_limits<double>::infinity());

		//Block bp cannot be postponed beyond the its (immediate) successors
		//Note: this comparison is not necessary if bp has been predetermined to be expeditable (see MakeExpeditableMineBlockList method)
		if (initialPeriodOfB > (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[&bp])
			return (-std::numeric_limits<double>::infinity());
	}

	MineBlockSet TransferredBlockB;
	MineBlockSet TransferredBlockBP;
	TransferredBlockB.insert(&b);
	TransferredBlockBP.insert(&bp);
	
	return (processingOptimizerInstance.Test(initialPeriodOfB, TransferredBlockB, initialPeriodOfBPrime, TransferredBlockBP));
}

void Optimization::ExecuteExchange(MineBlock& b, MineBlock& bp, std::vector<ProcessingOptimizer*>& processingOptimizerInstanceVector){
	int initialPeriodOfB = (*(*this->Data).GetExtractionPeriodOfBlock())[&b];
	int initialPeriodOfBPrime = (*(*this->Data).GetExtractionPeriodOfBlock())[&bp];
	
	//Update BlocksInPeriod
	(*(*this->Data).GetBlocksInPeriod())[initialPeriodOfB].erase(&b);
	(*(*this->Data).GetBlocksInPeriod())[initialPeriodOfBPrime].insert(&b);
	(*(*this->Data).GetBlocksInPeriod())[initialPeriodOfB].insert(&bp);
	(*(*this->Data).GetBlocksInPeriod())[initialPeriodOfBPrime].erase(&bp);

	//Update ExtractionPeriodOfBlock
	(*(*this->Data).GetExtractionPeriodOfBlock())[&b] = initialPeriodOfBPrime;
	(*(*this->Data).GetExtractionPeriodOfBlock())[&bp] = initialPeriodOfB;

	//Update LastestImmediatePredecessorPeriodOfBlock for immediate successors of b
	if (initialPeriodOfBPrime > initialPeriodOfB) // if b has been postposed then it may become a new determinant of LastestImmediatePredecessorPeriodOfBlock
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = b.ImmediateSuccessorBlocks.begin(); mineBlockIterator != b.ImmediateSuccessorBlocks.end(); ++mineBlockIterator)
		{
			if(initialPeriodOfBPrime > (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator])
				(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator] = initialPeriodOfBPrime;
		}//(these braces are necessary so that the following "else if" is excluded from the for-loop) 
	else if (initialPeriodOfBPrime < initialPeriodOfB)// if b has been expedited, there may be cases where it had been, and is no longer, a determinant
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = b.ImmediateSuccessorBlocks.begin(); mineBlockIterator != b.ImmediateSuccessorBlocks.end(); ++mineBlockIterator)
			if (initialPeriodOfB == (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator]) // check if it had been a determinant
			{
				(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator] = 0;
				for (std::forward_list<MineBlock*>::iterator mineBlockIterator2 = (*mineBlockIterator)->ImmediatePredecessorBlocks.begin(); mineBlockIterator2 != (*mineBlockIterator)->ImmediatePredecessorBlocks.end(); ++mineBlockIterator2)
					if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2] > (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator])
						(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator] = (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2];
			}

	//Update LastestImmediatePredecessorPeriodOfBlock for immediate successors of bp 
	if (initialPeriodOfB > initialPeriodOfBPrime) // if bp has been postposed then it may become a new determinant of LastestImmediatePredecessorPeriodOfBlock
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = bp.ImmediateSuccessorBlocks.begin(); mineBlockIterator != bp.ImmediateSuccessorBlocks.end(); ++mineBlockIterator)
		{
			if(initialPeriodOfB > (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator])
				(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator] = initialPeriodOfB;
		}//(these braces are necessary so that the following "else if" is excluded from the for-loop) 
	else if (initialPeriodOfB < initialPeriodOfBPrime)// if bp has been expedited, there may be cases where it had been, and is no longer a determinant
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = bp.ImmediateSuccessorBlocks.begin(); mineBlockIterator != bp.ImmediateSuccessorBlocks.end(); ++mineBlockIterator)
			if (initialPeriodOfBPrime == (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator]) // check if it had been a determinant
			{
				(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator] = 0;
				for (std::forward_list<MineBlock*>::iterator mineBlockIterator2 = (*mineBlockIterator)->ImmediatePredecessorBlocks.begin(); mineBlockIterator2 != (*mineBlockIterator)->ImmediatePredecessorBlocks.end(); ++mineBlockIterator2)
					if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2] > (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator])
						(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator] = (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2];
			}

	//Update EarliestImmediateSuccessorPeriodOfBlock for immediate predecessors of b
	if (initialPeriodOfBPrime > initialPeriodOfB) // if b has been postposed then there may be cases where it had been, and is no longer, a determinant of EarliestImmediateSuccessorPeriodOfBlock
	{
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = b.ImmediatePredecessorBlocks.begin(); mineBlockIterator != b.ImmediatePredecessorBlocks.end(); ++mineBlockIterator)
			if (initialPeriodOfB == (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator]) // check if it had been a determinant
			{
				//(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) - 1;
				
				(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]);
				for (std::forward_list<MineBlock*>::iterator mineBlockIterator2 = (*mineBlockIterator)->ImmediateSuccessorBlocks.begin(); mineBlockIterator2 != (*mineBlockIterator)->ImmediateSuccessorBlocks.end(); ++mineBlockIterator2)
					if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2] < (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator])
						(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2];
			}
	}
	else if (initialPeriodOfBPrime < initialPeriodOfB) // if b has been expedited then it may become a new determinant
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = b.ImmediatePredecessorBlocks.begin(); mineBlockIterator != b.ImmediatePredecessorBlocks.end(); ++mineBlockIterator)
			if (initialPeriodOfBPrime < (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator])
				(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = initialPeriodOfBPrime;

	//Update EarliestImmediateSuccessorPeriodOfBlock for immediate predecessors of bp
	if (initialPeriodOfB > initialPeriodOfBPrime) // if bp has been postposed then there may be cases where it had been, and is no longer, a determinant of EarliestImmediateSuccessorPeriodOfBlock
	{
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = bp.ImmediatePredecessorBlocks.begin(); mineBlockIterator != bp.ImmediatePredecessorBlocks.end(); ++mineBlockIterator)
			if (initialPeriodOfBPrime == (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator]) // check if it had been a determinant
			{
				//(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) - 1;

				(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]);
				for (std::forward_list<MineBlock*>::iterator mineBlockIterator2 = (*mineBlockIterator)->ImmediateSuccessorBlocks.begin(); mineBlockIterator2 != (*mineBlockIterator)->ImmediateSuccessorBlocks.end(); ++mineBlockIterator2)
					if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2] < (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator])
						(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2];
			}
	}
	else if (initialPeriodOfB < initialPeriodOfBPrime) // if bp has expedited then it may become a new determinant
		for (std::forward_list<MineBlock*>::iterator mineBlockIterator = bp.ImmediatePredecessorBlocks.begin(); mineBlockIterator != bp.ImmediatePredecessorBlocks.end(); ++mineBlockIterator)
			if (initialPeriodOfB < (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator])
				(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = initialPeriodOfB;
				
	//Update MiningTonnageOfPeriod
	(*(*this->Data).GetMiningTonnageOfPeriod())[initialPeriodOfB] += (*this->Data).GetRockTonnageOfMineBlock(&bp) - (*this->Data).GetRockTonnageOfMineBlock(&b);
	(*(*this->Data).GetMiningTonnageOfPeriod())[initialPeriodOfBPrime] += (*this->Data).GetRockTonnageOfMineBlock(&b)- (*this->Data).GetRockTonnageOfMineBlock(&bp);

	//Update ExpectedMiningCost
	(*(*this->Data).GetExpectedMiningCost()) += b.ExpectedCostOfMiningBlockInPeriod[initialPeriodOfBPrime]
					 - b.ExpectedCostOfMiningBlockInPeriod[initialPeriodOfB]
					 + bp.ExpectedCostOfMiningBlockInPeriod[initialPeriodOfB]
					 - bp.ExpectedCostOfMiningBlockInPeriod[initialPeriodOfBPrime];

	//Update ExpectedPriceCost
	//(*(*this->Data).GetExpectedMiningPrice())  += b.ExpectedPriceOfMiningBlockInPeriod[initialPeriodOfBPrime]
	//				 - b.ExpectedPriceOfMiningBlockInPeriod[initialPeriodOfB]
	//				 + bp.ExpectedPriceOfMiningBlockInPeriod[initialPeriodOfB]
	//				 - bp.ExpectedPriceOfMiningBlockInPeriod[initialPeriodOfBPrime];

	//Update processing optimization data
	MineBlockSet TransferredBlockB;
	MineBlockSet TransferredBlockBP;
	TransferredBlockB.insert(&b);
	TransferredBlockBP.insert(&bp);
	(*this->ProcessingOptimizerInstance).Execute(initialPeriodOfB, TransferredBlockB, initialPeriodOfBPrime, TransferredBlockBP);
	/*for (std::vector<ProcessingOptimizer*>::iterator processingOptimizerIterator = processingOptimizerInstanceVector.begin(); processingOptimizerIterator != processingOptimizerInstanceVector.end(); processingOptimizerIterator++)
		(*processingOptimizerIterator)->Execute(initialPeriodOfB, TransferredBlockB, initialPeriodOfBPrime, TransferredBlockBP);*/
	//processingOptimizerInstance.Execute(initialPeriodOfB, TransferredBlockB, initialPeriodOfBPrime, TransferredBlockBP);
}

double Optimization::TestShiftAfter(MineBlock& b, ProcessingOptimizer& processingOptimizerInstance){
	int currentPeriod = (*(*this->Data).GetExtractionPeriodOfBlock())[&b];
	int targetPeriod = currentPeriod + 1;
	//std::cout << std::to_string(currentPeriod) + " - " + std::to_string(targetPeriod) << std::endl;
	std::forward_list<MineBlock*>::iterator mineBlockIterator;
	std::forward_list<MineBlock*> activeBlocks, unexploredBlocks;
	MineBlockSet transferredBlocks;

	//Verify that mine tonnage constraint is satisfied, using breadth-first-search.  Otherwise, return (-infinity)
	double remainingMineTonnageCapacity = (*(*this->Data).GetMaximumMiningTonnageOfPeriod())[targetPeriod] - (*(*this->Data).GetMiningTonnageOfPeriod())[targetPeriod];
	double transferredMiningTonnage = 0.0;
	activeBlocks.push_front(&b);
	transferredBlocks.insert(&b);
	do{
		//Tally tonnage across breadth
		//int tamanio = 1;
		for (mineBlockIterator = activeBlocks.begin(); mineBlockIterator != activeBlocks.end(); ++mineBlockIterator){
			//std::cout << "Justo antes: "  + std::to_string(tamanio) << std::endl;
			transferredMiningTonnage += (*this->Data).GetRockTonnageOfMineBlock(*mineBlockIterator);
			//std::cout << "Se cayo aca" << std::endl;
			//tamanio++;
		}
		
		//std::cout << "Aqui estamos" << std::endl;

		//Check if mine tonnage capacity is violated
		if (transferredMiningTonnage > remainingMineTonnageCapacity)
			return (-std::numeric_limits<double>::infinity());

		//Discover next layer of unexplored blocks,
		do{
			//int contar = 0;
			for (mineBlockIterator = activeBlocks.front()->ImmediateSuccessorBlocks.begin(); mineBlockIterator != activeBlocks.front()->ImmediateSuccessorBlocks.end(); ++mineBlockIterator){
				// Se pregunta si el bloque pertenece al UPL. Sólo se consideran bloques que pertenecen al UPL.
				std::unordered_set <int>::const_iterator got = (*(*this->Data).GetConsecutivePositions()).find((*mineBlockIterator)->ConsecutivePosition);

				if ((got != (*(*this->Data).GetConsecutivePositions()).end()) and ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator] == currentPeriod) and (transferredBlocks.find(*mineBlockIterator) == transferredBlocks.end()))
				//if (((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator] == currentPeriod) and
				//(transferredBlocks.find(*mineBlockIterator) == transferredBlocks.end())) //make sure that the block does not get visited twice
				{

					//if ((*mineBlockIterator)->XIndex == 1 and (*mineBlockIterator)->YIndex == 13 and (*mineBlockIterator)->ZIndex == 2) std::cout << "Encontrado en Shif-After" << std::endl;
					//std::cout << "Entro al if" << std::endl;
					unexploredBlocks.push_front(*mineBlockIterator);
					transferredBlocks.insert(*mineBlockIterator);
				}
			}
			/*if ((activeBlocks.front()->XIndex == 7) and (activeBlocks.front()->YIndex == 7) and (activeBlocks.front()->ZIndex == 1)){
				std::cout << "->Cantidad de Sucesores: " + std::to_string(contar) << std::endl;
				std::cout << "  ->Los Sucesores son:" << std::endl;
				for (mineBlockIterator = activeBlocks.front()->ImmediateSuccessorBlocks.begin(); mineBlockIterator != activeBlocks.front()->ImmediateSuccessorBlocks.end(); ++mineBlockIterator){
					std::cout << std::to_string((**mineBlockIterator).XIndex) + " - " + std::to_string((**mineBlockIterator).YIndex) + " - " + std::to_string((**mineBlockIterator).ZIndex) << std::endl;
				}
			}*/
			activeBlocks.pop_front();
		}
		while (!activeBlocks.empty());

		//Advance to the the next layer of blocks
		activeBlocks.swap(unexploredBlocks);
	}
	while(!activeBlocks.empty());

	MineBlockSet Empty;
	return (processingOptimizerInstance.Test(currentPeriod, transferredBlocks, targetPeriod, Empty));
}

void Optimization::ExecuteShiftAfter(MineBlock& b, std::vector<ProcessingOptimizer*>& processingOptimizerInstanceVector){
	int currentPeriod = (*(*this->Data).GetExtractionPeriodOfBlock())[&b];
	int targetPeriod = currentPeriod + 1;
	std::forward_list<MineBlock*>::iterator mineBlockIterator, mineBlockIterator2;
	std::forward_list<MineBlock*> activeBlocks, unexploredBlocks;
	MineBlockSet transferredBlocks;
	MineBlockSet::iterator blockSetIterator;
	//GradeToRockAndMetalTonnageMapList transferredScenarioRockAndMetalTonnages;
	
	// Construct set of blocks to be transferred using breadth-wise traversal while updating BlocksInPeriod and ExtractionPeriodOfBlock
	activeBlocks.push_front(&b);
	transferredBlocks.insert(&b);
	(*(*this->Data).GetBlocksInPeriod())[currentPeriod].erase(&b);
	(*(*this->Data).GetBlocksInPeriod())[targetPeriod].insert(&b);
	(*(*this->Data).GetExtractionPeriodOfBlock())[&b] = targetPeriod;
	do{
		//Discover next layer of unexplored blocks 
		do{
			for (mineBlockIterator = activeBlocks.front()->ImmediateSuccessorBlocks.begin(); mineBlockIterator != activeBlocks.front()->ImmediateSuccessorBlocks.end(); ++mineBlockIterator){
				// Se pregunta si el bloque pertenece al UPL. Sólo se consideran bloques que pertenecen al UPL.
				std::unordered_set <int>::const_iterator got = (*(*this->Data).GetConsecutivePositions()).find((*mineBlockIterator)->ConsecutivePosition);

				if (got != (*(*this->Data).GetConsecutivePositions()).end() and (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator] == currentPeriod){
				//if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator] == currentPeriod){
					unexploredBlocks.push_front(*mineBlockIterator);
					transferredBlocks.insert(*mineBlockIterator);
					(*(*this->Data).GetBlocksInPeriod())[currentPeriod].erase(*mineBlockIterator);
					(*(*this->Data).GetBlocksInPeriod())[targetPeriod].insert(*mineBlockIterator);
					(*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator] = targetPeriod;
				}
			}
			activeBlocks.pop_front();
		}
		while (!activeBlocks.empty());

		//Advance to the the next layer of blocks
		activeBlocks.swap(unexploredBlocks);
	}
	while(!activeBlocks.empty());
	
	//Update LastestImmediatePredecessorPeriodOfBlock for immediate successors of the transfered blocks
	for (blockSetIterator = transferredBlocks.begin(); blockSetIterator != transferredBlocks.end(); ++blockSetIterator)
		//The transfered blocks are being postponed, hence may become new determinants for LastestImmediatePredecessorPeriodOfBlock
		for (mineBlockIterator = (*blockSetIterator)->ImmediateSuccessorBlocks.begin(); mineBlockIterator != (*blockSetIterator)->ImmediateSuccessorBlocks.end(); ++mineBlockIterator)
			if (targetPeriod > (*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator])
				(*(*this->Data).GetLastestImmediatePredecessorPeriodOfBlock())[*mineBlockIterator] = targetPeriod;
	
	//Update EarliestImmediateSuccessorPeriodOfBlock for immediate predecessors of the transfered blocks
	for (blockSetIterator = transferredBlocks.begin(); blockSetIterator != transferredBlocks.end(); ++blockSetIterator)
		//The transfered blocks are being postponed. There are hence cases where these blocks had been, and are no longer, determinantes of EarliestImmediateSuccessorPeriodOfBlock
		for (mineBlockIterator = (*blockSetIterator)->ImmediatePredecessorBlocks.begin(); mineBlockIterator != (*blockSetIterator)->ImmediatePredecessorBlocks.end(); ++mineBlockIterator)
			if (currentPeriod == (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator]){ //check if it had been a determinant
				//(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]) - 1;

				(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = stoi((*(*this->Data).GetConfiguration())["NumberOfPeriods"]);
				for (mineBlockIterator2 = (*mineBlockIterator)->ImmediateSuccessorBlocks.begin(); mineBlockIterator2 != (*mineBlockIterator)->ImmediateSuccessorBlocks.end(); ++mineBlockIterator2)
					if ((*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2] < (*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator])
						(*(*this->Data).GetEarliestImmediateSuccessorPeriodOfBlock())[*mineBlockIterator] = (*(*this->Data).GetExtractionPeriodOfBlock())[*mineBlockIterator2];
			}
	
	//Excecute transfer in mining tonnage
	for (blockSetIterator = transferredBlocks.begin(); blockSetIterator != transferredBlocks.end(); ++blockSetIterator){
		(*(*this->Data).GetMiningTonnageOfPeriod())[currentPeriod] -= (*this->Data).GetRockTonnageOfMineBlock(*blockSetIterator);
		(*(*this->Data).GetMiningTonnageOfPeriod())[targetPeriod] += (*this->Data).GetRockTonnageOfMineBlock(*blockSetIterator);
	}

	//Update ExpectedMiningCost and ExpectedMiningPrice
	for (blockSetIterator = transferredBlocks.begin(); blockSetIterator != transferredBlocks.end(); ++blockSetIterator){
		(*(*this->Data).GetExpectedMiningCost()) += (*blockSetIterator)->ExpectedCostOfMiningBlockInPeriod[targetPeriod] 
									- (*blockSetIterator)->ExpectedCostOfMiningBlockInPeriod[currentPeriod];
		//(*(*this->Data).GetExpectedMiningPrice()) += (*blockSetIterator)->ExpectedPriceOfMiningBlockInPeriod[targetPeriod] - (*blockSetIterator)->ExpectedPriceOfMiningBlockInPeriod[currentPeriod];
	}

	MineBlockSet Empty;
	(*this->ProcessingOptimizerInstance).Execute(currentPeriod, transferredBlocks, targetPeriod, Empty);
	/*for (std::vector<ProcessingOptimizer*>::iterator processingOptimizerIterator = processingOptimizerInstanceVector.begin(); processingOptimizerIterator != processingOptimizerInstanceVector.end(); processingOptimizerIterator++)
		(*processingOptimizerIterator)->Execute(currentPeriod, transferredBlocks, targetPeriod, Empty);*/
	//processingOptimizerInstance.Execute(currentPeriod, transferredBlocks, targetPeriod, Empty);
}
