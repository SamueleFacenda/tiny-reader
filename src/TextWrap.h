#pragma once

#include <stddef.h>
#include <stdint.h>

// Word wrapping for the reading view, free of Arduino and GxEPD2 so page boundaries can
// be computed without touching the panel, and so this compiles on a host.
namespace TextWrap {

// One visual line, as a range into the source text. No member initializers: the sketch
// is built as gnu++11, where those stop this being an aggregate and break Line{start, len}.
struct Line {
  size_t start;
  size_t len;
};

inline bool isBlank(char c) {
  return c == ' ' || c == '\t' || c == '\r';
}

// Lays out at most maxLines lines of at most maxWidth pixels, returning the bytes
// consumed, which the caller turns into the next page position.
//
// charWidth(c) must return 0 for bytes the renderer does not draw, so the measurement
// matches the screen. emit(line, index) receives every visual line and may do nothing
// when only the page length is wanted.
//
// The result is never 0 for non-empty text: a word wider than the line is accepted and
// clipped rather than leaving the reader on a page that never advances.
template <typename CharWidth, typename Emit>
size_t wrapPage(const char* text, size_t len, int16_t maxWidth, int16_t maxLines,
                CharWidth charWidth, Emit emit) {
  size_t pos = 0;
  size_t consumed = 0;

  for (int16_t lineIndex = 0; lineIndex < maxLines && pos < len; ++lineIndex) {
    while (pos < len && isBlank(text[pos])) {
      pos++;
    }
    if (pos >= len) {
      break;
    }

    // An explicit newline here means a paragraph break: an empty visual line.
    if (text[pos] == '\n') {
      pos++;
      consumed = pos;
      emit(Line{pos - 1, 0}, lineIndex);
      continue;
    }

    const size_t lineStart = pos;
    size_t lineEnd = pos;   // end of the last word accepted on this line
    int16_t lineWidth = 0;
    size_t cursor = pos;

    while (cursor < len) {
      size_t wordStart = cursor;
      while (wordStart < len && isBlank(text[wordStart])) {
        wordStart++;
      }
      if (wordStart >= len) {
        cursor = wordStart;
        break;
      }
      if (text[wordStart] == '\n') {
        cursor = wordStart + 1;   // the newline ends this line, and is consumed
        break;
      }

      // The separator is measured as the bytes actually present, because those
      // same bytes get drawn: collapsing them here would under-measure the line.
      int16_t separatorWidth = 0;
      if (lineEnd > lineStart) {
        for (size_t i = lineEnd; i < wordStart; ++i) {
          separatorWidth += charWidth(static_cast<unsigned char>(text[i]));
        }
      }

      size_t wordEnd = wordStart;
      int16_t wordWidth = 0;
      while (wordEnd < len && !isBlank(text[wordEnd]) && text[wordEnd] != '\n') {
        wordWidth += charWidth(static_cast<unsigned char>(text[wordEnd]));
        wordEnd++;
      }

      const bool lineHasContent = (lineEnd > lineStart);
      if (lineHasContent && lineWidth + separatorWidth + wordWidth > maxWidth) {
        cursor = wordStart;   // this word belongs to the next line
        break;
      }

      lineWidth += separatorWidth + wordWidth;
      lineEnd = wordEnd;
      cursor = wordEnd;
    }

    emit(Line{lineStart, lineEnd - lineStart}, lineIndex);
    pos = cursor;
    consumed = cursor;
  }

  // Trailing blanks skipped without emitting a line still count, or the reader stops
  // advancing on a page made only of whitespace.
  if (consumed < pos) {
    consumed = pos;
  }
  return consumed;
}

}  // namespace TextWrap
