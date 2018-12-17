#ifndef ANFUTILS_HPP
#define ANFUTILS_HPP

#include <vector>

#include "configdata.h"
#include "polybori.h"

double sample_and_clone(const ConfigData& config,
                        const std::vector<polybori::BoolePolynomial>& eqs,
                        std::vector<polybori::BoolePolynomial>& equations,
                        double log2size);

void subsitute(const polybori::BooleMonomial& from_mono,
               const polybori::BoolePolynomial& to_poly,
               polybori::BoolePolynomial& poly);

#endif
