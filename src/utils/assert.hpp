#pragma once

#include <cstdlib>

namespace AVCapture::Utils {

#define ASSERTM(expr, msg) assert(((void)(msg), expr))

} // namespace AVCapture::Utils