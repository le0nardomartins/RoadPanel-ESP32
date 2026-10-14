// Incluído antes de tudo ao compilar o simulador com o MSVC (cl /FI): o firmware usa
// extensões do GCC que o MSVC não conhece.
#pragma once
#define __attribute__(x)
