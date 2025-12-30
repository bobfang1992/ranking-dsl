#pragma once

#include <vector>

#include "object/obj.h"

namespace ranking_dsl {

/**
 * A batch of candidate objects processed together.
 */
using CandidateBatch = std::vector<Obj>;

}  // namespace ranking_dsl
