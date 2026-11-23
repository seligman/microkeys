#pragma once

// Python's headers ask for the debug build of Python when _DEBUG is set,
// which the embeddable package doesn't ship, so Debug builds of MicroKeys
// still compile against the release ABI.
#ifdef _DEBUG
#undef _DEBUG
#define MK_RESTORE_DEBUG
#endif
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#ifdef MK_RESTORE_DEBUG
#define _DEBUG
#undef MK_RESTORE_DEBUG
#endif
