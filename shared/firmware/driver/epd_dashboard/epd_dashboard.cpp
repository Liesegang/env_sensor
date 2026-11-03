/* SPDX-License-Identifier: MIT */
/* Dashboard renderer adapted from the Card e-paper prototype. */
#include <epd_dashboard.h>
#include <epd3in6e.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint16_t kLandscapeWidth = EPD3IN6E_HEIGHT;
constexpr uint16_t kLandscapeHeight = EPD3IN6E_WIDTH;
constexpr uint16_t kPanelWidth = EPD3IN6E_WIDTH;
constexpr size_t kFrameSize = EPD3IN6E_FRAME_BYTES;
constexpr size_t kSampleCount = EPD_DASHBOARD_SAMPLES;
constexpr size_t kSensorCount = EPD_DASHBOARD_SERIES;
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr uint8_t kBlack = EPD3IN6E_BLACK;
constexpr uint8_t kWhite = EPD3IN6E_WHITE;
constexpr uint8_t kYellow = EPD3IN6E_YELLOW;
constexpr uint8_t kRed = EPD3IN6E_RED;
constexpr uint8_t kBlue = EPD3IN6E_BLUE;
constexpr uint8_t kGreen = EPD3IN6E_GREEN;

// Five columns, seven rows. ASCII 0x20 (space) through 0x5A ('Z').
const uint8_t kFont5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
};

class Canvas {
public:
    explicit Canvas(uint8_t* pixels) : pixels_(pixels) {}

    void clear(uint8_t color) {
        memset(pixels_, static_cast<uint8_t>((color << 4) | color), kFrameSize);
    }

    void pixel(int16_t x, int16_t y, uint8_t color) {
        if (x < 0 || y < 0 || x >= kLandscapeWidth || y >= kLandscapeHeight) {
            return;
        }

        // Rotate the logical 600x400 canvas clockwise into panel RAM (400x600).
        const uint16_t panelX = kLandscapeHeight - 1 - y;
        const uint16_t panelY = x;
        const size_t index = panelY * (kPanelWidth / 2) + panelX / 2;
        if ((panelX & 1U) == 0) {
            pixels_[index] = static_cast<uint8_t>((pixels_[index] & 0x0F) | (color << 4));
        } else {
            pixels_[index] = static_cast<uint8_t>((pixels_[index] & 0xF0) | color);
        }
    }

    void fillRect(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color) {
        for (int16_t py = y; py < y + height; ++py) {
            for (int16_t px = x; px < x + width; ++px) {
                pixel(px, py, color);
            }
        }
    }

    void rect(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color) {
        hLine(x, y, width, color);
        hLine(x, y + height - 1, width, color);
        vLine(x, y, height, color);
        vLine(x + width - 1, y, height, color);
    }

    void hLine(int16_t x, int16_t y, int16_t width, uint8_t color) {
        for (int16_t px = x; px < x + width; ++px) {
            pixel(px, y, color);
        }
    }

