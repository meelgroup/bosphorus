#ifndef EXTENDEDLINEARIZATION_HPP
#define EXTENDEDLINEARIZATION_HPP

#include <vector>

#include "configdata.h"
#include "polybori.h"

USING_NAMESPACE_PBORI

bool extendedLinearization(const ConfigData& config,
                           const std::vector<BoolePolynomial>& eqs,
                           std::vector<BoolePolynomial>& loop_learnt);

#endif
