// SPDX-License-Identifier: BSD-2-Clause
// Adapted from sparsebundlefs-lib sparsebundle_iterate_bands (see source audit).
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int32_t (*sz_band_read)(void *context, uint64_t band, uint64_t offset, void *data,
                                uint32_t size);
int32_t sz_sparsebundle_read(void *context, sz_band_read read_band, uint64_t image_size,
                             uint64_t band_size, uint64_t offset, void *data, uint32_t size,
                             uint32_t *processed);
#ifdef __cplusplus
}
#endif
