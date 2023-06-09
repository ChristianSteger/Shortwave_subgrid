// Copyright (c) 2023 ETH Zurich, Christian R. Steger
// MIT License

#include "sun_position_comp.h"
#include <cstdio>
#include <embree3/rtcore.h>
#include <stdio.h>
#include <math.h>
#include <limits>
#include <stdio.h>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <string.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <sstream>
#include <iomanip>

using namespace std;
using namespace shapes;

//#############################################################################
// Auxiliary functions
//#############################################################################

// ----------------------------------------------------------------------------
// Unit conversion
// ----------------------------------------------------------------------------

// Convert degree to radian
inline double deg2rad(double ang) {
    /* Parameters
       ----------
       ang: angle [degree]

       Returns
       ----------
       ang: angle [radian]
    */
    return ((ang / 180.0) * M_PI);
}

// Convert radian to degree
inline double rad2deg(double ang) {
    /* Parameters
       ----------
       ang: angle [radian]

       Returns
       ----------
       ang: angle [degree]
    */
    return ((ang / M_PI) * 180.0);
}

// Convert from Kelvin to degree Celsius
inline double K2degC(double temp) {
    /* Parameters
       ----------
       temp: temperature [Kelvin]

       Returns
       ----------
       temp: temperature [degree Celsius]*/
    return (temp - 273.15);
}

// ----------------------------------------------------------------------------
// Compute linear array index from multidimensional subscripts
// ----------------------------------------------------------------------------

// Linear index from subscripts (2D-array)
inline size_t lin_ind_2d(size_t dim_1, size_t ind_0, size_t ind_1) {
    /* Parameters
       ----------
       dim_1: second dimension length of two-dimensional array [-]
       ind_0: first array indices [-]
       ind_1: second array indices [-]

       Returns
       ----------
       ind_lin: linear index of array [-]
    */
    return (ind_0 * dim_1 + ind_1);
}

// ----------------------------------------------------------------------------
// Vector and matrix operations
// ----------------------------------------------------------------------------

// Unit vector
inline void vec_unit(double &v_x, double &v_y, double &v_z) {
    /* Parameters
       ----------
       v_x: x-component of vector [arbitrary]
       v_y: y-component of vector [arbitrary]
       v_z: z-component of vector [arbitrary]
    */
    double mag = sqrt(v_x * v_x + v_y * v_y + v_z * v_z);
    v_x = v_x / mag;
    v_y = v_y / mag;
    v_z = v_z / mag;
}

// Cross product
inline void cross_prod(double a_x, double a_y, double a_z,
    double b_x, double b_y, double b_z,
    double &c_x, double &c_y, double &c_z) {
    /* Parameters
       ----------
       a_x: x-component of vector a [arbitrary]
       a_y: y-component of vector a [arbitrary]
       a_z: z-component of vector a [arbitrary]
       b_x: x-component of vector b [arbitrary]
       b_y: y-component of vector b [arbitrary]
       b_z: z-component of vector b [arbitrary]
       c_x: x-component of vector c [arbitrary]
       c_y: y-component of vector c [arbitrary]
       c_z: z-component of vector c [arbitrary]
    */
    c_x = a_y * b_z - a_z * b_y;
    c_y = a_z * b_x - a_x * b_z;
    c_z = a_x * b_y - a_y * b_x;
}

// Vector rotation (according to Rodrigues' rotation formula)
inline void vec_rot(double k_x, double k_y, double k_z, double theta,
    double &v_x, double &v_y, double &v_z) {
    /* Parameters
       ----------
       k_x: x-component of unit vector perpendicular to rotation plane [-]
       k_y: y-component of unit vector perpendicular to rotation plane [-]
       k_z: z-component of unit vector perpendicular to rotation plane [-]
       theta: rotation angle [radian]
       v_x: x-component of rotated vector [-]
       v_y: y-component of rotated vector [-]
       v_z: z-component of rotated vector [-]
    */
    double cos_theta = cos(theta);
    double sin_theta = sin(theta);
    double part = (k_x * v_x + k_y * v_y + k_z * v_z) * (1.0 - cos_theta);
    double v_x_rot = v_x * cos_theta + (k_y * v_z - k_z * v_y) * sin_theta
        + k_x * part;
    double v_y_rot = v_y * cos_theta + (k_z * v_x - k_x * v_z) * sin_theta
        + k_y * part;
    double v_z_rot = v_z * cos_theta + (k_x * v_y - k_y * v_x) * sin_theta
        + k_z * part;
    v_x = v_x_rot;
    v_y = v_y_rot;
    v_z = v_z_rot;
}

// ----------------------------------------------------------------------------
// Triangle operations
// ----------------------------------------------------------------------------

// Triangle surface normal and area
inline void triangle_normal_area(
    double &vert_0_x, double &vert_0_y, double &vert_0_z,
    double &vert_1_x, double &vert_1_y, double &vert_1_z,
    double &vert_2_x, double &vert_2_y, double &vert_2_z,
    double &norm_x, double &norm_y, double &norm_z,
    double &area) {
    /* Parameters
       ----------
       vert_0_x: x-component of first triangle vertices [m]
       vert_0_y: y-component of first triangle vertices [m]
       vert_0_z: z-component of first triangle vertices [m]
       vert_1_x: x-component of second triangle vertices [m]
       vert_1_y: y-component of second triangle vertices [m]
       vert_1_z: z-component of second triangle vertices [m]
       vert_2_x: x-component of third triangle vertices [m]
       vert_2_y: y-component of third triangle vertices [m]
       vert_2_z: z-component of third triangle vertices [m]
       norm_x: x-component of triangle surface normal [-]
       norm_y: y-component of triangle surface normal [-]
       norm_z: z-component of triangle surface normal [-]
       area: area of triangle [m2]
    */
    double a_x = vert_2_x - vert_1_x;
    double a_y = vert_2_y - vert_1_y;
    double a_z = vert_2_z - vert_1_z;
    double b_x = vert_0_x - vert_1_x;
    double b_y = vert_0_y - vert_1_y;
    double b_z = vert_0_z - vert_1_z;

    norm_x = a_y * b_z - a_z * b_y;
    norm_y = a_z * b_x - a_x * b_z;
    norm_z = a_x * b_y - a_y * b_x;

    double mag = sqrt(norm_x * norm_x + norm_y * norm_y + norm_z * norm_z);
    norm_x = norm_x / mag;
    norm_y = norm_y / mag;
    norm_z = norm_z / mag;

    area = mag / 2.0;
}

