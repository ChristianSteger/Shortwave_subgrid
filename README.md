# Subgrid_radiation
Package to efficiently compute topographic parameters to consider the subgrid effects of complex terrain on surface radiation.
Correction factors for direct downward shortwave radiation can be computed for a two-dimensional array of subsolar positions or individual positions.

# Installation

Subgrid_radiation has been tested with **Python 3.9** (Mac OS X).
It is recommended to install dependencies via [Conda](https://docs.conda.io/en/latest/#).
Installation via **Conda** can be accomplished as follows for different platforms:

## Linux

Installation requires the [GNU Compiler Collection (GCC)](https://gcc.gnu.org). Create an appropriate Conda environment

```bash
conda create -n raytracing -c conda-forge embree3 tbb-devel cython numpy scipy xarray matplotlib cartopy netcdf4 cmcrameri skyfield skimage pyinterp pyproj
```

and **activate this environment**.
Then install the package [Utilities](https://github.com/ChristianSteger/Utilities) according to the provided instructions.
The Subgrid_radiation package can then be installed with:

```bash
git clone https://github.com/ChristianSteger/Subgrid_radiation.git
cd Subgrid_radiation
python -m pip install .
```

## Mac OS X

Subgrid_radiation is compiled with **Clang** under Mac OS X. As the Apple-provided **Clang** does not support OpenMP, an alternative **Clang** with OpenMP support has to be installed.
This can be done via Conda. Create an appropriate Conda environment

```bash
conda create -n raytracing -c conda-forge embree3 tbb-devel cython numpy scipy xarray matplotlib cartopy netcdf4 cmcrameri skyfield skimage pyinterp pyproj c-compiler openmp python=3.9
```

and **activate this environment**.
Then install the package [Utilities](https://github.com/ChristianSteger/Utilities) according to the provided instructions.
The Subgrid_radiation package can then be installed with:

```bash
git clone https://github.com/ChristianSteger/Subgrid_radiation.git
cd Subgrid_radiation
python -m pip install .
```

# Usage