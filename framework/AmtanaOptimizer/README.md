# AmtanaOptimizer

Optimizador de planificación minera en C++ con una herramienta Python para
evaluar sus planes en múltiples escenarios geológicos.

- [Optimizador C++: modelo, compilación y ejecución](README.txt).
- [Validador Python: instalación, PyCharm, Gurobi y reportes](validation/README.md).
- [Inversión, capacidades y costos por período: formato, ejemplo y pruebas](configuration/PARAMETROS_POR_PERIODO.md).

## Flujo de trabajo

1. Ejecutar el optimizador desde la raíz del repositorio con su configuración.
2. Conservar el plan que genera en `sensor/result.txt`.
3. Evaluar ese plan con el validador en el rango de escenarios deseado.

```text
python validation/ValidadorFramework2.py configuration/configuration.txt 0 19 --quiet
```

El ejemplo supone que existen los escenarios 00 a 19 y que el entorno Python
ya tiene instaladas las dependencias de `validation/requirements.txt` y una
licencia adecuada de Gurobi. La guía del validador incluye un ejemplo pequeño
autocontenido con resultados conocidos.

El optimizador C++ usa una matheurística para el procesamiento; el validador
resuelve un LP con Gurobi para cada período y escenario del plan fijo. Sus
resultados sirven como referencia de evaluación y no implican que ambos
métodos deban producir el mismo valor.
