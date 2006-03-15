/**
 * @file SourceMapRegistry.h
 * @brief Class to manage creation of SourceMaps for gtmodelmap application.
 * @author J. Chiang
 *
 * $Header: /nfs/slac/g/glast/ground/cvs/Likelihood/src/gtmodelmap/SourceMapRegistry.h,v 1.1 2005/10/10 21:42:06 jchiang Exp $
 */

#ifndef Likelihood_SourceMapRegistry_h
#define Likelihood_SourceMapRegistry_h

#include <string>
#include <vector>

namespace optimizers {
   class FunctionFactory;
}

namespace Likelihood {
   class BinnedLikelihood;
   class CountsMap;
   class Observation;
   class SourceMap;
}

/** 
 * @class SourceMapRegistry
 * @brief Helper class to manage SourceMaps for gtmodelmap
 */

class SourceMapRegistry {

public:

   SourceMapRegistry(const std::string & countsMap,
                     const std::string & xmlFile,
                     const std::string & irfs,
                     const std::string & expCube,
                     const std::string & binnedExpMap,
                     optimizers::FunctionFactory & funcFactory,
                     bool performConvolution=true);

   ~SourceMapRegistry();

   const std::vector<float> & sourceMap(const std::string & srcName);

private:

   Likelihood::Observation * m_observation;
   Likelihood::CountsMap * m_countsMap;
   Likelihood::BinnedLikelihood * m_logLike;

   Likelihood::SourceMap * m_sourceMap;

};

#endif // Likelihood_SourceMapRegistry_h
