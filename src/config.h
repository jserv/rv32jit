#pragma once

#if !(__has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__))
#define CONFIG_ZERO_MMU_BASE 1
#else
#define CONFIG_ZERO_MMU_BASE 0
#endif
