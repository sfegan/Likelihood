/** 
 * @file SourceModel.cxx
 * @brief SourceModel class implementation
 * @author J. Chiang
 *
 * $Header: /nfs/slac/g/glast/ground/cvs/Likelihood/src/SourceModel.cxx,v 1.47 2004/08/25 15:28:49 jchiang Exp $
 */

#include <cassert>
#include <cmath>

#include <algorithm>
#include <string>
#include <vector>

#include "fitsio.h"

#include "xml/Dom.h"
#include "xml/XmlParser.h"

#include "facilities/Util.h"

#include "astro/SkyDir.h"

#include "optimizers/Arg.h"
#include "optimizers/FunctionFactory.h"

#include "Likelihood/Exception.h"
#include "Likelihood/ExposureCube.h"
#include "Likelihood/ExposureMap.h"
#include "Likelihood/RoiCuts.h"
#include "Likelihood/SpatialMap.h"
#include "Likelihood/SkyDirFunction.h"
#include "Likelihood/SourceFactory.h"
#include "Likelihood/TrapQuad.h"
#include "Likelihood/PointSource.h"
#include "Likelihood/FluxBuilder.h"
#include "Likelihood/ScData.h"
#include "Likelihood/SourceModelBuilder.h"
#include "Likelihood/SourceModel.h"

namespace {
   void fitsReportError(FILE *stream, int status) {
      fits_report_error(stream, status);
      if (status != 0) {
         throw std::string("writeExposureFile: cfitsio error.");
      }
   }
} // unnamed namespace

