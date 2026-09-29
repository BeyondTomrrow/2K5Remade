#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ARCHIVE_ZERO "original/vc_53450030/0"
#define OUTPUT_DIR "analysis/textures"

typedef struct {
    uint32_t identifier;
    uint32_t flags;
    uint32_t sector_address;
} ArchiveRecord;

void scan_hitx_blocks() {
    FILE *f = fopen(ARCHIVE_ZERO, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open archive 0\n");
        return;
    }

    // Read archive header to locate entry table
    uint32_t entry_count;
    fread(&entry_count, sizeof(uint32_t), 1, f);
    fseek(f, 156, SEEK_SET);

    ArchiveRecord *records = malloc(entry_count * sizeof(ArchiveRecord));
    for (uint32_t i = 0; i < entry_count; i++) {
        fread(&records[i].identifier, sizeof(uint32_t), 1, f);
        fread(&records[i].flags, sizeof(uint32_t), 1, f);
        fread(&records[i].sector_address, sizeof(uint32_t), 1, f);
    }

    // Scan record 0 payload for HITX signatures
    ArchiveRecord rec0 = records[0];
    uint32_t rec0_offset = rec0.sector_address * 2048;
    uint32_t rec0_size = rec0.flags & 0x0FFFFFFF;

    uint8_t *buffer = malloc(rec0_size);
    fseek(f, rec0_offset, SEEK_SET);
    fread(buffer, 1, rec0_size, f);

    uint32_t cursor = 0;
    int texture_index = 0;
    char outpath[512];

    while (cursor < rec0_size - 4) {
        if (memcmp(buffer + cursor, "HITX", 4) == 0) {
            // Validate block and extract details
            snprintf(outpath, sizeof(outpath), "%s/texture_%04d_%08X.raw", OUTPUT_DIR, texture_index++, rec0.identifier);
            FILE *out = fopen(outpath, "wb");
            if (out) {
                // Approximate generic block write or write payload based on header size field
                uint32_t block_size = *(uint32_t *)(buffer + cursor + 4);
                if (block_size == 0 || cursor + block_size > rec0_size) block_size = 0x404A0; // fallback bounding
                fwrite(buffer + cursor, 1, block_size, out);
                fclose(out);
            }
        }
        cursor += 4;
    }

    free(buffer);
    free(records);
    fclose(f);
}

int main(void) {
    scan_hitx_blocks();
    return 0;
}