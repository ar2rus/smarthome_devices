#pragma once
// /edit is optional; the bridge API, LittleFS pages and /flash do not depend on it.
#ifndef SMARTHOME_HAS_FILE_EDITOR
  #ifdef __has_include
    #if __has_include(<SPIFFSEditor.h>)
      #define SMARTHOME_HAS_FILE_EDITOR 1
    #else
      #define SMARTHOME_HAS_FILE_EDITOR 0
    #endif
  #else
    #define SMARTHOME_HAS_FILE_EDITOR 0
  #endif
#endif
