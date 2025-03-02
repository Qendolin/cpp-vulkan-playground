#pragma once

// Utility type for uniforms structs, ensures sequential write on assignment
#define MEMCPY_ASSIGNMENT(T)                                                                                           \
    T &operator=(const T &other) {                                                                                     \
        if (this != &other) {                                                                                          \
            std::memcpy(this, &other, sizeof(T)); /* NOLINT(*-undefined-memory-manipulation) */                        \
        }                                                                                                              \
        return *this;                                                                                                  \
    }

#if defined(__clang__)
#define TRIVIAL_ABI [[clang::trivial_abi]]
#elif defined(__GNUC__) || defined(__GNUG__)
#define TRIVIAL_ABI [[gnu::trivial_abi]]
#else
#define TRIVIAL_ABI
#endif