// Triangle centroid
inline void triangle_centroid(
    double &vert_0_x, double &vert_0_y, double &vert_0_z,
    double &vert_1_x, double &vert_1_y, double &vert_1_z,
    double &vert_2_x, double &vert_2_y, double &vert_2_z,
    double &cent_x, double &cent_y, double &cent_z) {
    /* Parameters
       ----------
       vert_0_x: x-component of first triangle vertices [m]
       vert_0_y: y-component of first triangle vertices [m]
       vert_0_z: z-component of first triangle vertices [m]
       vert_1_x: x-component of second triangle vertices [m]
       vert_1_y: y-component of second triangle vertices [m]
       vert_1_z: z-component of second triangle vertices [m]
       vert_2_x: x-component of third triangle vertices [m]
       vert_2_y: y-component of third triangle vertices [m]
       vert_2_z: z-component of third triangle vertices [m]
       cent_x: x-component of triangle centroid [-]
       cent_y: y-component of triangle centroid [-]
        cent_z: z-component of triangle centroid [-]
    */
    cent_x = (vert_0_x + vert_1_x + vert_2_x) / 3.0;
    cent_y = (vert_0_y + vert_1_y + vert_2_y) / 3.0;
    cent_z = (vert_0_z + vert_1_z + vert_2_z) / 3.0;
}

// Vertices of lower left triangle (within pixel)
inline void triangle_vert_ll(size_t dim_1, size_t ind_0, size_t ind_1,
    size_t &ind_tri_0, size_t &ind_tri_1, size_t &ind_tri_2) {
    /* Parameters
       ----------

    */
    ind_tri_0 = (ind_0 * dim_1 + ind_1) * 3;
    ind_tri_1 = (ind_0 * dim_1 + ind_1 + 1) * 3;
    ind_tri_2 = ((ind_0 + 1) * dim_1 + ind_1) * 3;
}

// Vertices of upper right triangle (within pixel)
inline void triangle_vert_ur(size_t dim_1, size_t ind_0, size_t ind_1,
    size_t &ind_tri_0, size_t &ind_tri_1, size_t &ind_tri_2) {
    /* Parameters
       ----------

    */
    ind_tri_0 = (ind_0 * dim_1 + ind_1 + 1) * 3;
    ind_tri_1 = ((ind_0 + 1) * dim_1 + ind_1 + 1) * 3;
    ind_tri_2 = ((ind_0 + 1) * dim_1 + ind_1) * 3;

}

// Store above two functions in array
void (*func_ptr[2])(size_t dim_1, size_t ind_0, size_t ind_1,
    size_t &ind_tri_0, size_t &ind_tri_1, size_t &ind_tri_2)
    = {triangle_vert_ll, triangle_vert_ur};

// ----------------------------------------------------------------------------
// Atmospheric refraction
// ----------------------------------------------------------------------------

// Estimate atmospheric refraction
inline double atmos_refrac(double elev_ang_true, double temp,
    double pressure) {
    /* Parameters
       ----------
       elev_ang_true: true solar elevation angle [degree]
       temp: temperature [degree Celsius]
       pressure: atmospheric pressure [kPa]

       Returns
       ----------
       refrac_cor: refraction correction [degree]

       Reference
       ----------
       - Saemundsson, P. (1986). "Astronomical Refraction". Sky and Telescope.
         72: 70
       - Meeus, J. (1998): Astronomical Algorithm - Second edition, p. 106*/
    double lower = -1.0;
    double upper = 90.0;
    elev_ang_true = std::max(lower, std::min(elev_ang_true, upper));
    double refrac_cor = (1.02 / tan(deg2rad(elev_ang_true + 10.3
        / (elev_ang_true + 5.11))));
    refrac_cor += 0.0019279;  // set R = 0.0 for h = 90.0 degree
    refrac_cor *= (pressure / 101.0) * (283.0 / (273.0 + temp));
    return refrac_cor * (1.0 / 60.0);
}

//#############################################################################
// Miscellaneous
//#############################################################################

// Namespace
#if defined(RTC_NAMESPACE_USE)
    RTC_NAMESPACE_USE
#endif

// Error function
void errorFunction(void* userPtr, enum RTCError error, const char* str) {
    printf("error %d: %s\n", error, str);
}

// Initialisation of device and registration of error handler
RTCDevice initializeDevice() {
    RTCDevice device = rtcNewDevice(NULL);
    if (!device) {
        printf("error %d: cannot create device\n", rtcGetDeviceError(NULL));
    }
    rtcSetDeviceErrorFunction(device, errorFunction, NULL);
    return device;
}

//#############################################################################
// Create scene from geometries
//#############################################################################

// Structures for triangle and quad
struct Triangle { int v0, v1, v2; };
struct Quad { int v0, v1, v2, v3; };
// -> above structures must contain 32-bit integers (-> Embree documentation).
//    Theoretically, these integers should be unsigned but the binary
//    representation until 2'147'483'647 is identical between signed/unsigned
//    integer.