    void vLine(int16_t x, int16_t y, int16_t height, uint8_t color) {
        for (int16_t py = y; py < y + height; ++py) {
            pixel(x, py, color);
        }
    }

    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color,
              uint8_t thickness = 1) {
        const int16_t dx = abs(x1 - x0);
        const int16_t sx = x0 < x1 ? 1 : -1;
        const int16_t dy = -abs(y1 - y0);
        const int16_t sy = y0 < y1 ? 1 : -1;
        int16_t error = dx + dy;
        const int16_t radius = thickness / 2;

        while (true) {
            fillRect(x0 - radius, y0 - radius, thickness, thickness, color);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int16_t doubled = 2 * error;
            if (doubled >= dy) {
                error += dy;
                x0 += sx;
            }
            if (doubled <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }

    // A continuous two-color line. Physical pixels alternate by horizontal
    // screen position, so the pair optically blends at normal viewing distance
    // (e.g. red+blue appears purple) while the trace itself remains unbroken.
    void mixedLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                   uint8_t colorA, uint8_t colorB, uint8_t thickness = 2) {
        const int16_t dx = abs(x1 - x0);
        const int16_t sx = x0 < x1 ? 1 : -1;
        const int16_t dy = -abs(y1 - y0);
        const int16_t sy = y0 < y1 ? 1 : -1;
        int16_t error = dx + dy;
        const int16_t radius = thickness / 2;

        while (true) {
            for (uint8_t oy = 0; oy < thickness; ++oy) {
                for (uint8_t ox = 0; ox < thickness; ++ox) {
                    const int16_t px = x0 - radius + ox;
                    const int16_t py = y0 - radius + oy;
                    pixel(px, py, (px & 1) == 0 ? colorA : colorB);
                }
            }
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int16_t doubled = 2 * error;
            if (doubled >= dy) {
                error += dy;
                x0 += sx;
            }
            if (doubled <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }

    void fillMixedRect(int16_t x, int16_t y, int16_t width, int16_t height,
                       uint8_t colorA, uint8_t colorB) {
        for (int16_t py = y; py < y + height; ++py) {
            for (int16_t px = x; px < x + width; ++px) {
                pixel(px, py, (px & 1) == 0 ? colorA : colorB);
            }
        }
    }

    int16_t textWidth(const char* text, uint8_t scale = 1) const {
        return static_cast<int16_t>(strlen(text) * 6 * scale);
    }

    void text(int16_t x, int16_t y, const char* value, uint8_t color,
              uint8_t scale = 1) {
        while (*value != '\0') {
            drawChar(x, y, *value, color, scale);
            x += 6 * scale;
            ++value;
        }
    }

    void centeredText(int16_t x, int16_t y, int16_t width, const char* value,
                      uint8_t color, uint8_t scale = 1) {
        text(x + (width - textWidth(value, scale)) / 2, y, value, color, scale);
    }

    int16_t textWidth150(const char* value) const {
        return static_cast<int16_t>(strlen(value) * 9);
    }

    // Draw the 5x7 font at exactly 150% using nearest-neighbor expansion.
    // Each 6x7 character cell becomes 9x11 pixels.
    void text150(int16_t x, int16_t y, const char* value, uint8_t color) {
        while (*value != '\0') {
            drawChar150(x, y, *value, color);
            x += 9;
            ++value;
        }
    }

    void centeredText150(int16_t x, int16_t y, int16_t width,
                         const char* value, uint8_t color) {
        text150(x + (width - textWidth150(value)) / 2, y, value, color);
    }

private:
    void drawChar150(int16_t x, int16_t y, char character, uint8_t color) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        if (character < 0x20 || character > 0x5A) {
            character = '?';
        }
        const uint8_t index = static_cast<uint8_t>(character - 0x20);
        for (uint8_t destinationX = 0; destinationX < 8; ++destinationX) {
            const uint8_t sourceX = destinationX * 2 / 3;
            const uint8_t bits = kFont5x7[index][sourceX];
            for (uint8_t destinationY = 0; destinationY < 11; ++destinationY) {
                const uint8_t sourceY = destinationY * 2 / 3;
                if ((bits & (1U << sourceY)) != 0) {
                    pixel(x + destinationX, y + destinationY, color);
                }
            }
        }
    }

    void drawChar(int16_t x, int16_t y, char character, uint8_t color,
                  uint8_t scale) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        if (character < 0x20 || character > 0x5A) {
            character = '?';
        }
        const uint8_t index = static_cast<uint8_t>(character - 0x20);
        for (uint8_t column = 0; column < 5; ++column) {
            const uint8_t bits = kFont5x7[index][column];
            for (uint8_t row = 0; row < 7; ++row) {
                if ((bits & (1U << row)) != 0) {
                    fillRect(x + column * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }

    uint8_t* pixels_;
};

enum class Status : uint8_t {
    VeryLow = 0,
    Low = 1,
    Mid = 2,
    High = 3,
    VeryHigh = 4
};

using SensorSeries = epd_dashboard_series;

// Demo data stays off the caller stack; production uses its own measured logs.
SensorSeries sensorLogs[kSensorCount];

uint32_t rngState = 0xE6C32026;

float noise(float amplitude) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    const float unit = static_cast<float>(rngState & 0xFFFFU) / 32767.5f - 1.0f;
    return unit * amplitude;
}

float gaussian(float value, float center, float spread) {
    const float normalized = (value - center) / spread;
    return expf(-(normalized * normalized));
}

void setSample(SensorSeries& sensor, size_t index, float value) {
    const float clipped = fminf(sensor.plotHigh, fmaxf(sensor.plotLow, value));
    const float normalized =
        (clipped - sensor.plotLow) / (sensor.plotHigh - sensor.plotLow);
    sensor.values[index] = static_cast<uint16_t>(lroundf(normalized * 65535.0f));
}

float sampleValue(const SensorSeries& sensor, size_t index) {
    const float normalized = static_cast<float>(sensor.values[index]) / 65535.0f;
    const float value = sensor.plotLow + normalized * (sensor.plotHigh - sensor.plotLow);
    return fminf(sensor.plotHigh, fmaxf(sensor.plotLow, value));
}

void generateFakeLogs(SensorSeries (&sensors)[kSensorCount]) {
    for (auto& sensor : sensors) {
        sensor.has_validity_map = 0;
        sensor.has_current_value = 0;
    }
    sensors[0].name = "TEMP";
    sensors[0].unit = "C";
    sensors[0].lineColor = kRed;
    sensors[0].mixColor = kRed;
    sensors[0].plotLow = 18.0f;
    sensors[0].plotHigh = 32.0f;
    sensors[0].veryLowHigh = 19.0f;
    sensors[0].lowHigh = 20.0f;
    sensors[0].midHigh = 28.0f;
    sensors[0].highHigh = 30.0f;
    sensors[0].decimals = 1;

    sensors[1].name = "HUM";
    sensors[1].unit = "%";
    sensors[1].lineColor = kBlue;
    sensors[1].mixColor = kBlue;
    sensors[1].plotLow = 35.0f;
    sensors[1].plotHigh = 85.0f;
    sensors[1].veryLowHigh = 37.5f;
    sensors[1].lowHigh = 40.0f;
    sensors[1].midHigh = 65.0f;
    sensors[1].highHigh = 75.0f;
    sensors[1].decimals = 0;

    sensors[2].name = "PRESS";
    sensors[2].unit = "HPA";
    sensors[2].lineColor = kGreen;
    sensors[2].mixColor = kGreen;
    sensors[2].plotLow = 1006.0f;
    sensors[2].plotHigh = 1018.0f;
    sensors[2].veryLowHigh = 1007.5f;
    sensors[2].lowHigh = 1009.0f;
    sensors[2].midHigh = 1016.0f;
    sensors[2].highHigh = 1017.5f;
    sensors[2].decimals = 0;

    sensors[3].name = "CO2";
    sensors[3].unit = "PPM";
    sensors[3].lineColor = kBlack;
    sensors[3].mixColor = kBlack;
    sensors[3].plotLow = 400.0f;
    sensors[3].plotHigh = 1700.0f;
    sensors[3].veryLowHigh = 500.0f;
    sensors[3].lowHigh = 600.0f;
    sensors[3].midHigh = 1000.0f;
    sensors[3].highHigh = 1500.0f;
    sensors[3].decimals = 0;

    sensors[4].name = "VOC";
    sensors[4].unit = "PPB";
    sensors[4].lineColor = kYellow;
    sensors[4].mixColor = kRed;  // yellow + red = orange
    sensors[4].plotLow = 0.0f;
    sensors[4].plotHigh = 1000.0f;
    sensors[4].veryLowHigh = 50.0f;
    sensors[4].lowHigh = 100.0f;
    sensors[4].midHigh = 400.0f;
    sensors[4].highHigh = 800.0f;
    sensors[4].decimals = 0;

    sensors[5].name = "HCHO";
    sensors[5].unit = "PPM";
    sensors[5].lineColor = kRed;
    sensors[5].mixColor = kBlue;  // red + blue = purple
    sensors[5].plotLow = 0.0f;
    sensors[5].plotHigh = 0.15f;
    sensors[5].veryLowHigh = 0.01f;
    sensors[5].lowHigh = 0.02f;
    sensors[5].midHigh = 0.08f;
    sensors[5].highHigh = 0.12f;
    sensors[5].decimals = 2;

    sensors[6].name = "NOX";
    sensors[6].unit = "PPB";
    sensors[6].lineColor = kBlue;
    sensors[6].mixColor = kGreen;  // blue + green = cyan
    sensors[6].plotLow = 0.0f;
    sensors[6].plotHigh = 350.0f;
    sensors[6].veryLowHigh = 10.0f;
    sensors[6].lowHigh = 25.0f;
    sensors[6].midHigh = 150.0f;
    sensors[6].highHigh = 300.0f;
    sensors[6].decimals = 0;

    sensors[7].name = "LUX";
    sensors[7].unit = "LX";
    sensors[7].lineColor = kGreen;
    sensors[7].mixColor = kYellow;  // green + yellow = lime
    sensors[7].plotLow = 0.0f;
    sensors[7].plotHigh = 70000.0f;
    sensors[7].veryLowHigh = 100.0f;
    sensors[7].lowHigh = 1000.0f;
    sensors[7].midHigh = 30000.0f;
    sensors[7].highHigh = 60000.0f;
    sensors[7].decimals = 0;

    for (size_t index = 0; index < kSampleCount; ++index) {
        const float hour = static_cast<float>(index) * 24.0f / kSampleCount;
        setSample(sensors[0], index,
                  21.9f + 9.0f * gaussian(hour, 23.5f, 4.5f) +
                      1.2f * gaussian(hour, 8.0f, 4.0f) + noise(0.18f));
        setSample(sensors[1], index,
                  49.0f + 25.0f * gaussian(hour, 22.0f, 5.5f) +
                      8.0f * gaussian(hour, 4.0f, 3.5f) + noise(0.8f));
        setSample(sensors[2], index,
                  1012.0f + 3.8f * sinf((hour + 2.0f) * kTwoPi / 18.0f) +
                      noise(0.25f));
        setSample(sensors[3], index,
                  480.0f + 1120.0f * gaussian(hour, 8.0f, 2.4f) +
                      760.0f * gaussian(hour, 23.0f, 3.2f) + noise(15.0f));
        setSample(sensors[4], index,
                  120.0f + 550.0f * gaussian(hour, 7.5f, 2.8f) +
                      360.0f * gaussian(hour, 22.5f, 3.5f) + noise(18.0f));
        setSample(sensors[5], index,
                  0.025f + 0.085f * gaussian(hour, 14.0f, 4.0f) +
                      0.070f * gaussian(hour, 23.0f, 3.0f) + noise(0.0025f));
        setSample(sensors[6], index,
                  35.0f + 270.0f * gaussian(hour, 8.0f, 2.0f) +
                      145.0f * gaussian(hour, 23.0f, 3.0f) + noise(7.0f));
        setSample(sensors[7], index,
                  fmaxf(0.0f,
                      50.0f + 65000.0f * gaussian(hour, 12.5f, 4.5f) +
                          noise(350.0f)));
    }
}

Status statusFor(const SensorSeries& sensor, float value) {
    if (value >= sensor.highHigh) {
        return Status::VeryHigh;
    }
    if (value >= sensor.midHigh) {
        return Status::High;
    }
    if (value < sensor.veryLowHigh) {
        return Status::VeryLow;
    }
    if (value < sensor.lowHigh) {
        return Status::Low;
    }
    return Status::Mid;
}

uint8_t statusColor(Status status) {
    switch (status) {
        case Status::VeryHigh:
            return kRed;
        case Status::High:
            return kYellow;
        case Status::Low:
            return kBlue;
        case Status::VeryLow:
            return kBlack;
        default:
            return kGreen;
    }
}

const char* statusName(Status status) {
    switch (status) {
        case Status::VeryHigh:
            return "VHIGH";
        case Status::High:
            return "HIGH";
        case Status::Low:
            return "LOW";
        case Status::VeryLow:
            return "VLOW";
        default:
            return "MID";
    }
}

uint8_t statusTextColor(Status status) {
    return status == Status::High ? kBlack : kWhite;
}

bool samplePresent(const SensorSeries& sensor, size_t index) {
    return sensor.has_validity_map == 0 ||
           (sensor.valid_bits[index / 8] & (1U << (index % 8))) != 0;
}

void drawStatusCard(Canvas& canvas, int16_t x, int16_t y,
                    const SensorSeries& sensor) {
    constexpr int16_t width = 140;
    constexpr int16_t height = 40;
    const bool present = samplePresent(sensor, kSampleCount - 1);
    const float current = !present ? 0.0f :
        (sensor.has_current_value != 0 ? sensor.current_value :
         sampleValue(sensor, kSampleCount - 1));
    const Status status = present ? statusFor(sensor, current) : Status::Mid;
    const uint8_t fill = present ? statusColor(status) : kWhite;

    canvas.fillRect(x, y, width, height, kWhite);
    canvas.rect(x, y, width, height, kBlack);
    canvas.fillMixedRect(x + 1, y + 1, 5, height - 2,
                         sensor.lineColor, sensor.mixColor);
    canvas.text150(x + 10, y + 3, sensor.name, kBlack);

    char value[20];
    if (!present) {
        snprintf(value, sizeof(value), "--");
    } else if (sensor.decimals == 2) {
        snprintf(value, sizeof(value), "%.2F %s", current, sensor.unit);
    } else if (sensor.decimals == 1) {
        snprintf(value, sizeof(value), "%.1F %s", current, sensor.unit);
    } else {
        snprintf(value, sizeof(value), "%.0F %s", current, sensor.unit);
    }
    canvas.text150(x + 10, y + 21, value, kBlack);

    canvas.fillRect(x + 88, y + 1, width - 89, height - 2, fill);
    canvas.centeredText150(x + 88, y + 14, width - 89,
                           present ? statusName(status) : "NA",
                           present ? statusTextColor(status) : kBlack);
}

int16_t plotX(size_t index) {
    constexpr int16_t left = 43;
    constexpr int16_t right = 590;
    return static_cast<int16_t>(left + index * (right - left) / (kSampleCount - 1));
}

int16_t plotY(const SensorSeries& sensor, float value) {
    constexpr int16_t top = 98;
    constexpr int16_t bottom = 373;
    const float clipped = fminf(sensor.plotHigh, fmaxf(sensor.plotLow, value));
    const float normalized = (clipped - sensor.plotLow) / (sensor.plotHigh - sensor.plotLow);
    return static_cast<int16_t>(lroundf(bottom - normalized * (bottom - top)));
}

void drawSeries(Canvas& canvas, const SensorSeries& sensor, uint8_t thickness) {
    bool previousPresent = false;
    int16_t previousX = 0;
    int16_t previousY = 0;
    for (size_t index = 0; index < kSampleCount; ++index) {
        if (!samplePresent(sensor, index)) {
            previousPresent = false;
            continue;
        }
        const int16_t x = plotX(index);
        const int16_t y = plotY(sensor, sampleValue(sensor, index));
        if (previousPresent) {
            canvas.mixedLine(previousX, previousY, x, y, sensor.lineColor,
                             sensor.mixColor, thickness);
        } else if (index == kSampleCount - 1 || !samplePresent(sensor, index + 1)) {
            // A lone real sample is visible without joining across a gap.
            canvas.mixedLine(x, y, x, y, sensor.lineColor,
                             sensor.mixColor, thickness);
        }
        previousX = x;
        previousY = y;
        previousPresent = true;
    }
}

void drawDashboard(Canvas& canvas, const SensorSeries* sensors,
                   const char* caption, const char* rightLabel) {
    canvas.clear(kWhite);

    const int16_t cardX[] = {10, 157, 304, 451};
    for (uint8_t index = 0; index < kSensorCount; ++index) {
        drawStatusCard(canvas, cardX[index % 4], index < 4 ? 5 : 49,
                       sensors[index]);
    }

    constexpr int16_t plotLeft = 43;
    constexpr int16_t plotTop = 98;
    constexpr int16_t plotRight = 590;
    constexpr int16_t plotBottom = 373;
    canvas.rect(plotLeft, plotTop, plotRight - plotLeft + 1,
                plotBottom - plotTop + 1, kBlack);

    const char* yLabels[] = {"100", "75", "50", "25", "0"};
    for (uint8_t row = 0; row < 5; ++row) {
        const int16_t y = plotTop + row * (plotBottom - plotTop) / 4;
        canvas.text(9, y - 3, yLabels[row], kBlack);
        for (int16_t x = plotLeft + 1; x < plotRight; x += 8) {
            canvas.hLine(x, y, 4, kBlack);
        }
    }

    const size_t tickSamples[] = {0, (kSampleCount - 1) / 4,
        (kSampleCount - 1) / 2, (kSampleCount - 1) * 3 / 4, kSampleCount - 1};
    const char* xLabels[] = {"-24H", "-18", "-12", "-6", rightLabel ? rightLabel : "NOW"};
    for (uint8_t tick = 0; tick < 5; ++tick) {
        const int16_t x = plotX(tickSamples[tick]);
        canvas.vLine(x, plotBottom - 2, 4, kBlack);
        canvas.text(x - canvas.textWidth(xLabels[tick]) / 2, plotBottom + 6,
                    xLabels[tick], kBlack);
    }

    // One graph, eight continuous overlaid series. Added sensors use optical
    // two-color mixing: orange, purple, cyan, and lime.
    const uint8_t drawOrder[] = {7, 2, 5, 0, 6, 1, 4, 3};
    for (const uint8_t index : drawOrder) {
        drawSeries(canvas, sensors[index], index == 3 ? 3 : 2);
    }
    if (caption) { canvas.text(43, 392, caption, kBlack); }
}

bool validColor(uint8_t color) {
    return color == kBlack || color == kWhite || color == kYellow ||
           color == kRed || color == kBlue || color == kGreen;
}

bool validSeries(const SensorSeries& sensor) {
    if (sensor.name == nullptr || sensor.unit == nullptr ||
        strnlen(sensor.name, 9) == 0 || strnlen(sensor.name, 9) > 8 ||
        strnlen(sensor.unit, 7) > 6 || sensor.decimals > 2 ||
        !validColor(sensor.lineColor) || !validColor(sensor.mixColor) ||
        (sensor.has_current_value != 0 && !isfinite(sensor.current_value))) {
        return false;
    }
    if (!isfinite(sensor.plotLow) || !isfinite(sensor.plotHigh) ||
        sensor.plotLow >= sensor.plotHigh ||
        !isfinite(sensor.plotHigh - sensor.plotLow) ||
        !isfinite(sensor.veryLowHigh) || !isfinite(sensor.lowHigh) ||
        !isfinite(sensor.midHigh) || !isfinite(sensor.highHigh)) {
        return false;
    }
    return sensor.veryLowHigh <= sensor.lowHigh &&
           sensor.lowHigh <= sensor.midHigh &&
           sensor.midHigh <= sensor.highHigh;
}
} // namespace

extern "C" int epd_dashboard_render(uint8_t* frame, size_t length,
                                   const struct epd_dashboard_series* series,
                                   size_t count) {
    return epd_dashboard_render_labeled(frame, length, series, count, nullptr, nullptr);
}

extern "C" int epd_dashboard_render_labeled(uint8_t* frame, size_t length,
                                   const struct epd_dashboard_series* series,
                                   size_t count, const char* caption,
                                   const char* rightLabel) {
    if (frame == nullptr || length != kFrameSize || series == nullptr ||
        count != kSensorCount || (caption && strnlen(caption, 86) > 85) ||
        (rightLabel && strnlen(rightLabel, 5) > 4)) {
        return -EINVAL;
    }
    for (size_t index = 0; index < count; ++index) {
        if (!validSeries(series[index])) {
            return -EINVAL;
        }
    }
    Canvas canvas(frame);
    drawDashboard(canvas, series, caption, rightLabel);
    return 0;
}

extern "C" int epd_dashboard_demo_render(uint8_t* frame, size_t length) {
    if (frame == nullptr || length != kFrameSize) {
        return -EINVAL;
    }
    rngState = 0xE6C32026;
    generateFakeLogs(sensorLogs);
    return epd_dashboard_render(frame, length, sensorLogs, kSensorCount);
}

extern "C" int epd_dashboard_pairing_render(uint8_t* frame, size_t length,
                                            uint32_t passkey) {
    if (frame == nullptr || length != kFrameSize || passkey > 999999U) {
        return -EINVAL;
    }
    Canvas canvas(frame);
    canvas.clear(kWhite);
    canvas.rect(16, 16, 568, 368, kBlack);
    canvas.centeredText(0, 52, 600, "BLUETOOTH PAIRING", kBlack, 3);
    char digits[7];
    snprintf(digits, sizeof(digits), "%06u", static_cast<unsigned int>(passkey));
    canvas.centeredText(0, 144, 600, digits, kBlack, 10);
    canvas.centeredText(0, 252, 600, "ENTER THIS CODE ON YOUR IPHONE", kBlack, 2);
    canvas.centeredText(0, 296, 600, "OPEN THE CARD SENSOR APP", kBlack, 2);
    canvas.centeredText(0, 342, 600, "NEW CODE FOR EACH PAIRING ATTEMPT", kBlack, 1);
    return 0;
}
