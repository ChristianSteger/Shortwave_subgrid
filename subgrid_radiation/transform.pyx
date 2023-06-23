#cython: boundscheck=False, wraparound=False, cdivision=True, language_level=3

# Copyright (c) 2023 ETH Zurich, Christian R. Steger
# MIT License

# Load modules
import numpy as np
from libc.math cimport sin, cos, sqrt
from libc.math cimport M_PI
from cython.parallel import prange


# -----------------------------------------------------------------------------

def lonlat2ecef(lon, lat, h, ellps):
    """Coordinate transformation from lon/lat to ECEF.

    Transformation of geodetic longitude/latitude to earth-centered,
    earth-fixed (ECEF) coordinates.

    Parameters
    ----------
    lon : ndarray of double
        Array (with arbitrary dimensions) with geographic longitude [degree]
    lat : ndarray of double
        Array (with arbitrary dimensions) with geographic latitude [degree]
    h : ndarray of float
        Array (with arbitrary dimensions) with elevation above ellipsoid
        [metre]
    ellps : str
        Earth's surface approximation (sphere, GRS80 or WGS84)

    Returns
    -------
    x_ecef : ndarray of double
        Array (dimensions according to input) with ECEF x-coordinates [metre]
    y_ecef : ndarray of double
        Array (dimensions according to input) with ECEF y-coordinates [metre]
    z_ecef : ndarray of double
        Array (dimensions according to input) with ECEF z-coordinates [metre]
        """

    # Check arguments
    if (lon.shape != lat.shape) or (lat.shape != h.shape):
        raise ValueError("Inconsistent shapes / number of dimensions of "
                         + "input arrays")
    if ((lon.dtype != "float64") or (lat.dtype != "float64")
            or (h.dtype != "float32")):
        raise ValueError("Input array(s) has/have incorrect data type(s)")
    if ellps not in ("sphere", "GRS80", "WGS84"):
        raise ValueError("Unknown value for 'ellps'")

    # Wrapper for 1-dimensional function
    shp = lon.shape
    x_ecef, y_ecef, z_ecef = _lonlat2ecef_1d(lon.ravel(), lat.ravel(),
                                              h.ravel(), ellps)
    return x_ecef.reshape(shp), y_ecef.reshape(shp), z_ecef.reshape(shp)


def _lonlat2ecef_1d(double[:] lon, double[:] lat, float[:] h, ellps):
    """Coordinate transformation from lon/lat to ECEF (for 1-dimensional data).

    Sources
    -------
    - https://en.wikipedia.org/wiki/Geographic_coordinate_conversion
    - Geoid parameters r, a and f: PROJ"""

    cdef int len_0 = lon.shape[0]
    cdef int i
    cdef double r, f, a, b, e_2, n
    cdef double[:] x_ecef = np.empty(len_0, dtype=np.float64)
    cdef double[:] y_ecef = np.empty(len_0, dtype=np.float64)
    cdef double[:] z_ecef = np.empty(len_0, dtype=np.float64)

    # Spherical coordinates
    if ellps == "sphere":
        r = 6370997.0  # earth radius [m]
        for i in prange(len_0, nogil=True, schedule="static"):
            x_ecef[i] = (r + h[i]) * cos(deg2rad(lat[i])) \
                * cos(deg2rad(lon[i]))
            y_ecef[i] = (r + h[i]) * cos(deg2rad(lat[i])) \
                * sin(deg2rad(lon[i]))
            z_ecef[i] = (r + h[i]) * sin(deg2rad(lat[i]))
        
    # Elliptic (geodetic) coordinates
    else:
        a = 6378137.0  # equatorial radius (semi-major axis) [m]
        if ellps == "GRS80":
            f = (1.0 / 298.257222101)  # flattening [-]
        else:  # WGS84
            f = (1.0 / 298.257223563)  # flattening [-]
        b = a * (1.0 - f)  # polar radius (semi-minor axis) [m]
        e_2 = 1.0 - (b ** 2 / a ** 2)  # squared num. eccentricity [-]
        for i in prange(len_0, nogil=True, schedule="static"):
            n = a / sqrt(1.0 - e_2 * sin(deg2rad(lat[i])) ** 2)
            x_ecef[i] = (n + h[i]) * cos(deg2rad(lat[i])) \
                * cos(deg2rad(lon[i]))
            y_ecef[i] = (n + h[i]) * cos(deg2rad(lat[i])) \
                * sin(deg2rad(lon[i]))
            z_ecef[i] = (b ** 2 / a ** 2 * n + h[i]) \
                * sin(deg2rad(lat[i]))

    return np.asarray(x_ecef), np.asarray(y_ecef), np.asarray(z_ecef)