// Initialise scene
RTCScene initializeScene(RTCDevice device, float* vert_grid,
    int dem_dim_0, int dem_dim_1, char* geom_type) {

    RTCScene scene = rtcNewScene(device);
    rtcSetSceneFlags(scene, RTC_SCENE_FLAG_ROBUST);

    int num_vert = (dem_dim_0 * dem_dim_1);
    printf("DEM dimensions: (%d, %d) \n", dem_dim_0, dem_dim_1);
    printf("Number of vertices: %d \n", num_vert);

    RTCGeometryType rtc_geom_type;
    if (strcmp(geom_type, "triangle") == 0) {
        rtc_geom_type = RTC_GEOMETRY_TYPE_TRIANGLE;
    } else if (strcmp(geom_type, "quad") == 0) {
        rtc_geom_type = RTC_GEOMETRY_TYPE_QUAD;
    } else {
        rtc_geom_type = RTC_GEOMETRY_TYPE_GRID;
    }

    RTCGeometry geom = rtcNewGeometry(device, rtc_geom_type);
    rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0,
        RTC_FORMAT_FLOAT3, vert_grid, 0, 3*sizeof(float), num_vert);

    //-------------------------------------------------------------------------
    // Triangle
    //-------------------------------------------------------------------------
    if (strcmp(geom_type, "triangle") == 0) {
        cout << "Selected geometry type: triangle" << endl;
        int num_tri = ((dem_dim_0 - 1) * (dem_dim_1 - 1)) * 2;
        printf("Number of triangles: %d \n", num_tri);
        Triangle* triangles = (Triangle*) rtcSetNewGeometryBuffer(geom,
            RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, sizeof(Triangle),
            num_tri);
        int n = 0;
        for (int i = 0; i < (dem_dim_0 - 1); i++) {
            for (int j = 0; j < (dem_dim_1 - 1); j++) {
                triangles[n].v0 = (i * dem_dim_1) + j;
                triangles[n].v1 = (i * dem_dim_1) + j + 1;
                triangles[n].v2 = ((i + 1) * dem_dim_1) + j;
                n++;
                triangles[n].v0 = (i * dem_dim_1) + j + 1;
                triangles[n].v1 = ((i + 1) * dem_dim_1) + j + 1;
                triangles[n].v2 = ((i + 1) * dem_dim_1) + j;
                n++;
            }
        }
    //-------------------------------------------------------------------------
    // Quad
    //-------------------------------------------------------------------------
    } else if (strcmp(geom_type, "quad") == 0) {
        cout << "Selected geometry type: quad" << endl;
        int num_quad = ((dem_dim_0 - 1) * (dem_dim_1 - 1));
        printf("Number of quads: %d \n", num_quad);
        Quad* quads = (Quad*) rtcSetNewGeometryBuffer(geom,
            RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT4, sizeof(Quad),
            num_quad);
        int n = 0;
        for (int i = 0; i < (dem_dim_0 - 1); i++) {
            for (int j = 0; j < (dem_dim_1 - 1); j++) {
                //  identical to grid scene (-> otherwise reverse v0, v1, ...)
                quads[n].v0 = (i * dem_dim_1) + j;
                quads[n].v1 = (i * dem_dim_1) + j + 1;
                quads[n].v2 = ((i + 1) * dem_dim_1) + j + 1;
                quads[n].v3 = ((i + 1) * dem_dim_1) + j;
                n++;
            }
        }
    //-------------------------------------------------------------------------
    // Grid
    //-------------------------------------------------------------------------
    } else {
        cout << "Selected geometry type: grid" << endl;
        RTCGrid* grid = (RTCGrid*)rtcSetNewGeometryBuffer(geom,
            RTC_BUFFER_TYPE_GRID, 0, RTC_FORMAT_GRID, sizeof(RTCGrid), 1);
        grid[0].startVertexID = 0;
        grid[0].stride        = dem_dim_1;
        grid[0].width         = dem_dim_1;
        grid[0].height        = dem_dim_0;
    }
    //-------------------------------------------------------------------------

    auto start = std::chrono::high_resolution_clock::now();

    // Commit geometry
    rtcCommitGeometry(geom);

    rtcAttachGeometry(scene, geom);
    rtcReleaseGeometry(geom);

    //-------------------------------------------------------------------------

    // Commit scene
    rtcCommitScene(scene);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time = end - start;
    cout << "BVH build time: " << time.count() << " s" << endl;

    return scene;

}

//#############################################################################
// Initialise terrain
//#############################################################################

CppTerrain::CppTerrain() {

    device = initializeDevice();

}

CppTerrain::~CppTerrain() {

    // Release resources allocated through Embree
    rtcReleaseScene(scene);
    rtcReleaseDevice(device);

}

