## Macro

This folder contains `MicroKeys.py`, the script that's loaded and parsed by MicroKeys.  In a debug build, MicroKeys looks for this file in a directory called `macro` under the current working directory.  In a release build, it looks for it in the current working directory.

The script only needs itself.  The other files are a mock implementation of the library that's built
into MicroKeys for auto-complete and linters to have a target.  They should also be extended in the future to provide a small test environment, along with documentation.
