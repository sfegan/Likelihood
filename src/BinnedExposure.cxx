/**
 * @file BinnedExposure.cxx
 * @brief Integral of effective area over time for the entire sky at
 * various energies.
 * @author J. Chiang
 *
 * $Header: /nfs/slac/g/glast/ground/cvs/Likelihood/src/BinnedExposure.cxx,v 1.13 2005/11/17 01:47:28 jchiang Exp $
 */

#include <cmath>

#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "tip/Header.h"
#include "tip/IFileSvc.h"
#include "tip/Image.h"

#include "astro/SkyProj.h"

#include "Likelihood/BinnedExposure.h"
#include "Likelihood/Observation.h"

#include "Verbosity.h"

namespace Likelihood {

BinnedExposure::BinnedExposure() : m_observation(0), m_proj(0) {}

BinnedExposure::BinnedExposure(const std::vector<double> & energies,
                               const Observation & observation) 
   : m_observation(&observation), m_energies(energies), m_proj(0) {
   computeMap();
}

BinnedExposure::BinnedExposure(const std::string & filename) 
   : m_observation(0), m_proj(0) {
   m_proj = new astro::SkyProj(filename);

   std::auto_ptr<const tip::Image> 
      image(tip::IFileSvc::instance().readImage(filename, ""));

   m_exposureMap.clear();
   image->get(m_exposureMap);

   m_naxes = image->getImageDimensions();

   std::auto_ptr<const tip::Table>
      energies(tip::IFileSvc::instance().readTable(filename, "Energies"));

   m_energies.clear();
   tip::Table::ConstIterator it = energies->begin();
   tip::ConstTableRecord & row = *it;
   for ( ; it != energies->end(); ++it) {
      double value;
      row["Energy"].get(value);
      m_energies.push_back(value);
   }
}

BinnedExposure::~BinnedExposure() {
   delete m_proj;
}

double BinnedExposure::operator()(double energy, double ra, double dec) const {
   std::vector<double>::const_iterator ie = std::find(m_energies.begin(),
                                                      m_energies.end(),
                                                      energy);
   if (ie == m_energies.end()) {
      std::ostringstream what;
      what << "BinnedExposure::operator(): The energy " << energy 
           << " is not available.\nHere are the relevant energies:\n";
      for (unsigned int i = 0; i < m_energies.size(); i++) {
         what << m_energies.at(i) << "\n";
      }
      throw std::runtime_error(what.str());
   }
   unsigned int k = ie - m_energies.begin();

   std::pair<double, double> pixel = m_proj->sph2pix(ra, dec);

   int i = static_cast<int>(pixel.first) - 1;
   int j = static_cast<int>(pixel.second) - 1;

   unsigned int indx = (k*m_naxes.at(1) + j)*m_naxes.at(0) + i;

   try {
      return m_exposureMap.at(indx);
   } catch (std::out_of_range &) {
      return 0;
   }
}

void BinnedExposure::computeMap() {
   m_naxes.push_back(360);
   m_naxes.push_back(180);
   m_naxes.push_back(m_energies.size());

   double crpix[] = {m_naxes.at(0)/2 + 0.5, m_naxes.at(1)/2 + 0.5};
   double crval[] = {180., 0};
   double cdelt[] = {-1, 1};
   double crota2(0);

   m_proj = new astro::SkyProj("CAR", crpix, crval, cdelt, crota2, false);

   m_exposureMap.resize(m_naxes.at(0)*m_naxes.at(1)*m_energies.size(), 0);
   int iter(0);
   if (print_output()) {
      std::cerr << "Computing binned exposure map";
   }
   for (int j = 0; j < m_naxes.at(1); j++) {
      for (int i = 0; i < m_naxes.at(0); i++) {
         if (print_output() && 
             (iter % ((m_naxes.at(1)*m_naxes.at(0))/20)) == 0) {
            std::cerr << ".";
         }
         std::pair<double, double> coord = m_proj->pix2sph(i + 1, j + 1);
         astro::SkyDir dir(coord.first, coord.second);
         for (unsigned int k = 0; k < m_energies.size(); k++) {
            unsigned int indx = (k*m_naxes.at(1) + j)*m_naxes.at(0) + i;
            for (int evtType = 0; evtType < 2; evtType++) {
               Aeff aeff(m_energies[k], evtType, *m_observation);
               m_exposureMap.at(indx)
                  += m_observation->expCube().value(dir, aeff);
            }
         }
         iter++;
      }
   }
   if (print_output()) {
      std::cerr << "!" << std::endl;
   }
}

double BinnedExposure::Aeff::s_phi(0);

double BinnedExposure::Aeff::operator()(double cosTheta) const {
   double inclination = acos(cosTheta)*180./M_PI;
   std::map<unsigned int, irfInterface::Irfs *>::const_iterator respIt 
      = m_observation.respFuncs().begin();
   for ( ; respIt != m_observation.respFuncs().end(); ++respIt) {
      if (respIt->second->irfID() == m_evtType) {
         irfInterface::IAeff * aeff = respIt->second->aeff();
         double aeff_val = aeff->value(m_energy, inclination, s_phi);
         return aeff_val;
      }
   }
   return 0;
}

void BinnedExposure::writeOutput(const std::string & filename) const {
   std::remove(filename.c_str());

   std::string ext("PRIMARY");
   tip::IFileSvc::instance().appendImage(filename, ext, m_naxes);
   tip::Image * image = tip::IFileSvc::instance().editImage(filename, ext);

   image->set(m_exposureMap);

   tip::Header & header(image->getHeader());

   header["TELESCOP"].set("GLAST");
   header["INSTRUME"].set("LAT SIMULATION");
   header["DATE-OBS"].set("");
   header["DATE-END"].set("");

   header["CRVAL1"].set(180.);
   header["CRPIX1"].set(m_naxes.at(0)/2 + 0.5);
   header["CDELT1"].set(-1.);
   header["CTYPE1"].set("RA---CAR");

   header["CRVAL2"].set(0);
   header["CRPIX2"].set(m_naxes.at(1)/2 + 0.5);
   header["CDELT2"].set(1.);
   header["CTYPE2"].set("DEC--CAR");

   int nee = m_energies.size();
   header["CRVAL3"].set(log(m_energies.at(0)));
   header["CRPIX3"].set(1);
   header["CDELT3"].set(log(m_energies.at(nee-1)/m_energies.at(0))/(nee-1));
   header["CTYPE3"].set("log_Energy");

   delete image;

   ext = "ENERGIES";
   tip::IFileSvc::instance().appendTable(filename, ext);
   tip::Table * table = tip::IFileSvc::instance().editTable(filename, ext);
   table->appendField("Energy", "1D");
   table->setNumRecords(m_energies.size());

   tip::Table::Iterator row = table->begin();
   tip::Table::Record & record = *row;

   std::vector<double>::const_iterator energy = m_energies.begin();
   for ( ; energy != m_energies.end(); ++energy, ++row) {
      record["Energy"].set(*energy);
   }

   delete table;
}

} // namespace Likelihood
