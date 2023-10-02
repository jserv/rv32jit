#pragma once

#include <sys/types.h>

namespace dbt
{
void *host_mmap(void *addr,
                size_t len,
                int prot,
                int flags,
                int fd,
                off_t offset);
}
