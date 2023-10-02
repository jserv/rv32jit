#pragma once

#include "qmc/qir.h"

namespace dbt::qir
{
Inst *ApplyFolder(Block *bb, Inst *ins);
}