void CppTerrain::initialise(
    float* vert_grid,
    int dem_dim_0, int dem_dim_1,
    float* vert_grid_in,
    int dem_dim_in_0, int dem_dim_in_1,
    int pixel_per_gc,
    int offset_gc,
	unsigned char* mask,
    double dist_search,
    char* geom_type,
    double sw_dir_cor_max,
    double ang_max) {

    vert_grid_cl = vert_grid;
    dem_dim_0_cl = dem_dim_0;
    dem_dim_1_cl = dem_dim_1;
    vert_grid_in_cl = vert_grid_in;
    dem_dim_in_0_cl = dem_dim_in_0;
    dem_dim_in_1_cl = dem_dim_in_1;
    pixel_per_gc_cl = pixel_per_gc;
    offset_gc_cl = offset_gc;
    mask_cl = mask;
    sw_dir_cor_max_cl = sw_dir_cor_max;
    ang_max_cl = ang_max;

    // Hard-coded settings
    ray_org_elev_cl = 0.1;
    // value to elevate ray origin (-> avoids potential issue with numerical
    // imprecision / truncation) [m]

    // Number of grid cells
    num_gc_y_cl = (dem_dim_in_0 - 1) / pixel_per_gc;
    num_gc_x_cl = (dem_dim_in_1 - 1) / pixel_per_gc;

    // Number of triangles
    num_tri_cl = (dem_dim_in_0 - 1) * (dem_dim_in_1 - 1) * 2;
    cout << "Number of triangles: " << num_tri_cl << endl;

    // Unit conversion(s)
    dot_prod_min_cl = cos(deg2rad(ang_max));
    dist_search_cl = dist_search * 1000.0;  // [kilometre] to [metre]
    cout << "Search distance: " << dist_search_cl << " m" << endl;

    cout << "ang_max: " << ang_max << " degree" << endl;
    cout << "sw_dir_cor_max: " << sw_dir_cor_max  << endl;

    auto start_ini = std::chrono::high_resolution_clock::now();

    scene = initializeScene(device, vert_grid, dem_dim_0, dem_dim_1,
        geom_type);

    auto end_ini = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time = end_ini - start_ini;
    cout << "Total initialisation time: " << time.count() << " s" << endl;

}

//#############################################################################
// Compute correction factors
//#############################################################################

