/**
 * @file WcsMap.h
 * @brief A map with reference point centered on the image and that
 * uses WCS projections for indexing its internal representation.
 * @author J. Chiang
 *
 * $Header: /nfs/slac/g/glast/ground/cvs/Likelihood/Likelihood/WcsMap.h,v 1.13 2009/05/30 22:35:42 jchiang Exp $
 */

#ifndef Likelihood_WcsMap_h
#define Likelihood_WcsMap_h

#include <vector>
#include <utility>

#include "astro/SkyDir.h"

namespace Likelihood {

class BinnedExposure;
class DiffuseSource;
class MeanPsf;

/**
 * @class WcsMap
 * @brief A map with reference point centered on the image and that
 * uses WCS projections for indexing its internal representation.
 * @author J. Chiang
 * 
 * $Header: /nfs/slac/g/glast/ground/cvs/Likelihood/Likelihood/WcsMap.h,v 1.13 2009/05/30 22:35:42 jchiang Exp $
 */

class WcsMap {

public:

   WcsMap(const std::string & filename, const std::string & extension="",
          bool interpolate=true);

   WcsMap(const DiffuseSource & diffuseSource, double ra, double dec,
          double pix_size, int npts, double energy=100.,
          const std::string & proj_name="STG", bool use_lb=false,
          bool interpolate=false);

   ~WcsMap();

   WcsMap(const WcsMap &);

   WcsMap & operator=(const WcsMap &);

   double operator()(const astro::SkyDir & dir) const;

   WcsMap convolve(double energy, const MeanPsf & psf,
                   const BinnedExposure & exposure,
                   bool performConvolution=true) const;

   const std::vector< std::vector<double> > & image() const {
      return m_image;
   }

   /// @return Solid angle of the (ilon, ilat) pixel
   static double solidAngle(const astro::SkyProj & proj, 
                            double ilon, double ilat);

   double solidAngle(double ilon, double ilat) const;

   const std::vector< std::vector<double> > & solidAngles() const;

   /// @return Pixel value as a function of index
   double pixelValue(double ilon, double ilat) const;
   
   /// @return SkyDir corresponding to the pixel indices
   astro::SkyDir skyDir(double ilon, double ilat) const;

   int nxpix() const {
      return m_naxis1;
   }

   int nypix() const {
      return m_naxis2;
   }

   bool insideMap(const astro::SkyDir & dir) const;

   std::pair<astro::SkyDir, astro::SkyDir> 
   minMaxDistPixels(const astro::SkyDir & dir) const;

   void getCorners(std::vector<astro::SkyDir> & corners) const;

   double mapIntegral() const;

private:

   astro::SkyDir m_refDir;

   std::vector< std::vector<double> > m_image;

   mutable std::vector< std::vector<double> > m_solidAngles;

   int m_naxis1;
   int m_naxis2;

   astro::SkyProj * m_proj;
   
   bool m_interpolate;

   bool m_isPeriodic;

   double m_cdelt1, m_cdelt2;

   astro::SkyDir::CoordSystem m_coordSys;

   double m_mapIntegral;

   WcsMap();

   void computeMapIntegral();

};

} // namespace Likelihood

#endif // Likelihood_WcsMap_h
