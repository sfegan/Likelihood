/** @file lbfgs.h
 * @brief Declaration for the lbfgs Optimizer subclass.
 * @author J. Chiang
 *
 * $Header: /nfs/slac/g/glast/ground/cvs/Likelihood/src/lbfgs.h,v 1.4 2003/03/17 00:53:44 jchiang Exp $
 */

#ifndef lbfgs_h
#define lbfgs_h

#include "Likelihood/Optimizer.h"
#include "Likelihood/Statistic.h"

namespace Likelihood {

/** 
 * @class lbfgs
 *
 * @brief Wrapper class for the Broyden-Fletcher-Goldfarb-Shanno
 * variable metric implementation of Byrd, Lu, Nocedal, & Zhu 1995,
 * SIAM, J. Sci. Comp., 16, 5 (http://www.netlib.org/opt/lbfgs_bcm.shar).
 *
 * @author J. Chiang
 *    
 * $Header:
 */

class lbfgs : public Optimizer {
    
public:
    
   lbfgs(Statistic &stat) {s_stat = &stat;}
   virtual ~lbfgs() {}

   void find_min(int verbose = 0, double tol = 1e-5);
    
protected:

   static int s_verbose;

   //! interface to the objective function that lbgfs_bcm expects
   static long statInterface(long *nparams, double *param_vals, 
			     double *func_val, double *derivs);

   static Statistic *s_stat;

};

} // namespace Likelihood

#endif // lbfgs_h
