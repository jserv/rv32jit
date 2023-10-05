#pragma once

#include "ir/qir.h"

namespace dbt::qir
{
Inst *ApplyFolder(Block *bb, Inst *ins);
}
