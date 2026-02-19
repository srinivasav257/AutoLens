/**
 * @file CANInterface.cpp
 * @brief Translation unit for the ICANDriver abstract base class.
 *
 * Why this file exists
 * ────────────────────
 * ICANDriver is defined in CANInterface.h with the Q_OBJECT macro.
 * Q_OBJECT requires the Meta-Object Compiler (moc) to generate extra C++
 * code (signal bodies, metaObject(), qt_metacall(), etc.).
 *
 * AUTOMOC generates "moc_CANInterface.cpp" from the header, but it needs
 * a .cpp translation unit to attach that generated code to.  Without a
 * corresponding .cpp file, the linker cannot find the moc-generated symbols
 * — which causes the classic "unresolved external symbol qt_metacall" error.
 *
 * This stub file satisfies that requirement with zero overhead.
 */

#include "CANInterface.h"
