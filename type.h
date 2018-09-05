#pragma once

#include <stdexcept>

#define CHKERR_THROW(_cond, _msg, _later)                                   \
{                                                                           \
    if (_cond) {                                                            \
        _later;                                                             \
        throw std::runtime_error(std::string("Failed to ") + _msg);         \
    }                                                                       \
}(0)

#define CHKERR_PRINT(_cond, _msg, _later)                                   \
{                                                                           \
    if (_cond) {                                                            \
        fprintf(stderr, "Failed to %s\n", _msg);                            \
        _later;                                                             \
    }                                                                       \
}(0)
