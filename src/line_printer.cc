// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "line_printer.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x4
#endif
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/time.h>
#endif

#include "util.h"

using namespace std;

LinePrinter::LinePrinter() : have_blank_line_(true), console_locked_(false) {
  const char* term = getenv("TERM");
#ifndef _WIN32
  smart_terminal_ = isatty(1) && term && string(term) != "dumb";
#else
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  console_ = GetStdHandle(STD_OUTPUT_HANDLE);
  smart_terminal_ = GetConsoleScreenBufferInfo(console_, &csbi);
  if (smart_terminal_) {
    terminal_width = csbi.dwSize.X;
  } else {
    const char* columns = getenv("COLUMNS");
    if (columns) {
      terminal_width = atoi(columns);
    }
  }
  if (term && string(term) == "dumb") {
    smart_terminal_ = false;
  } else if (term && string(term).find_first_of("xterm") != string::npos) {
    smart_terminal_ = true;
  }
#endif
  supports_color_ = smart_terminal_;
  if (!supports_color_) {
    const char* clicolor_force = getenv("CLICOLOR_FORCE");
    supports_color_ = clicolor_force && string(clicolor_force) != "0";
  }
#ifdef _WIN32
  // Try enabling ANSI escape sequence support on Windows 10 terminals.
  if (supports_color_) {
    DWORD mode;
    if (GetConsoleMode(console_, &mode)) {
      if (!SetConsoleMode(console_, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        supports_color_ = false;
      }
    }
  }
#endif
}

void LinePrinter::Print(string to_print, LineType type) {
  if (console_locked_) {
    line_buffer_ = to_print;
    line_type_ = type;
    return;
  }

  if (smart_terminal_ && type == ELIDE) {
#ifndef _WIN32
    // Limit output to width of the terminal if provided so we don't cause
    // line-wrapping.
    winsize size;
    if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) && size.ws_col) {
      terminal_width = size.ws_col;
    }
#endif
    to_print = ElideMiddle(to_print, static_cast<size_t>(terminal_width));
    printf("\r%s\x1B[K", to_print.c_str());  // Clear to end of line
    have_blank_line_ = false;
  } else {
    printf("%s\n", to_print.c_str());
  }
  fflush(stdout);
}

void LinePrinter::PrintOrBuffer(const char* data, size_t size) {
  if (console_locked_) {
    output_buffer_.append(data, size);
  } else {
    // Avoid printf and C strings, since the actual output might contain null
    // bytes like UTF-16 does (yuck).
    fwrite(data, 1, size, stdout);
  }
}

void LinePrinter::PrintOnNewLine(const string& to_print) {
  if (console_locked_ && !line_buffer_.empty()) {
    output_buffer_.append(line_buffer_);
    output_buffer_.append(1, '\n');
    line_buffer_.clear();
  }
  if (!have_blank_line_) {
    PrintOrBuffer("\n", 1);
  }
  if (!to_print.empty()) {
    PrintOrBuffer(&to_print[0], to_print.size());
  }
  have_blank_line_ = to_print.empty() || *to_print.rbegin() == '\n';
}

void LinePrinter::SetConsoleLocked(bool locked) {
  if (locked == console_locked_)
    return;

  if (locked)
    PrintOnNewLine("");

  console_locked_ = locked;

  if (!locked) {
    PrintOnNewLine(output_buffer_);
    if (!line_buffer_.empty()) {
      Print(line_buffer_, line_type_);
    }
    output_buffer_.clear();
    line_buffer_.clear();
  }
}
