#ifndef ELIMLIN_HPP
#define ELIMLIN_HPP

#include <vector>

#include "configdata.h"
#include "polybori.h"

bool elimLin(const ConfigData& config,
             const std::vector<polybori::BoolePolynomial>& eqs,
             std::vector<polybori::BoolePolynomial>& loop_learnt);

#endif
