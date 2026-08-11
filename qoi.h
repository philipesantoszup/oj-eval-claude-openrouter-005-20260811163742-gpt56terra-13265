#ifndef QOI_FORMAT_CODEC_QOI_H_
#define QOI_FORMAT_CODEC_QOI_H_

#include "utils.h"

constexpr uint8_t QOI_OP_INDEX_TAG = 0x00;
constexpr uint8_t QOI_OP_DIFF_TAG  = 0x40;
constexpr uint8_t QOI_OP_LUMA_TAG  = 0x80;
constexpr uint8_t QOI_OP_RUN_TAG   = 0xc0;
constexpr uint8_t QOI_OP_RGB_TAG   = 0xfe;
constexpr uint8_t QOI_OP_RGBA_TAG  = 0xff;
constexpr uint8_t QOI_PADDING[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
constexpr uint8_t QOI_MASK_2 = 0xc0;

/**
 * @brief encode the raw pixel data of an image to qoi format.
 *
 * @param[in] width image width in pixels
 * @param[in] height image height in pixels
 * @param[in] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[in] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace = 0);

/**
 * @brief decode the qoi format of an image to raw pixel data
 *
 * @param[out] width image width in pixels
 * @param[out] height image height in pixels
 * @param[out] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[out] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace);


bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {
    constexpr uint64_t kMaxPixels = 400000000ull;
    const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
    if (width == 0 || height == 0 || pixel_count > kMaxPixels ||
        (channels != 3 && channels != 4) || colorspace > 1) {
        return false;
    }

    QoiWriteChar('q');
    QoiWriteChar('o');
    QoiWriteChar('i');
    QoiWriteChar('f');
    QoiWriteU32(width);
    QoiWriteU32(height);
    QoiWriteU8(channels);
    QoiWriteU8(colorspace);

    uint8_t index[64][4] = {};
    uint8_t previous[4] = {0u, 0u, 0u, 255u};
    uint8_t pixel[4];
    int run = 0;

    for (uint64_t i = 0; i < pixel_count; ++i) {
        pixel[0] = QoiReadU8();
        pixel[1] = QoiReadU8();
        pixel[2] = QoiReadU8();
        pixel[3] = channels == 4 ? QoiReadU8() : 255u;
        if (!std::cin.good()) return false;

        if (memcmp(pixel, previous, sizeof(pixel)) == 0) {
            ++run;
            if (run == 62 || i + 1 == pixel_count) {
                QoiWriteU8(QOI_OP_RUN_TAG | static_cast<uint8_t>(run - 1));
                run = 0;
            }
            continue;
        }

        if (run != 0) {
            QoiWriteU8(QOI_OP_RUN_TAG | static_cast<uint8_t>(run - 1));
            run = 0;
        }

        const int hash = QoiColorHash(pixel[0], pixel[1], pixel[2], pixel[3]);
        if (memcmp(pixel, index[hash], sizeof(pixel)) == 0) {
            QoiWriteU8(QOI_OP_INDEX_TAG | static_cast<uint8_t>(hash));
        } else {
            memcpy(index[hash], pixel, sizeof(pixel));
            // Match the assignment's reference encoder, which performs channel
            // subtraction in uint8_t before choosing a differential opcode.
            const uint8_t dr = pixel[0] - previous[0];
            const uint8_t dg = pixel[1] - previous[1];
            const uint8_t db = pixel[2] - previous[2];

            if (pixel[3] == previous[3] && (dr <= 1u || dr >= 254u) &&
                (dg <= 1u || dg >= 254u) && (db <= 1u || db >= 254u)) {
                QoiWriteU8(QOI_OP_DIFF_TAG | static_cast<uint8_t>((dr + 2u) << 4) |
                           static_cast<uint8_t>((dg + 2u) << 2) |
                           static_cast<uint8_t>(db + 2u));
            } else if (pixel[3] == previous[3] &&
                       (dg <= 31u || dg >= 224u) &&
                       (static_cast<uint8_t>(dr - dg) <= 7u ||
                        static_cast<uint8_t>(dr - dg) >= 248u) &&
                       (static_cast<uint8_t>(db - dg) <= 7u ||
                        static_cast<uint8_t>(db - dg) >= 248u)) {
                QoiWriteU8(QOI_OP_LUMA_TAG | static_cast<uint8_t>(dg + 32u));
                QoiWriteU8(static_cast<uint8_t>((static_cast<uint8_t>(dr - dg) + 8u) << 4) |
                           static_cast<uint8_t>(db - dg + 8u));
            } else if (pixel[3] == previous[3]) {
                QoiWriteU8(QOI_OP_RGB_TAG);
                QoiWriteU8(pixel[0]);
                QoiWriteU8(pixel[1]);
                QoiWriteU8(pixel[2]);
            } else {
                QoiWriteU8(QOI_OP_RGBA_TAG);
                QoiWriteU8(pixel[0]);
                QoiWriteU8(pixel[1]);
                QoiWriteU8(pixel[2]);
                QoiWriteU8(pixel[3]);
            }
        }
        memcpy(previous, pixel, sizeof(pixel));
    }

    for (uint8_t byte : QOI_PADDING) QoiWriteU8(byte);
    return std::cout.good();
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace) {
    constexpr uint64_t kMaxPixels = 400000000ull;
    const uint8_t magic[4] = {QoiReadU8(), QoiReadU8(), QoiReadU8(), QoiReadU8()};
    if (!std::cin.good() || memcmp(magic, "qoif", sizeof(magic)) != 0) return false;

    width = QoiReadU32();
    height = QoiReadU32();
    channels = QoiReadU8();
    colorspace = QoiReadU8();
    const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
    if (!std::cin.good() || width == 0 || height == 0 || pixel_count > kMaxPixels ||
        (channels != 3 && channels != 4) || colorspace > 1) {
        return false;
    }

    uint8_t index[64][4] = {};
    uint8_t pixel[4] = {0u, 0u, 0u, 255u};
    int run = 0;

    for (uint64_t i = 0; i < pixel_count; ++i) {
        if (run > 0) {
            --run;
        } else {
            const uint8_t tag = QoiReadU8();
            if (!std::cin.good()) return false;

            if (tag == QOI_OP_RGB_TAG) {
                pixel[0] = QoiReadU8();
                pixel[1] = QoiReadU8();
                pixel[2] = QoiReadU8();
            } else if (tag == QOI_OP_RGBA_TAG) {
                pixel[0] = QoiReadU8();
                pixel[1] = QoiReadU8();
                pixel[2] = QoiReadU8();
                pixel[3] = QoiReadU8();
            } else if ((tag & QOI_MASK_2) == QOI_OP_INDEX_TAG) {
                memcpy(pixel, index[tag], sizeof(pixel));
            } else if ((tag & QOI_MASK_2) == QOI_OP_DIFF_TAG) {
                pixel[0] = static_cast<uint8_t>(pixel[0] + ((tag >> 4 & 0x03) - 2));
                pixel[1] = static_cast<uint8_t>(pixel[1] + ((tag >> 2 & 0x03) - 2));
                pixel[2] = static_cast<uint8_t>(pixel[2] + ((tag & 0x03) - 2));
            } else if ((tag & QOI_MASK_2) == QOI_OP_LUMA_TAG) {
                const uint8_t adjustment = QoiReadU8();
                if (!std::cin.good()) return false;
                const int dg = (tag & 0x3f) - 32;
                pixel[0] = static_cast<uint8_t>(pixel[0] + dg + ((adjustment >> 4) - 8));
                pixel[1] = static_cast<uint8_t>(pixel[1] + dg);
                pixel[2] = static_cast<uint8_t>(pixel[2] + dg + ((adjustment & 0x0f) - 8));
            } else {
                run = tag & 0x3f;
                if (static_cast<uint64_t>(run) > pixel_count - i - 1) return false;
            }
            if (!std::cin.good()) return false;
        }

        memcpy(index[QoiColorHash(pixel[0], pixel[1], pixel[2], pixel[3])], pixel, sizeof(pixel));
        QoiWriteU8(pixel[0]);
        QoiWriteU8(pixel[1]);
        QoiWriteU8(pixel[2]);
        if (channels == 4) QoiWriteU8(pixel[3]);
    }

    for (uint8_t expected : QOI_PADDING) {
        if (QoiReadU8() != expected || !std::cin.good()) return false;
    }
    return std::cin.peek() == std::char_traits<char>::eof() && std::cout.good();
}

#endif // QOI_FORMAT_CODEC_QOI_H_