void CppTerrain::sw_dir_cor(double* sun_pos, float* sw_dir_cor,
    int refrac_cor) {
 
    auto start_ray = std::chrono::high_resolution_clock::now();
    size_t num_rays = 0;

	// Parameters for reference atmosphere
	double temperature_ref = 283.15;  // reference temperature at sea level [K]
	double pressure_ref = 101.0;  // reference pressure at sea level [kPa]
	double lapse_rate = 0.0065;  // temperature lapse rate [K m-1]
	double g = 9.81;  // acceleration due to gravity at sea level [m s-2]
	double R_d = 287.0;  // gas constant for dry air [J K-1 kg-1]
	double exp_baro = (g / (R_d * lapse_rate));
	// exponent for barometric formula

    num_rays += tbb::parallel_reduce(
        tbb::blocked_range<size_t>(0, num_gc_y_cl), 0.0,
        [&](tbb::blocked_range<size_t> r, size_t num_rays) {  // parallel

    // Loop through 2D-field of grid cells
    //for (size_t i = 0; i < num_gc_y_cl; i++) {  // serial
    for (size_t i=r.begin(); i<r.end(); ++i) {  // parallel
        for (size_t j = 0; j < num_gc_x_cl; j++) {

            size_t lin_ind_gc = lin_ind_2d(num_gc_x_cl, i, j);
            if (mask_cl[lin_ind_gc] == 1) {

            // Loop through 2D-field of DEM pixels
            for (size_t k = (i * pixel_per_gc_cl);
                k < ((i * pixel_per_gc_cl) + pixel_per_gc_cl); k++) {
                for (size_t m = (j * pixel_per_gc_cl);
                    m < ((j * pixel_per_gc_cl) + pixel_per_gc_cl); m++) {

                    // Loop through two triangles per pixel
                    for (size_t n = 0; n < 2; n++) {

                        //-----------------------------------------------------
                        // Tilted triangle
                        //-----------------------------------------------------

                        size_t ind_tri_0, ind_tri_1, ind_tri_2;
                        func_ptr[n](dem_dim_1_cl,
                            k + (pixel_per_gc_cl * offset_gc_cl),
                            m + (pixel_per_gc_cl * offset_gc_cl),
                            ind_tri_0, ind_tri_1, ind_tri_2);

                        double vert_0_x = (double)vert_grid_cl[ind_tri_0];
                        double vert_0_y = (double)vert_grid_cl[ind_tri_0 + 1];
                        double vert_0_z = (double)vert_grid_cl[ind_tri_0 + 2];
                        double vert_1_x = (double)vert_grid_cl[ind_tri_1];
                        double vert_1_y = (double)vert_grid_cl[ind_tri_1 + 1];
                        double vert_1_z = (double)vert_grid_cl[ind_tri_1 + 2];
                        double vert_2_x = (double)vert_grid_cl[ind_tri_2];
                        double vert_2_y = (double)vert_grid_cl[ind_tri_2 + 1];
                        double vert_2_z = (double)vert_grid_cl[ind_tri_2 + 2];

                        double cent_x, cent_y, cent_z;
                        triangle_centroid(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            cent_x, cent_y, cent_z);

                        double norm_tilt_x, norm_tilt_y, norm_tilt_z,
                            area_tilt;
                        triangle_normal_area(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            norm_tilt_x, norm_tilt_y, norm_tilt_z,
                            area_tilt);

                        // Ray origin
                        double ray_org_x = (cent_x
                            + norm_tilt_x * ray_org_elev_cl);
                        double ray_org_y = (cent_y
                            + norm_tilt_y * ray_org_elev_cl);
                        double ray_org_z = (cent_z
                            + norm_tilt_z * ray_org_elev_cl);

                        //-----------------------------------------------------
                        // Horizontal triangle
                        //-----------------------------------------------------

                        func_ptr[n](dem_dim_in_1_cl, k, m,
                            ind_tri_0, ind_tri_1, ind_tri_2);

                        vert_0_x = (double)vert_grid_in_cl[ind_tri_0];
                        vert_0_y = (double)vert_grid_in_cl[ind_tri_0 + 1];
                        vert_0_z = (double)vert_grid_in_cl[ind_tri_0 + 2];
                        vert_1_x = (double)vert_grid_in_cl[ind_tri_1];
                        vert_1_y = (double)vert_grid_in_cl[ind_tri_1 + 1];
                        vert_1_z = (double)vert_grid_in_cl[ind_tri_1 + 2];
                        vert_2_x = (double)vert_grid_in_cl[ind_tri_2];
                        vert_2_y = (double)vert_grid_in_cl[ind_tri_2 + 1];
                        vert_2_z = (double)vert_grid_in_cl[ind_tri_2 + 2];

                        double norm_hori_x, norm_hori_y, norm_hori_z,
                            area_hori;
                        triangle_normal_area(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            norm_hori_x, norm_hori_y, norm_hori_z,
                            area_hori);

                        double surf_enl_fac = area_tilt / area_hori;

                        //-----------------------------------------------------
                        // Compute correction factor
                        //-----------------------------------------------------

                        // Compute sun unit vector
                        double sun_x = (sun_pos[0] - ray_org_x);
                        double sun_y = (sun_pos[1] - ray_org_y);
                        double sun_z = (sun_pos[2] - ray_org_z);
                        vec_unit(sun_x, sun_y, sun_z);

                        // Consider atmospheric refraction (optional)
                        double dot_prod_hs = (norm_hori_x * sun_x
                            + norm_hori_y * sun_y
                            + norm_hori_z * sun_z);
                        if (refrac_cor == 1) {

                            // Compute elevation (distance between centroid
                            // of DEM triangle and 'base triangle')
                            double cent_base_x, cent_base_y, cent_base_z;
                            triangle_centroid(vert_0_x, vert_0_y, vert_0_z,
                                vert_1_x, vert_1_y, vert_1_z,
                                vert_2_x, vert_2_y, vert_2_z,
                                cent_base_x, cent_base_y, cent_base_z);
                            double elevation
                                = sqrt(pow(cent_x - cent_base_x, 2)
                                + pow(cent_y - cent_base_y, 2)
                                + pow(cent_z - cent_base_z, 2));

                            // Update sun position
                            double elev_ang_true = 90.0
                                - rad2deg(acos(dot_prod_hs));
                            double temperature = temperature_ref
                                - (lapse_rate * elevation);
                            double pressure = pressure_ref
                                * pow((temperature / temperature_ref),
                                exp_baro);
                            double refrac_cor = atmos_refrac(elev_ang_true,
                                K2degC(temperature), pressure);
                            double k_x, k_y, k_z;
                            cross_prod(sun_x, sun_y, sun_z,
                                norm_hori_x, norm_hori_y, norm_hori_z,
                                k_x, k_y, k_z);
                            vec_unit(k_x, k_y, k_z);
                            vec_rot(k_x, k_y, k_z, deg2rad(refrac_cor),
                                sun_x, sun_y, sun_z);
                            dot_prod_hs = (norm_hori_x * sun_x
                                + norm_hori_y * sun_y   + norm_hori_z * sun_z);

                        }

                        // Check for self-shadowing (Earth)
                        if (dot_prod_hs <= dot_prod_min_cl) {
                            continue;   // sw_dir_cor += 0.0
                        }

                        // Check for self-shadowing (triangle)
                        double dot_prod_ts = norm_tilt_x * sun_x
                            + norm_tilt_y * sun_y
                            + norm_tilt_z * sun_z;
                        if (dot_prod_ts <= 0.0) {
                            continue;  // sw_dir_cor += 0.0
                        }

                        // Intersect context
                        struct RTCIntersectContext context;
                        rtcInitIntersectContext(&context);

                        // Ray structure
                        struct RTCRay ray;
                        ray.org_x = (float)ray_org_x;
                        ray.org_y = (float)ray_org_y;
                        ray.org_z = (float)ray_org_z;
                        ray.dir_x = (float)sun_x;
                        ray.dir_y = (float)sun_y;
                        ray.dir_z = (float)sun_z;
                        ray.tnear = 0.0;
                        ray.tfar = (float)dist_search_cl;
                        // std::numeric_limits<float>::infinity();

                        // Intersect ray with scene
                        rtcOccluded1(scene, &context, &ray);
                        if (ray.tfar > 0.0) {
                            // no intersection -> 'tfar' is not updated;
                            // otherwise 'tfar' = -inf
                            sw_dir_cor[lin_ind_gc] =
                                sw_dir_cor[lin_ind_gc]
                                + (float)(std::min(((dot_prod_ts
                                / dot_prod_hs)
                                * surf_enl_fac), sw_dir_cor_max_cl));
                        }  // else: sw_dir_cor += 0.0
                        num_rays += 1;

                    }

                }
            }

            } else {

                sw_dir_cor[lin_ind_gc] = NAN;

            }

        }
    }

    return num_rays;  // parallel
    }, std::plus<size_t>());  // parallel

    auto end_ray = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time_ray = (end_ray - start_ray);
    cout << "Ray tracing time: " << time_ray.count() << " s" << endl;
    cout << "Number of rays shot: " << num_rays << endl;
    double frac_ray = (double)num_rays / (double)num_tri_cl;
    cout << "Fraction of rays required: " << frac_ray << endl;

    // Divide accumulated values by number of triangles within grid cell
    float num_tri_per_gc = pixel_per_gc_cl * pixel_per_gc_cl * 2.0;
    size_t num_elem = (num_gc_y_cl * num_gc_x_cl);
    for (size_t i = 0; i < num_elem; i++) {
        sw_dir_cor[i] /= num_tri_per_gc;
    }

}

//#############################################################################
// Compute correction factors with coherent rays
//#############################################################################

