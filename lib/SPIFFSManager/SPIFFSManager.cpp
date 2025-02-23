#include "SPIFFSManager.h"

SPIFFSManager::SPIFFSManager(fs::FS &fs) : fs_(fs) {}

SPIFFSManager::~SPIFFSManager() {}

void SPIFFSManager::listDir(const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs_.open(dirname);
    if (!root) {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                listDir(file.path(), levels - 1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

String SPIFFSManager::readFile(const char *path) {
    Serial.printf("Reading file: %s\r\n", path);
    String fileContent = "";

    // First, check if path exists and is really a file
    if (!fs_.exists(path)) {
        Serial.println("- path does not exist");
        return fileContent;
    }

    File file = fs_.open(path, "r");
    if (!file) {
        Serial.println("- failed to open file for reading (file object is null)");
        return fileContent;
    }

    if (file.isDirectory()) {
        Serial.println("- failed to open file for reading (path is a directory)");
        file.close();
        return fileContent;
    }

    size_t fileSize = file.size();
    Serial.printf("- file opened successfully, size: %d bytes\n", fileSize);

    if (fileSize == 0) {
        Serial.println("- warning: file is empty");
        file.close();
        return fileContent;
    }

    // Read the file in chunks
    uint8_t buf[128];
    while (file.available()) {
        size_t bytesRead = file.read(buf, sizeof(buf));
        if (bytesRead > 0) {
            fileContent += String((char*)buf, bytesRead);
        }
    }

    Serial.printf("- read %d bytes from file\n", fileContent.length());
    Serial.printf("- content: '%s'\n", fileContent.c_str());
    
    file.close();
    return fileContent;
}

// String SPIFFSManager::readFile2(const char *path) {
//     Serial.printf("Reading file: %s\r\n", path);
//     String fileContent = "";

//     File file = fs_.open(path);
//     if (!file || file.isDirectory()) {
//         Serial.println("- failed to open file for reading");
//         return fileContent;
//     }

//     Serial.println("- read from file:");
//     while (file.available()) {
//         fileContent += (char)file.read();
//     }
//     Serial.println(fileContent);
//     file.close();
    
//     return fileContent;
// }

bool SPIFFSManager::fileExists(const char *path) {
    Serial.printf("Checking if file exists: %s\r\n", path);
    
    if (!fs_.exists(path)) {
        Serial.println("- path does not exist");
        return false;
    }

    File file = fs_.open(path, "r");
    if (!file) {
        Serial.println("- failed to open file");
        return false;
    }

    bool isFile = !file.isDirectory();
    file.close();

    if (isFile) {
        Serial.println("- file exists and is a regular file");
    } else {
        Serial.println("- path exists but is a directory");
    }

    return isFile;
}

void SPIFFSManager::writeFile(const char *path, const char *message) {
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs_.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message)) {
        Serial.println("- file written");
    } else {
        Serial.println("- write failed");
    }
    file.close();
}

void SPIFFSManager::appendFile(const char *path, const char *message) {
    Serial.printf("Appending to file: %s\r\n", path);

    File file = fs_.open(path, FILE_APPEND);
    if (!file) {
        Serial.println("- failed to open file for appending");
        return;
    }
    if (file.print(message)) {
        Serial.println("- message appended");
    } else {
        Serial.println("- append failed");
    }
    file.close();
}

void SPIFFSManager::renameFile(const char *path1, const char *path2) {
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs_.rename(path1, path2)) {
        Serial.println("- file renamed");
    } else {
        Serial.println("- rename failed");
    }
}

void SPIFFSManager::deleteFile(const char *path) {
    Serial.printf("Deleting file: %s\r\n", path);
    if (fs_.remove(path)) {
        Serial.println("- file deleted");
    } else {
        Serial.println("- delete failed");
    }
}

void SPIFFSManager::testFileIO(const char *path) {
    Serial.printf("Testing file I/O with %s\r\n", path);

    static uint8_t buf[512];
    size_t len = 0;
    File file = fs_.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("- failed to open file for writing");
        return;
    }

    size_t i;
    Serial.print("- writing");
    uint32_t start = millis();
    for (i = 0; i < 2048; i++) {
        if ((i & 0x001F) == 0x001F) {
            Serial.print(".");
        }
        file.write(buf, 512);
    }
    Serial.println("");
    uint32_t end = millis() - start;
    Serial.printf(" - %u bytes written in %u ms\r\n", 2048 * 512, end);
    file.close();

    file = fs_.open(path);
    start = millis();
    end = start;
    i = 0;
    if (file && !file.isDirectory()) {
        len = file.size();
        size_t flen = len;
        start = millis();
        Serial.print("- reading");
        while (len) {
            size_t toRead = len;
            if (toRead > 512) {
                toRead = 512;
            }
            file.read(buf, toRead);
            if ((i++ & 0x001F) == 0x001F) {
                Serial.print(".");
            }
            len -= toRead;
        }
        Serial.println("");
        end = millis() - start;
        Serial.printf("- %u bytes read in %u ms\r\n", flen, end);
        file.close();
    } else {
        Serial.println("- failed to open file for reading");
    }
}
