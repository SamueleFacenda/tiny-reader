// GxEPD2 panel class for the JD79661 / EK79029 revision of the Elecrow CrowPanel 2.13"
// e-paper module. The other revision is an SSD1680Z panel, driven by GxEPD2's own
// GxEPD2_213_GDEY0213B74: same pins and same 122x250 visible area, but a different command
// set and BUSY active low rather than high.
//
// Reference sources:
//  - Elecrow's driver for this module, example/arduino-v1.2/main/EPD_Init.cpp in
//    CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250. _InitDisplay's register
//    sequence and the ten waveform tables are copied from it verbatim.
//  - GxEPD2's epd/GxEPD2_154_M09 (GDEW0154M09, JD79653A): the structural template. Same
//    JD796xx family, also b/w, also downloads its waveform into 0x20-0x24.
//  - GxEPD2's epd4c/GxEPD2_213c_GDEY0213F51: the same controller on the 4-colour panel, so
//    its image path does not apply, but it is the authority on the controller's opcodes:
//    0x83 partial window, 0x02 + 0x00 power off, 0x07 + 0xA5 deep sleep, BUSY active low.
//
// Library: https://github.com/ZinggJM/GxEPD2

#ifndef _GxEPD2_213_JD79661_H_
#define _GxEPD2_213_JD79661_H_

#include <GxEPD2_EPD.h>

class GxEPD2_213_JD79661 : public GxEPD2_EPD
{
  public:
    // attributes
    static const uint16_t WIDTH = 128;
    // 122 of the 128 source lines are visible, the same as the SSD1680 revision, which is
    // what makes Ui.cpp's computeLayout give identical layouts on both panels.
    static const uint16_t WIDTH_VISIBLE = 122;
    static const uint16_t HEIGHT = 250;
    // Load-bearing, not descriptive: GDE0213B1 is the one value GxEPD2_BW tests for
    // (GxEPD2_BW.h:249) and it sets _reverse, which this panel needs because its gate axis
    // is addressed from the end opposite to GxEPD2's assumption. It must be done here and
    // not by flipping the PSR gate scan: _reverse also applies to _pw_y (:419), drawPixel
    // (:294) and the refresh window (:384), which a PSR flip leaves addressing the wrong
    // end of the panel.
    static const GxEPD2::Panel panel = GxEPD2::GDE0213B1;
    static const bool hasColor = false;
    static const bool hasPartialUpdate = true;
    static const bool hasFastPartialUpdate = true;
    static const uint16_t power_on_time = 100; // ms
    static const uint16_t power_off_time = 150; // ms
    static const uint16_t full_refresh_time = 1700; // ms, GC waveform
    static const uint16_t partial_refresh_time = 400; // ms, DU waveform is labelled 300ms
    // constructor
    GxEPD2_213_JD79661(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    // methods (virtual)
    //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
    void clearScreen(uint8_t value = 0xFF); // init controller memory and screen (default white)
    void writeScreenBuffer(uint8_t value = 0xFF); // init controller memory (default white)
    void writeScreenBufferAgain(uint8_t value = 0xFF); // init previous buffer controller memory (default white)
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePartToPrevious(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write to controller memory, with screen refresh; x and w should be multiple of 8
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    //for differential update: set current and previous buffers equal (for fast partial update to work correctly)
    void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                             int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void refresh(bool partial_update_mode = false); // screen refresh from controller memory to full screen
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h); // screen refresh from controller memory, partial screen
    void powerOff(); // turns off generation of panel driving voltages, avoids screen fading over time
    void hibernate(); // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)
  private:
    void _writeScreenBuffer(uint8_t command, uint8_t value);
    void _writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool partial_mode = false);
    void _writeLut(bool partial);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Init_Full();
    void _Init_Part();
    void _Update_Full();
    void _Update_Part();
  private:
    // Alternates which waveform 0x22 and 0x23 receive on every LUT download. This is VCOM
    // balancing, from Elecrow's lut_flag: do not simplify it away.
    uint8_t _lut_flag;
    static const unsigned char lut_20_gc[];
    static const unsigned char lut_21_gc[];
    static const unsigned char lut_22_gc[];
    static const unsigned char lut_23_gc[];
    static const unsigned char lut_24_gc[];
    static const unsigned char lut_20_du[];
    static const unsigned char lut_21_du[];
    static const unsigned char lut_22_du[];
    static const unsigned char lut_23_du[];
    static const unsigned char lut_24_du[];
};

#endif
