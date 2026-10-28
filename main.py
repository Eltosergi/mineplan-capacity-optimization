#!/usr/bin/env python3
"""Inicia el executer (que corre el framework) e imprime el valor final."""
import sys

import executer


def main() -> int:
    config_path = sys.argv[1] if len(sys.argv) > 1 else None

    try:
        npv, computation_time = executer.ejecutar(config_path)
    except (FileNotFoundError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    print(f"Optimized Solution NPV: {npv}")
    print(f"Computation Time: {computation_time:.0f} s")

    return 0


if __name__ == "__main__":
    sys.exit(main())
