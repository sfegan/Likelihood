/**
 * @file Verbosity.h
 * @brief Singleton to store verbosity level for screen output and whether
 * files are to be overwritten.
 * 
 * $Header: /nfs/slac/g/glast/ground/cvs/Likelihood/src/Verbosity.h,v 1.1 2004/11/27 20:02:59 jchiang Exp $
 */

#ifndef Likelihood_Verbosity_h
#define Likelihood_Verbosity_h

namespace Likelihood {

class Verbosity {

public:
   static Verbosity * instance(unsigned int verbosity=2, bool clobber=true) {
      if (s_instance == 0) {
         s_instance = new Verbosity(verbosity, clobber);
      }
      return s_instance;
   }
   unsigned int value() {
      return m_verbosity;
   }
   bool clobber() {
      return m_clobber;
   }
protected:
   Verbosity(unsigned int verbosity, bool clobber=true) 
      : m_verbosity(verbosity), m_clobber(clobber) {}
private:
   static Verbosity * s_instance;
   unsigned int m_verbosity;
   bool m_clobber;
};

bool print_output(unsigned int local_verbosity=2);

bool clobber();

} // namespace Likelihood

#endif // Likelihood_Verbosity_h