# -----------------------------------------------------------------------------

def ecef2enu(x_ecef, y_ecef, z_ecef, trans_ecef2enu):
    """Coordinate transformation from ECEF to ENU.

    Transformation of earth-centered, earth-fixed (ECEF) to local tangent
    plane (ENU) coordinates.

    Parameters
    ----------
    x_ecef : ndarray of double
        Array (with arbitrary dimensions) with ECEF x-coordinates [metre]
    y_ecef : ndarray of double
        Array (with arbitrary dimensions) with ECEF y-coordinates [metre]
    z_ecef : ndarray of double
        Array (with arbitrary dimensions) with ECEF z-coordinates [metre]
    trans_ecef2enu : class
        Instance of class `TransformerEcef2enu`

    Returns
    -------
    x_enu : ndarray of float
        Array (dimensions according to input) with ENU x-coordinates [metre]
    y_enu : ndarray of float
        Array (dimensions according to input) with ENU y-coordinates [metre]
    z_enu : ndarray of float
        Array (dimensions according to input) with ENU z-coordinates [metre]"""

    # Check arguments
    if (x_ecef.shape != y_ecef.shape) or (y_ecef.shape != z_ecef.shape):
        raise ValueError("Inconsistent shapes / number of dimensions of "
                         + "input arrays")
    if ((x_ecef.dtype != "float64") or (y_ecef.dtype != "float64")
            or (z_ecef.dtype != "float64")):
        raise ValueError("Input array(s) has/have incorrect data type(s)")
    if not isinstance(trans_ecef2enu, TransformerEcef2enu):
        raise ValueError("Last input argument must be instance of class "
                         + "'TransformerEcef2enu'")

    # Wrapper for 1-dimensional function
    shp = x_ecef.shape
    x_enu, y_enu, z_enu = _ecef2enu_1d(x_ecef.ravel(), y_ecef.ravel(),
                                              z_ecef.ravel(), trans_ecef2enu)
    return x_enu.reshape(shp), y_enu.reshape(shp), z_enu.reshape(shp)


def _ecef2enu_1d(double[:] x_ecef, double[:] y_ecef, double[:] z_ecef,
                trans_ecef2enu):
    """Coordinate transformation from ECEF to ENU (for 1-dimensional data).

    Sources
    -------
    - https://en.wikipedia.org/wiki/Geographic_coordinate_conversion"""

    cdef int len_0 = x_ecef.shape[0]
    cdef int i
    cdef double sin_lon, cos_lon, sin_lat, cos_lat
    cdef float[:] x_enu = np.empty(len_0, dtype=np.float32)
    cdef float[:] y_enu = np.empty(len_0, dtype=np.float32)
    cdef float[:] z_enu = np.empty(len_0, dtype=np.float32)
    cdef double x_ecef_or = trans_ecef2enu.x_ecef_or
    cdef double y_ecef_or = trans_ecef2enu.y_ecef_or
    cdef double z_ecef_or = trans_ecef2enu.z_ecef_or
    cdef double lon_or = trans_ecef2enu.lon_or
    cdef double lat_or = trans_ecef2enu.lat_or

    # Trigonometric functions
    sin_lon = sin(deg2rad(lon_or))
    cos_lon = cos(deg2rad(lon_or))
    sin_lat = sin(deg2rad(lat_or))
    cos_lat = cos(deg2rad(lat_or))

    # Coordinate transformation
    for i in prange(len_0, nogil=True, schedule="static"):
        x_enu[i] = (- sin_lon * (x_ecef[i] - x_ecef_or)
                    + cos_lon * (y_ecef[i] - y_ecef_or))
        y_enu[i] = (- sin_lat * cos_lon * (x_ecef[i] - x_ecef_or)
                    - sin_lat * sin_lon * (y_ecef[i] - y_ecef_or)
                    + cos_lat * (z_ecef[i] - z_ecef_or))
        z_enu[i] = (+ cos_lat * cos_lon * (x_ecef[i] - x_ecef_or)
                    + cos_lat * sin_lon * (y_ecef[i] - y_ecef_or)
                    + sin_lat * (z_ecef[i] - z_ecef_or))

    return np.asarray(x_enu), np.asarray(y_enu), np.asarray(z_enu)


