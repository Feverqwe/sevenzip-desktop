/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2012-2016 Tor Arne Vestbø. All rights reserved.
 * Copyright (c) 2018 Jief Luce.
 * Adapted from sparsebundle_iterate_bands: portable callback I/O boundary,
 * bounded unsigned offsets, error propagation, no process or global state.
 * Full notice: third_party/sparsebundlefs/LICENSE.
 */
#include "bandmap.h"

int32_t sz_sparsebundle_read(void *context, sz_band_read read_band, uint64_t image_size,
                             uint64_t band_size, uint64_t offset, void *data, uint32_t size,
                             uint32_t *processed) {
    uint32_t bytes_read = 0;
    *processed = 0;
    if (offset >= image_size || !size)
        return 0;
    if ((uint64_t)size > image_size - offset)
        size = (uint32_t)(image_size - offset);
    while (bytes_read < size) {
        const uint64_t band_number = (offset + bytes_read) / band_size;
        const uint64_t band_offset = (offset + bytes_read) % band_size;
        uint32_t to_read = size - bytes_read;
        int32_t result;
        if ((uint64_t)to_read > band_size - band_offset)
            to_read = (uint32_t)(band_size - band_offset);
        result =
            read_band(context, band_number, band_offset, (uint8_t *)data + bytes_read, to_read);
        if (result != 0)
            return result;
        bytes_read += to_read;
        *processed = bytes_read;
    }
    return 0;
}