void CppTerrain::sw_dir_cor_coherent(double* sun_pos, float* sw_dir_cor) {

    auto start_ray = std::chrono::high_resolution_clock::now();
    size_t num_rays = 0;

    int num_tri_per_gc = pixel_per_gc_cl * pixel_per_gc_cl * 2;
    // number of triangles per grid cell

    num_rays += tbb::parallel_reduce(
        tbb::blocked_range<size_t>(0, num_gc_y_cl), 0.0,
        [&](tbb::blocked_range<size_t> r, size_t num_rays) {  // parallel

    // Loop through 2D-field of grid cells
    //for (size_t i = 0; i < num_gc_y_cl; i++) {  // serial
    for (size_t i=r.begin(); i<r.end(); ++i) {  // parallel
        for (size_t j = 0; j < num_gc_x_cl; j++) {

            size_t lin_ind_gc = lin_ind_2d(num_gc_x_cl, i, j);
            if (mask_cl[lin_ind_gc] == 1) {

            RTCRay rays[num_tri_per_gc];
            float* sw_dir_cor_ray = new float[num_tri_per_gc];
            unsigned int num_rays_gc = 0;

            // Loop through pixels within grid cell
            for (size_t k = (i * pixel_per_gc_cl);
                k < ((i * pixel_per_gc_cl) + pixel_per_gc_cl); k++) {
                for (size_t m = (j * pixel_per_gc_cl);
                    m < ((j * pixel_per_gc_cl) + pixel_per_gc_cl); m++) {

                    // Loop through two triangles per pixel
                    for (size_t n = 0; n < 2; n++) {

                        //-----------------------------------------------------
                        // Tilted triangle
                        //-----------------------------------------------------

                        size_t ind_tri_0, ind_tri_1, ind_tri_2;
                        func_ptr[n](dem_dim_1_cl,
                            k + (pixel_per_gc_cl * offset_gc_cl),
                            m + (pixel_per_gc_cl * offset_gc_cl),
                            ind_tri_0, ind_tri_1, ind_tri_2);

                        double vert_0_x = (double)vert_grid_cl[ind_tri_0];
                        double vert_0_y = (double)vert_grid_cl[ind_tri_0 + 1];
                        double vert_0_z = (double)vert_grid_cl[ind_tri_0 + 2];
                        double vert_1_x = (double)vert_grid_cl[ind_tri_1];
                        double vert_1_y = (double)vert_grid_cl[ind_tri_1 + 1];
                        double vert_1_z = (double)vert_grid_cl[ind_tri_1 + 2];
                        double vert_2_x = (double)vert_grid_cl[ind_tri_2];
                        double vert_2_y = (double)vert_grid_cl[ind_tri_2 + 1];
                        double vert_2_z = (double)vert_grid_cl[ind_tri_2 + 2];

                        double cent_x, cent_y, cent_z;
                        triangle_centroid(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            cent_x, cent_y, cent_z);

                        double norm_tilt_x, norm_tilt_y, norm_tilt_z, area_tilt;
                        triangle_normal_area(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            norm_tilt_x, norm_tilt_y, norm_tilt_z,
                            area_tilt);

                        // Ray origin
                        double ray_org_x = (cent_x
                            + norm_tilt_x * ray_org_elev_cl);
                        double ray_org_y = (cent_y
                            + norm_tilt_y * ray_org_elev_cl);
                        double ray_org_z = (cent_z
                            + norm_tilt_z * ray_org_elev_cl);

                        //-----------------------------------------------------
                        // Horizontal triangle
                        //-----------------------------------------------------

                        func_ptr[n](dem_dim_in_1_cl, k, m,
                            ind_tri_0, ind_tri_1, ind_tri_2);

                        vert_0_x = (double)vert_grid_in_cl[ind_tri_0];
                        vert_0_y = (double)vert_grid_in_cl[ind_tri_0 + 1];
                        vert_0_z = (double)vert_grid_in_cl[ind_tri_0 + 2];
                        vert_1_x = (double)vert_grid_in_cl[ind_tri_1];
                        vert_1_y = (double)vert_grid_in_cl[ind_tri_1 + 1];
                        vert_1_z = (double)vert_grid_in_cl[ind_tri_1 + 2];
                        vert_2_x = (double)vert_grid_in_cl[ind_tri_2];
                        vert_2_y = (double)vert_grid_in_cl[ind_tri_2 + 1];
                        vert_2_z = (double)vert_grid_in_cl[ind_tri_2 + 2];

                        double norm_hori_x, norm_hori_y, norm_hori_z, area_hori;
                        triangle_normal_area(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            norm_hori_x, norm_hori_y, norm_hori_z,
                            area_hori);

                        double surf_enl_fac = area_tilt / area_hori;

                        //-----------------------------------------------------
                        // Compute correction factor
                        //-----------------------------------------------------

                        // Compute sun unit vector
                        double sun_x = (sun_pos[0] - ray_org_x);
                        double sun_y = (sun_pos[1] - ray_org_y);
                        double sun_z = (sun_pos[2] - ray_org_z);
                        vec_unit(sun_x, sun_y, sun_z);

                        // Check for self-shadowing (Earth)
                        double dot_prod_hs = (norm_hori_x * sun_x
                            + norm_hori_y * sun_y
                            + norm_hori_z * sun_z);
                        if (dot_prod_hs <= dot_prod_min_cl) {
                            continue;  // sw_dir_cor += 0.0
                        }

                        // Check for self-shadowing (triangle)
                        double dot_prod_ts = norm_tilt_x * sun_x
                            + norm_tilt_y * sun_y
                            + norm_tilt_z * sun_z;
                        if (dot_prod_ts <= 0.0) {
                            continue;  // sw_dir_cor += 0.0
                        }

                        // Add ray
                        rays[num_rays_gc].org_x = (float)ray_org_x;
                        rays[num_rays_gc].org_y = (float)ray_org_y;
                        rays[num_rays_gc].org_z = (float)ray_org_z;
                        rays[num_rays_gc].dir_x = (float)sun_x;
                        rays[num_rays_gc].dir_y = (float)sun_y;
                        rays[num_rays_gc].dir_z = (float)sun_z;
                        rays[num_rays_gc].tnear = 0.0;
                        rays[num_rays_gc].tfar = (float)dist_search_cl;
                        // std::numeric_limits<float>::infinity();
                        rays[num_rays_gc].id = num_rays_gc;

                        sw_dir_cor_ray[num_rays_gc] =
                            (float)(std::min(((dot_prod_ts / dot_prod_hs)
                            * surf_enl_fac), sw_dir_cor_max_cl));
                        num_rays_gc = num_rays_gc + 1;

                    }

                }
            }

            struct RTCIntersectContext context;
            rtcInitIntersectContext(&context);
            context.flags = RTC_INTERSECT_CONTEXT_FLAG_COHERENT;

            // Intersect rays with scene
            rtcOccluded1M(scene, &context,(RTCRay*)rays, num_rays_gc,
                sizeof(RTCRay));
            num_rays += num_rays_gc;

            float sw_dir_cor_agg = 0.0;
            for (size_t k = 0; k < num_rays_gc; k++) {
                if (rays[k].tfar > 0.0) {
                    // no intersection -> 'tfar' is not updated;
                    // otherwise 'tfar' = -inf
                    sw_dir_cor_agg = sw_dir_cor_agg
                        + sw_dir_cor_ray[rays[k].id];
                }  // else: sw_dir_cor += 0.0
            }

            delete[] sw_dir_cor_ray;

            sw_dir_cor[lin_ind_gc]
                = sw_dir_cor_agg / (float)num_tri_per_gc;

            } else {

                sw_dir_cor[lin_ind_gc] = NAN;

            }

        }
    }

    return num_rays;  // parallel
    }, std::plus<size_t>());  // parallel

    auto end_ray = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time_ray = (end_ray - start_ray);
    cout << "Ray tracing time: " << time_ray.count() << " s" << endl;
    cout << "Number of rays shot: " << num_rays << endl;
    double frac_ray = (double)num_rays / (double)num_tri_cl;
    cout << "Fraction of rays required: " << frac_ray << endl;

}

