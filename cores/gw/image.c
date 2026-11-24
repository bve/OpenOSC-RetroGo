#include "image.h"
#include "jpeg/tjpgd.h"
#include "lzma/LzmaDec.h"

enum { ScratchSize = GwWidth * GwHeight * sizeof(uint16_t), InputChunk = 512 };

static void *alloc_probs(ISzAllocPtr allocator, size_t bytes)
{
    return bytes <= ScratchSize ? allocator->Mem : NULL;
}

static void release_probs(ISzAllocPtr allocator, void *address)
{
    (void)allocator;
    (void)address;
}

static enum RgResult decompress(const RgHost *host, uint32_t end, uint8_t *image,
                                uint32_t capacity, void *scratch, uint32_t *size)
{
    static const uint8_t properties[] = {0x5d, 0x00, 0x40, 0x00, 0x00};
    const ISzAlloc allocator = {alloc_probs, release_probs, scratch};
    CLzmaDec decoder;
    LzmaDec_Construct(&decoder);
    if (LzmaDec_AllocateProbs(&decoder, properties, sizeof(properties), &allocator) != SZ_OK)
        return RG_BAD_ROM;
    decoder.dic = image;
    decoder.dicBufSize = capacity;
    LzmaDec_Init(&decoder);

    uint8_t input[InputChunk];
    for (uint32_t offset = 8; offset < end;) {
        const uint32_t count = end - offset < sizeof(input) ? end - offset : sizeof(input);
        if (host->read_rom(host->context, offset, input, count)) return RG_ROM_IO;
        SizeT used = count;
        ELzmaStatus status;
        const SRes result = LzmaDec_DecodeToDic(&decoder, capacity, input, &used,
                                               LZMA_FINISH_END, &status);
        if (result != SZ_OK || !used) return RG_BAD_ROM;
        offset += used;
        if (status == LZMA_STATUS_FINISHED_WITH_MARK && offset == end) {
            *size = decoder.dicPos;
            return RG_OK;
        }
        if (status != LZMA_STATUS_NEEDS_MORE_INPUT || used != count) return RG_BAD_ROM;
    }
    return RG_BAD_ROM;
}

enum RgResult gw_read_image(const RgHost *host, uint32_t bytes, uint8_t *image,
                            uint32_t capacity, void *scratch, uint32_t *size,
                            uint32_t *jpeg_offset)
{
    uint8_t prefix[8];
    if (bytes < sizeof(prefix)) return RG_BAD_ROM;
    if (host->read_rom(host->context, 0, prefix, sizeof(prefix))) return RG_ROM_IO;

    *jpeg_offset = bytes;
    if (!memcmp(prefix, "LZMA", 4)) {
        const uint32_t length = prefix[4] | (uint32_t)prefix[5] << 8 |
                                (uint32_t)prefix[6] << 16 | (uint32_t)prefix[7] << 24;
        if (!length || length > bytes - sizeof(prefix)) return RG_BAD_ROM;
        *jpeg_offset = sizeof(prefix) + length;
        return decompress(host, *jpeg_offset, image, capacity, scratch, size);
    }

    if (memcmp(prefix, "SM5", 3) || bytes > capacity) return RG_UNSUPPORTED_ROM;
    if (host->read_rom(host->context, 0, image, bytes)) return RG_ROM_IO;
    *size = bytes;
    return RG_OK;
}

struct JpegInput {
    const RgHost *host;
    uint16_t *background;
    uint32_t offset, end;
    unsigned x, y;
    int io_error;
};

static size_t read_jpeg(JDEC *decoder, uint8_t *out, size_t count)
{
    struct JpegInput *input = decoder->device;
    if (count > input->end - input->offset) count = input->end - input->offset;
    if (out && count && input->host->read_rom(input->host->context, input->offset, out, count)) {
        input->io_error = 1;
        return 0;
    }
    input->offset += count;
    return count;
}

static int draw_jpeg(JDEC *decoder, void *block, JRECT *rect)
{
    const struct JpegInput *input = decoder->device;
    const uint16_t *source = block;
    const unsigned width = rect->right - rect->left + 1;
    uint16_t *destination = input->background + (input->y + rect->top) * GwWidth +
                            input->x + rect->left;
    for (unsigned row = rect->top; row <= rect->bottom; ++row) {
        memcpy(destination, source, width * sizeof(*source));
        destination += GwWidth;
        source += width;
    }
    return 1;
}

enum RgResult gw_read_background(const RgHost *host, uint32_t bytes, uint32_t offset,
                                 uint16_t *background, void *scratch)
{
    if (offset >= bytes) return RG_BAD_ROM;
    struct JpegInput input = {host, background, offset, bytes, 0, 0, 0};
    JDEC decoder;
    JRESULT result = jd_prepare(&decoder, read_jpeg, scratch, ScratchSize, &input);
    if (result == JDR_OK) {
        if (decoder.width > GwWidth || decoder.height > GwHeight) return RG_UNSUPPORTED_ROM;
        input.x = (GwWidth - decoder.width) / 2;
        input.y = (GwHeight - decoder.height) / 2;
        memset(background, 0, ScratchSize);
        result = jd_decomp(&decoder, draw_jpeg, 0);
    }
    if (input.io_error) return RG_ROM_IO;
    if (result == JDR_FMT2 || result == JDR_FMT3) return RG_UNSUPPORTED_ROM;
    return result == JDR_OK ? RG_OK : RG_BAD_ROM;
}
