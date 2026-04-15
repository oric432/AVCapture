#pragma once

#include <cstdlib>

namespace VSCapture::Utils {

#define ASSERTM(expr, msg) assert(((void)(msg), expr))

} // namespace VSCapture::Utils