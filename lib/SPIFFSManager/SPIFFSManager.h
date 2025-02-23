#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H

#include <Arduino.h>
#include "FS.h"
#include "SPIFFS.h"

#define FORMAT_SPIFFS_IF_FAILED true

class SPIFFSManager {
public:
    SPIFFSManager(fs::FS &fs);
    ~SPIFFSManager();

    void listDir(const char *dirname, uint8_t levels);
    String readFile(const char *path);
    void writeFile(const char *path, const char *message);
    void appendFile(const char *path, const char *message);
    void renameFile(const char *path1, const char *path2);
    void deleteFile(const char *path);
    void testFileIO(const char *path);
    bool fileExists(const char *path);

private:
    fs::FS &fs_;
};

#endif // SPIFFS_MANAGER_H
