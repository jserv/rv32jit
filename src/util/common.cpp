#include <cstdio>
#include <cstdlib>

#include "util/common.h"

namespace dbt
{
void __attribute__((noreturn)) Panic(char const *msg)
{
    fprintf(stderr, "Panic: %s\n", msg);
    abort();
}

void __attribute__((noreturn)) Panic(std::string const &msg)
{
    fprintf(stderr, "Panic: %s\n", msg.c_str());
    abort();
}
}  // namespace dbt
