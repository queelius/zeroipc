#include "zeroipc.h"

/* Get error string */
const char* zeroipc_error_string(zeroipc_error_t error) {
    switch (error) {
        case ZEROIPC_OK:
            return "Success";
        case ZEROIPC_ERROR_OPEN:
            return "Failed to open shared memory";
        case ZEROIPC_ERROR_MMAP:
            return "Failed to map memory";
        case ZEROIPC_ERROR_SIZE:
            return "Invalid size or insufficient space";
        case ZEROIPC_ERROR_NOT_FOUND:
            return "Entry not found";
        case ZEROIPC_ERROR_TABLE_FULL:
            return "Table is full";
        case ZEROIPC_ERROR_NAME_TOO_LONG:
            return "Name exceeds maximum length";
        case ZEROIPC_ERROR_INVALID_MAGIC:
            return "Invalid magic number";
        case ZEROIPC_ERROR_VERSION_MISMATCH:
            return "Version mismatch";
        case ZEROIPC_ERROR_ALREADY_EXISTS:
            return "Entry already exists";
        default:
            return "Unknown error";
    }
}