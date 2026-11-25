/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Makes the Starboard C library importable as an extension module so that
 * setuptools can build it on every platform; the API itself is reached
 * through ctypes (starboard/_core.py). */
#include <Python.h>

static struct PyModuleDef sb_module = {
    PyModuleDef_HEAD_INIT, "_starboard",
    "Starboard C library (use the starboard package API).", -1, NULL,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit__starboard(void)
{
    return PyModule_Create(&sb_module);
}