//#############################################################################
// Compute correction factors with coherent rays (packages with 8 rays)
//#############################################################################

void CppTerrain::sw_dir_cor_coherent_rp8(double* sun_pos, float* sw_dir_cor) {

    if (pixel_per_gc_cl % 2) {
        cout << "Error: method is only implemented for even " <<
            "'pixel_per_gc' values" << endl;
        return;
    }

    auto start_ray = std::chrono::high_resolution_clock::now();
    size_t num_rays = 0;

    double num_tri_per_gc = pixel_per_gc_cl * pixel_per_gc_cl * 2.0;
    // number of triangles per grid cell

    num_rays += tbb::parallel_reduce(
        tbb::blocked_range<size_t>(0, num_gc_y_cl), 0.0,
        [&](tbb::blocked_range<size_t> r, size_t num_rays) {  // parallel

    // Loop through 2D-field of grid cells
    //for (size_t i = 0; i < num_gc_y_cl; i++) {  // serial
    for (size_t i=r.begin(); i<r.end(); ++i) {  // parallel
        for (size_t j = 0; j < num_gc_x_cl; j++) {

            size_t lin_ind_gc = lin_ind_2d(num_gc_x_cl, i, j);
            if (mask_cl[lin_ind_gc] == 1) {

            RTCRay8 ray8;
            float* sw_dir_cor_ray = new float[8];
            float sw_dir_cor_agg = 0.0;

            // Loop through pixels within grid cell (-> process by blocks of 4)
            for (size_t k = (i * pixel_per_gc_cl);
                k < ((i * pixel_per_gc_cl) + pixel_per_gc_cl); k += 2) {
                for (size_t m = (j * pixel_per_gc_cl);
                    m < ((j * pixel_per_gc_cl) + pixel_per_gc_cl); m += 2) {

                    unsigned int num_rays_gc = 0;
                    int valid8[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // 0: invalid

                    for (size_t k_block = k; k_block < (k + 2); k_block++) {
                    for (size_t m_block = m; m_block < (m + 2); m_block++) {

                    // Loop through two triangles per pixel
                    for (size_t n = 0; n < 2; n++) {

                        //-----------------------------------------------------
                        // Tilted triangle
                        //-----------------------------------------------------

                        size_t ind_tri_0, ind_tri_1, ind_tri_2;
                        func_ptr[n](dem_dim_1_cl,
                            k_block + (pixel_per_gc_cl * offset_gc_cl),
                            m_block + (pixel_per_gc_cl * offset_gc_cl),
                            ind_tri_0, ind_tri_1, ind_tri_2);

                        double vert_0_x = (double)vert_grid_cl[ind_tri_0];
                        double vert_0_y = (double)vert_grid_cl[ind_tri_0 + 1];
                        double vert_0_z = (double)vert_grid_cl[ind_tri_0 + 2];
                        double vert_1_x = (double)vert_grid_cl[ind_tri_1];
                        double vert_1_y = (double)vert_grid_cl[ind_tri_1 + 1];
                        double vert_1_z = (double)vert_grid_cl[ind_tri_1 + 2];
                        double vert_2_x = (double)vert_grid_cl[ind_tri_2];
                        double vert_2_y = (double)vert_grid_cl[ind_tri_2 + 1];
                        double vert_2_z = (double)vert_grid_cl[ind_tri_2 + 2];

                        double cent_x, cent_y, cent_z;
                        triangle_centroid(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            cent_x, cent_y, cent_z);

                        double norm_tilt_x, norm_tilt_y, norm_tilt_z,
                            area_tilt;
                        triangle_normal_area(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            norm_tilt_x, norm_tilt_y, norm_tilt_z,
                            area_tilt);

                        // Ray origin
                        double ray_org_x = (cent_x
                            + norm_tilt_x * ray_org_elev_cl);
                        double ray_org_y = (cent_y
                            + norm_tilt_y * ray_org_elev_cl);
                        double ray_org_z = (cent_z
                            + norm_tilt_z * ray_org_elev_cl);

                        //-----------------------------------------------------
                        // Horizontal triangle
                        //-----------------------------------------------------

                        func_ptr[n](dem_dim_in_1_cl, k_block, m_block,
                            ind_tri_0, ind_tri_1, ind_tri_2);

                        vert_0_x = (double)vert_grid_in_cl[ind_tri_0];
                        vert_0_y = (double)vert_grid_in_cl[ind_tri_0 + 1];
                        vert_0_z = (double)vert_grid_in_cl[ind_tri_0 + 2];
                        vert_1_x = (double)vert_grid_in_cl[ind_tri_1];
                        vert_1_y = (double)vert_grid_in_cl[ind_tri_1 + 1];
                        vert_1_z = (double)vert_grid_in_cl[ind_tri_1 + 2];
                        vert_2_x = (double)vert_grid_in_cl[ind_tri_2];
                        vert_2_y = (double)vert_grid_in_cl[ind_tri_2 + 1];
                        vert_2_z = (double)vert_grid_in_cl[ind_tri_2 + 2];

                        double norm_hori_x, norm_hori_y, norm_hori_z,
                            area_hori;
                        triangle_normal_area(vert_0_x, vert_0_y, vert_0_z,
                            vert_1_x, vert_1_y, vert_1_z,
                            vert_2_x, vert_2_y, vert_2_z,
                            norm_hori_x, norm_hori_y, norm_hori_z,
                            area_hori);

                        double surf_enl_fac = area_tilt / area_hori;

                        //-----------------------------------------------------
                        // Compute correction factor
                        //-----------------------------------------------------

                        // Compute sun unit vector
                        double sun_x = (sun_pos[0] - ray_org_x);
                        double sun_y = (sun_pos[1] - ray_org_y);
                        double sun_z = (sun_pos[2] - ray_org_z);
                        vec_unit(sun_x, sun_y, sun_z);

                        // Check for self-shadowing (Earth)
                        double dot_prod_hs = (norm_hori_x * sun_x
                            + norm_hori_y * sun_y
                            + norm_hori_z * sun_z);
                        if (dot_prod_hs <= dot_prod_min_cl) {
                            continue;  // sw_dir_cor += 0.0
                        }

                        // Check for self-shadowing (triangle)
                        double dot_prod_ts = norm_tilt_x * sun_x
                            + norm_tilt_y * sun_y
                            + norm_tilt_z * sun_z;
                        if (dot_prod_ts <= 0.0) {
                            continue;  // sw_dir_cor += 0.0
                        }

                        // Add ray
                        ray8.org_x[num_rays_gc] = (float)ray_org_x;
                        ray8.org_y[num_rays_gc] = (float)ray_org_y;
                        ray8.org_z[num_rays_gc] = (float)ray_org_z;
                        ray8.tnear[num_rays_gc] = 0.0;
                        ray8.dir_x[num_rays_gc] = (float)sun_x;
                        ray8.dir_y[num_rays_gc] = (float)sun_y;
                        ray8.dir_z[num_rays_gc] = (float)sun_z;
                        ray8.tfar[num_rays_gc] = (float)dist_search_cl;
                        // std::numeric_limits<float>::infinity();
                        ray8.id[num_rays_gc] = num_rays_gc;
                        valid8[num_rays_gc] = -1; // -1: valid

                        sw_dir_cor_ray[num_rays_gc] =
                            (float)(std::min(((dot_prod_ts / dot_prod_hs)
                            * surf_enl_fac), sw_dir_cor_max_cl));
                        num_rays_gc = num_rays_gc + 1;

                    }

                    }
                    }

                    struct RTCIntersectContext context;
                    rtcInitIntersectContext(&context);
                    context.flags = RTC_INTERSECT_CONTEXT_FLAG_COHERENT;

                    // Intersect rays with scene
                    rtcOccluded8(valid8, scene, &context, (RTCRay8*)&ray8);
                    num_rays += num_rays_gc;

                    for (size_t n = 0; n < num_rays_gc; n++) {
                        if (ray8.tfar[n] > 0.0) {
                            // no intersection -> 'tfar' is not updated;
                            // otherwise 'tfar' = -inf
                            sw_dir_cor_agg = sw_dir_cor_agg
                                + sw_dir_cor_ray[n];
                        }  // else: sw_dir_cor += 0.0
                    }

                }
            }

            delete[] sw_dir_cor_ray;

            sw_dir_cor[lin_ind_gc] = sw_dir_cor_agg / num_tri_per_gc;

            } else {

                sw_dir_cor[lin_ind_gc] = NAN;

            }

        }
    }

    return num_rays;  // parallel
    }, std::plus<size_t>());  // parallel

    auto end_ray = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time_ray = (end_ray - start_ray);
    cout << "Ray tracing time: " << time_ray.count() << " s" << endl;
    cout << "Number of rays shot: " << num_rays << endl;
    double frac_ray = (double)num_rays / (double)num_tri_cl;
    cout << "Fraction of rays required: " << frac_ray << endl;

}