# -----------------------------------------------------------------------------

class TransformerEcef2enu:
    """Class that stores attributes to transform from ECEF to ENU coordinates.

    Transformer class that stores attributes to convert between ECEF and ENU
    coordinates. The origin of the ENU coordinate system coincides with the
    surface of the sphere/ellipsoid.

    Parameters
    -------
    lon_or : double
        Longitude coordinate for origin of ENU coordinate system [degree]
    lat_or : double
        Latitude coordinate for origin of ENU coordinate system [degree]
    ellps : str
        Earth's surface approximation (sphere, GRS80 or WGS84)"""

    def __init__(self, lon_or, lat_or, ellps):
        if (lon_or < -180.0) or (lon_or > 180.0):
            raise ValueError("Value for 'lon_or' is outside of valid range")
        if (lat_or < -90.0) or (lat_or > 90.0):
            raise ValueError("Value for 'lat_or' is outside of valid range")
        self.lon_or = lon_or
        self.lat_or = lat_or

        if ellps == "sphere":
            r = 6370997.0  # earth radius [m]
            self.x_ecef_or = r * np.cos(np.deg2rad(self.lat_or)) \
                             * np.cos(np.deg2rad(self.lon_or))
            self.y_ecef_or = r * np.cos(np.deg2rad(self.lat_or)) \
                             * np.sin(np.deg2rad(self.lon_or))
            self.z_ecef_or = r * np.sin(np.deg2rad(self.lat_or))
        elif ellps in ("GRS80", "WGS84"):
            a = 6378137.0  # equatorial radius (semi-major axis) [m]
            if ellps == "GRS80":
                f = (1.0 / 298.257222101)  # flattening [-]
            else:  # WGS84
                f = (1.0 / 298.257223563)  # flattening [-]
            b = a * (1.0 - f)  # polar radius (semi-minor axis) [m]
            e_2 = 1.0 - (b ** 2 / a ** 2)  # squared num. eccentricity [-]
            n = a / np.sqrt(1.0 - e_2 * np.sin(np.deg2rad(self.lat_or)) ** 2)
            self.x_ecef_or = n * np.cos(np.deg2rad(self.lat_or)) \
                             * np.cos(np.deg2rad(self.lon_or))
            self.y_ecef_or = n * np.cos(np.deg2rad(self.lat_or)) \
                             * np.sin(np.deg2rad(self.lon_or))
            self.z_ecef_or = (b ** 2 / a ** 2 * n) \
                             * np.sin(np.deg2rad(self.lat_or))
        else:
            raise ValueError("Unknown value for 'ellps'")


# -----------------------------------------------------------------------------
# Auxiliary function(s)
# -----------------------------------------------------------------------------

cdef inline double deg2rad(double ang_in) nogil:
    """Convert degree to radian"""

    cdef double ang_out
    
    ang_out = ang_in * (M_PI / 180.0)
       
    return ang_out