namespace Likelihood {

int SourceModel::s_refCount = 0;
std::map<std::string, Source *> SourceModel::s_sources;

SourceModel::~SourceModel() {
   s_refCount--;
   if (s_refCount == 0) {
      std::map<std::string, Source *>::iterator it = s_sources.begin();
      for (; it != s_sources.end(); it++)
         delete (*it).second;
   }
   s_sources.clear();
}

void SourceModel::setParam(const optimizers::Parameter &param, 
                           const std::string &funcName,
                           const std::string &srcName) 
   throw(optimizers::Exception) {
   if (s_sources.count(srcName)) {
      Source::FuncMap srcFuncs = (*s_sources[srcName]).getSrcFuncs();
      if (srcFuncs.count(funcName)) {
         srcFuncs[funcName]->setParam(param.getName(), 
                                      param.getValue(),
                                      param.isFree());
         syncParams();
         return;
      }
   }
   std::string errorMessage = "SourceModel::setParam:\n Function " 
      + funcName + " for Source " + srcName + " was not found.";
   throw optimizers::Exception(errorMessage);
}
 
std::vector<double>::const_iterator SourceModel::setParamValues_(
   std::vector<double>::const_iterator it) { 
   std::map<std::string, Source *>::iterator srcIt = s_sources.begin();
   for ( ; srcIt != s_sources.end(); ++srcIt) {
      Source::FuncMap srcFuncs = srcIt->second->getSrcFuncs();
      Source::FuncMap::iterator func_it = srcFuncs.begin();
      for (; func_it != srcFuncs.end(); func_it++) 
         it = (*func_it).second->setParamValues_(it);
   }
   syncParams();
   return it;
}

std::vector<double>::const_iterator SourceModel::setFreeParamValues_(
   std::vector<double>::const_iterator it) { 
   std::map<std::string, Source *>::iterator srcIt = s_sources.begin();
   for ( ; srcIt != s_sources.end(); ++srcIt) {
      Source::FuncMap srcFuncs = srcIt->second->getSrcFuncs();
      Source::FuncMap::iterator func_it = srcFuncs.begin();
      for (; func_it != srcFuncs.end(); func_it++) 
         it = (*func_it).second->setFreeParamValues_(it);
   }
   syncParams();
   return it;
}

optimizers::Parameter SourceModel::getParam(const std::string &paramName,
                                            const std::string &funcName,
                                            const std::string &srcName) const 
   throw(optimizers::Exception, optimizers::ParameterNotFound) {
   if (s_sources.count(srcName)) {
      std::vector<optimizers::Parameter> params;
      Source::FuncMap srcFuncs = s_sources[srcName]->getSrcFuncs();
      if (srcFuncs.count(funcName)) {    //check for funcName
         try {
            srcFuncs[funcName]->getParams(params);
         } catch (optimizers::Exception &eObj) {
            std::cerr << eObj.what() << std::endl;
            assert(false);
         }                 
         for (unsigned int j = 0; j < params.size(); j++) {
            if (paramName == params[j].getName()) {
               return params[j];
            }
         }
         throw optimizers::ParameterNotFound(paramName, funcName, 
                                             "SourceModel::getParam");
      }
      std::string errorMessage = "SourceModel::getParam:\n Function"
         + funcName + " was not found in Source " 
         + srcName + ".";
      throw optimizers::Exception(errorMessage);
   }
   std::string errorMessage = "SourceModel::getParam:\nSource "
      + srcName + " was not found.";
   throw optimizers::Exception(errorMessage);
}

void SourceModel::setParamBounds(const std::string &paramName,
                                 const std::string &funcName,
                                 const std::string &srcName,
                                 double lower, double upper)
   throw(optimizers::ParameterNotFound, optimizers::OutOfBounds) {
   optimizers::Parameter my_param = getParam(paramName, funcName, srcName);
   my_param.setBounds(lower, upper);
   setParam(my_param, funcName, srcName);
   syncParams();
}

void SourceModel::setParamScale(const std::string &paramName,
                                const std::string &funcName,
                                const std::string &srcName,
                                double scale) 
   throw(optimizers::ParameterNotFound) {
   optimizers::Parameter my_param = getParam(paramName, funcName, srcName);
   my_param.setScale(scale);
   setParam(my_param, funcName, srcName);
   syncParams();
}

void SourceModel::setParamTrueValue(const std::string &paramName,
                                    const std::string &funcName,
                                    const std::string &srcName,
                                    double paramValue)
   throw(optimizers::ParameterNotFound, optimizers::OutOfBounds) {
   optimizers::Parameter my_param = getParam(paramName, funcName, srcName);
   my_param.setTrueValue(paramValue);
   setParam(my_param, funcName, srcName);
   syncParams();
}

void SourceModel::setParams_(std::vector<optimizers::Parameter> &params, 
                             bool setFree) 
   throw(optimizers::Exception, optimizers::ParameterNotFound) {
// Ensure the number of Parameters matches.
   unsigned int numParams;
   if (setFree) {
      numParams = getNumFreeParams();
   } else {
      numParams = getNumParams();
   }
   if (params.size() != numParams) {
      std::string errorMessage = std::string("SourceModel::setParams:\n") 
         + "Inconsistent number of Parameters.";
      throw optimizers::Exception(errorMessage);
   }
// Assume ordering of Parameters in params matches that given by the
// ordering of the Sources and their Functions.
   int k = 0;  // params' index
   std::map<std::string, Source *>::iterator srcIt = s_sources.begin();
   for ( ; srcIt != s_sources.end(); ++srcIt) {
      Source::FuncMap srcFuncs = srcIt->second->getSrcFuncs();
      Source::FuncMap::iterator func_it = srcFuncs.begin();
      for (; func_it != srcFuncs.end(); func_it++) {
         unsigned int numParams;
         if (setFree) {
            numParams = func_it->second->getNumFreeParams();
         } else { 
            numParams = func_it->second->getNumParams();
         }
         for (unsigned int j = 0; j < numParams; j++) {
            func_it->second->setParam(params[k]);
            k++;
         }
      }
   }
   syncParams();
}

void SourceModel::addSource(Source *src) {
   if (!s_sources.count(src->getName())) {
      s_sources[src->getName()] = src->clone();
      syncParams();
   } else {
      throw Exception("Likelihood::SourceModel:\nSource named " 
                      + src->getName() + " alread exists.");
   }
}
 
Source * SourceModel::deleteSource(const std::string &srcName) 
   throw(optimizers::Exception) {
   std::map<std::string, Source *>::iterator it = s_sources.find(srcName);
   if (it != s_sources.end()) {
      Source * mySource = it->second;
      s_sources.erase(it);
      syncParams();
      return mySource;
   }
   std::string errorMessage = "SourceModel::deleteSource:\n" 
      + srcName + " was not found.";
   throw optimizers::Exception(errorMessage);
}

void SourceModel::deleteAllSources() {
   std::vector<std::string> srcNames;
   getSrcNames(srcNames);
   for (unsigned int i = 0; i < srcNames.size(); i++) {
      Source * source = deleteSource(srcNames[i]);
      delete source;
   }
   m_parameter.clear();
}

Source * SourceModel::getSource(const std::string &srcName) {
   if (s_sources.count(srcName)) {
      return s_sources[srcName];
   }
   return 0;
}

void SourceModel::getSrcNames(std::vector<std::string> &names) const {
   names.clear();
   std::map<std::string, Source *>::iterator it = s_sources.begin();
   for ( ; it != s_sources.end(); ++it) {
      names.push_back(it->first);
   }
}

double SourceModel::value(optimizers::Arg &x) const {
   double my_val = 0.;
   std::map<std::string, Source *>::iterator srcIt = s_sources.begin();
   for ( ; srcIt != s_sources.end(); ++srcIt) {
      Source::FuncMap srcFuncs = srcIt->second->getSrcFuncs();
      Source::FuncMap::iterator func_it = srcFuncs.begin();
      for (; func_it != srcFuncs.end(); func_it++) {
         my_val += (*func_it).second->value(x);
      }
   }
   return my_val;
}

void SourceModel::syncParams() { // remake parameter vector from scratch 
   m_parameter.clear();

   std::map<std::string, Source *>::iterator srcIt = s_sources.begin();
   for ( ; srcIt != s_sources.end(); ++srcIt) {
      Source::FuncMap srcFuncs = srcIt->second->getSrcFuncs();
      Source::FuncMap::iterator func_it = srcFuncs.begin();
      for (; func_it != srcFuncs.end(); func_it++) {
         std::vector<optimizers::Parameter> params;
         (*func_it).second->getParams(params);
         for (unsigned int ip = 0; ip < params.size(); ip++)
            m_parameter.push_back(params[ip]);
      }
   }
}

void SourceModel::fetchDerivs(optimizers::Arg &x,
                              std::vector<double> &derivs, 
                              bool getFree) const {
   derivs.clear();

   std::map<std::string, Source *>::iterator srcIt = s_sources.begin();
   for ( ; srcIt != s_sources.end(); ++srcIt) {
      Source::FuncMap srcFuncs = srcIt->second->getSrcFuncs();
      Source::FuncMap::iterator func_it = srcFuncs.begin();
      for (; func_it != srcFuncs.end(); func_it++) {
         std::vector<double> my_derivs;
         if (getFree) {
            (*func_it).second->getFreeDerivs(x, my_derivs);
         } else {
            (*func_it).second->getDerivs(x, my_derivs);
         }
         for (unsigned int j = 0; j < my_derivs.size(); j++) 
            derivs.push_back(my_derivs[j]);
      }
   }
}

void SourceModel::readXml(std::string xmlFile,
                          optimizers::FunctionFactory &funcFactory,
                          bool requireExposure) {

// Expand any environment variables in the xmlFile name.
   facilities::Util::expandEnvVar(&xmlFile);

// Create a SourceFactory to read in the xml file.
   SourceFactory srcFactory;
   try {
      srcFactory.readXml(xmlFile, funcFactory, requireExposure);
   } catch (xml::DomException &eObj) {
      std::cout << "SourceModel::readXml:\n DomException: " 
                << eObj.what() << std::endl;
   }

// Loop over the sources that are now contained in srcFactory and add
// each one to the source model.
   std::vector<std::string> srcNames;
   srcFactory.fetchSrcNames(srcNames);

   std::vector<std::string>::iterator nameIt = srcNames.begin();
   for ( ; nameIt != srcNames.end(); nameIt++) {
      Source *src = srcFactory.create(*nameIt);
      if (m_verbose) std::cout << "adding source " << *nameIt << std::endl;
      addSource(src);
   }
   syncParams();
}

void SourceModel::reReadXml(std::string xmlFile) {

   facilities::Util::expandEnvVar(&xmlFile);

   xml::XmlParser *parser = new xml::XmlParser();

   DomDocument doc = parser->parse(xmlFile.c_str());

   if (doc == 0) {
      std::string errorMessage = "SourceFactory::readXml:\nInput xml file, "
         + xmlFile + ", not parsed successfully.";
      throw Exception(errorMessage);
   }

//*** direct Xerces API call...still available in Xerces 2.4.0 ***/
   DomElement source_library = doc.getDocumentElement();
   if (!xml::Dom::checkTagName(source_library, "source_library")) {
      throw Exception("SourceModel::reReadXml:\nsource_library not found");
   }

// Loop through source DOM_Elements and Source objects in parallel.
   std::vector<DomElement> srcs;
   xml::Dom::getChildrenByTagName(source_library, "source", srcs);

   for (unsigned int j = 0; j < srcs.size(); j++) {
      std::string srcName = xml::Dom::getAttribute(srcs[j], "name");
      Source * my_source(0);
      if (s_sources.count(srcName)) {
         my_source = s_sources[srcName];
      } else {
         continue;
      }
      std::string srcType = xml::Dom::getAttribute(srcs[j], "type");
// Get spectrum and spatialModel elements
      std::vector<DomElement> child;
      xml::Dom::getChildrenByTagName(srcs[j], "spectrum", child);
      DomElement spectrum = child[0];

      xml::Dom::getChildrenByTagName(srcs[j], "spatialModel", child);
      DomElement spatialModel = child[0];

      my_source->getSrcFuncs()["Spectrum"]->setParams(spectrum);
      if (srcType == "PointSource") {
// Extract (RA, Dec) from the parameter elements and use
// PointSource::setDir(...) method to ensure that exposure gets
// recalculated.
         double ra(0), dec(0);
         std::vector<DomElement> params;
         xml::Dom::getChildrenByTagName(spatialModel, "parameter", params);

         std::vector<DomElement>::const_iterator paramIt = params.begin();
         for ( ; paramIt != params.end(); paramIt++) {
            std::string name = xml::Dom::getAttribute(*paramIt, "name");
            if (name == "RA")
               ra = ::atof(xml::Dom::getAttribute(*paramIt, "value").c_str());
            if (name == "DEC")
               dec = ::atof(xml::Dom::getAttribute(*paramIt, "value").c_str());
         }
         astro::SkyDir newDir(ra, dec);
         double tol = 1e-4;
         if (newDir.difference(
                dynamic_cast<PointSource *>(my_source)->getDir() ) > tol) {
// Reset the direction, re-computing the PointSource exposure.
            my_source->setDir(newDir);
         }
      } else if (srcType == "DiffuseSource") {
         my_source->getSrcFuncs()["SpatialDist"]->setParams(spatialModel);
      } else {
         std::cerr << "SourceModel::reReadXml: "
                   << "Unknown Source type: " << srcType << std::endl;
         assert(false);
      }
   }
   syncParams();
   delete parser;
}

void SourceModel::writeXml(std::string xmlFile,
                           const std::string &functionLibrary,
                           const std::string &srcLibTitle) {

   SourceModelBuilder builder(functionLibrary, srcLibTitle);

   std::map<std::string, Source *>::iterator srcIt = s_sources.begin();
   for ( ; srcIt != s_sources.end(); srcIt++) {
      builder.addSource(*(srcIt->second));
   }

   builder.write(xmlFile);
}

void SourceModel::write_fluxXml(std::string xmlFile) {

   FluxBuilder builder;

   std::map<std::string, Source *>::iterator srcIt = s_sources.begin();
   for ( ; srcIt != s_sources.end(); srcIt++) {
      builder.addSource(*(srcIt->second));
   }

   builder.write(xmlFile);
}

bool SourceModel::hasSrcNamed(const std::string & srcName) const {
   std::vector<std::string> names;
   getSrcNames(names);
   std::vector<std::string>::iterator it 
      = std::find(names.begin(), names.end(), srcName);
   if (it == names.end()) {
      return false;
   }
   return true;
}

void SourceModel::makeCountsMap(const CountsMap & dataMap, 
                                const std::string & filename) const {

   if (ExposureCube::instance() == 0) {
      std::runtime_error(std::string("SourceModel::makeCountsMap:\n")
                         + "Exposure cube not available.");
   }

   std::vector<double> map;
   long nx = dataMap.imageDimension(0);
   long ny = dataMap.imageDimension(1);
   long nz = dataMap.imageDimension(2);
   map.resize(nx*ny*nz, 0);
   
   std::vector<double> longitudes;
   dataMap.getAxisVector(0, longitudes);
   std::vector<double> latitudes;
   dataMap.getAxisVector(1, latitudes);
   std::vector<double> energies;
   dataMap.getAxisVector(2, energies);

   astro::SkyDir::CoordSystem coordType(astro::SkyDir::EQUATORIAL);
   if (dataMap.use_lb) {
      coordType = astro::SkyDir::GALACTIC;
   }

   std::vector<astro::SkyDir> pixelDirs;
   pixelDirs.reserve(nx*ny);
   std::vector<double>::const_iterator latIt = latitudes.begin();
   for ( ; latIt != latitudes.end() - 1; ++latIt) {
      double latitude = (*latIt + *(latIt+1))/2.;
      std::vector<double>::const_iterator lonIt = longitudes.begin();
      for ( ; lonIt != longitudes.end() - 1; ++lonIt) {
         double longitude = (*lonIt + *(lonIt+1))/2.;
         pixelDirs.push_back(astro::SkyDir(*lonIt, *latIt, coordType));
      }
   }

   std::map<std::string, Source *>::const_iterator src;

// loop over pixel directions
   for (unsigned int j = 0; j < pixelDirs.size(); j++) {
      for (unsigned int k = 0; k < energies.size()-1; k++) {
         int indx = k*nx*ny + j;
         double energy = sqrt(energies[k]*energies[k+1]);
         for (int evtType = 0; evtType < 2; evtType++) {
            for (src = s_sources.begin(); src != s_sources.end(); ++src) {
               Aeff aeff(src->second, pixelDirs[j], energy, evtType);
               map[indx] 
                  += ExposureCube::instance()->value(pixelDirs[j], aeff);
            } // src
         } // evtType
      } // k
   } // j

   CountsMap modelMap(dataMap);
   modelMap.setImage(map);
   modelMap.writeOutput("Likelihood::SourceModel", filename);
}

SourceModel::Aeff::Aeff(Source * src, astro::SkyDir & appDir, double energy,
                        int type)
   : m_src(src), m_appDir(appDir), m_energy(energy), m_type(type) {
   PointSource * ptsrc = dynamic_cast<PointSource *>(src);
   if (ptsrc == 0) {
      m_separation = 90.;
   } else {
      m_separation = ptsrc->getDir().difference(appDir)*180./M_PI;
   }
}

double SourceModel::Aeff::operator()(double costheta) const {
   if (m_separation < 90.) {
      double inclination = acos(costheta)*180./M_PI;
      static double phi(0);
      return m_src->fluxDensity(inclination, phi, m_energy, m_separation,
                                m_type);
   }
   return 0;
}

void SourceModel::writeFitsFile(const std::string &filename, 
                                std::vector<double> &ras,
                                std::vector<double> &decs,
                                std::vector<double> &energies,
                                std::vector<double> &map) const {
   


}

// void SourceModel::writeFitsFile(const std::string &filename, 
//                                 std::vector<double> &ras,
//                                 std::vector<double> &decs,
//                                 std::vector<double> &energies,
//                                 std::vector<double> &map) const {
//    fitsfile *fptr;
//    int status = 0;
   
// // Always overwrite an existing file.
//    std::remove(filename.c_str());
//    fits_create_file(&fptr, filename.c_str(), &status);
//    fitsReportError(stderr, status);

//    int bitpix = DOUBLE_IMG;
//    long naxis = 3;
//    long naxes[] = {ras.size(), decs.size(), energies.size()};
//    fits_create_img(fptr, bitpix, naxis, naxes, &status);
//    fitsReportError(stderr, status);

// // Write the exposure map data.
//    long group = 0;
//    long dim1 = ras.size();
//    long dim2 = decs.size();

//    fits_write_3d_dbl(fptr, group, dim1, dim2, 
//                      ras.size(), decs.size(), energies.size(),
//                      &map[0], &status);
//    fitsReportError(stderr, status);

// // Write some keywords.
//    double l0 = ras[0];
//    fits_update_key(fptr, TDOUBLE, "CRVAL1", &l0, 
//                    "longitude of reference pixel", &status);
//    fitsReportError(stderr, status);
//    double b0 = decs[0];
//    fits_update_key(fptr, TDOUBLE, "CRVAL2", &b0, 
//                    "latitude of reference pixel", &status);
//    fitsReportError(stderr, status);
   
//    double lstep = ras[1] - ras[0];
//    fits_update_key(fptr, TDOUBLE, "CDELT1", &lstep, 
//                    "longitude step at ref. pixel", &status);
//    fitsReportError(stderr, status);
//    double bstep = decs[1] - decs[0];
//    fits_update_key(fptr, TDOUBLE, "CDELT2", &bstep, 
//                    "latitude step at ref. pixel", &status);
//    fitsReportError(stderr, status);
   
//    float crpix1 = 1.0;
//    fits_update_key(fptr, TFLOAT, "CRPIX1", &crpix1, 
//                    "reference pixel for longitude coordinate", &status);
//    fitsReportError(stderr, status);
//    float crpix2 = 1.0;
//    fits_update_key(fptr, TFLOAT, "CRPIX2", &crpix2, 
//                    "reference pixel for latitude coordinate", &status);
//    fitsReportError(stderr, status);
   
//    char *ctype1 = "RA---CAR";
//    fits_update_key(fptr, TSTRING, "CTYPE1", ctype1, 
//                    "right ascension", &status);
//    fitsReportError(stderr, status);
//    char *ctype2 = "DEC--CAR";
//    fits_update_key(fptr, TSTRING, "CTYPE2", ctype2, 
//                    "declination", &status);
//    fitsReportError(stderr, status);

//    double logEmin = log(energies[0]);
//    fits_update_key(fptr, TDOUBLE, "CRVAL3", &logEmin,
//                    "reference value for log_energy coordinate", &status);
//    fitsReportError(stderr, status);

//    int nee = energies.size();
//    double estep = log(energies[nee-1]/energies[0])/(nee-1);
//    fits_update_key(fptr, TDOUBLE, "CDELT3", &estep, 
//                    "step in log_energy coordinate", &status);
//    fitsReportError(stderr, status);

//    float crpix3 = 1.;
//    fits_update_key(fptr, TFLOAT, "CRPIX3", &crpix3,
//                    "reference pixel for log_energy coordinate", &status);
//    fitsReportError(stderr, status);

//    char *ctype3 = "log_MeV";
//    fits_update_key(fptr, TSTRING, "CTYPE3", ctype3,
//                    "units for log_energy", &status);
//    fitsReportError(stderr, status);

   
// // Write the energy array as a binary table.
//    int nrows = energies.size();
//    int tfields = 1;
//    char *ttype[] = {"Energy"};
//    char *tform[] = {"1D"};
//    char *tunit[] = {"MeV"};
//    char extname[] = "True Photon Energy Array";
   
//    int firstrow  = 1;
//    int firstelem = 1;
   
//    fits_create_tbl(fptr, BINARY_TBL, nrows, tfields, ttype, tform,
//                    tunit, extname, &status);
//    fitsReportError(stderr, status);
   
//    fits_write_col(fptr, TDOUBLE, 1, firstrow, firstelem, nrows, 
//                   &energies[0], &status);
//    fitsReportError(stderr, status);
   
//    fits_close_file(fptr, &status);
//    fitsReportError(stderr, status);
   
//    return;
// }

} // namespace Likelihood
