// directory_listing.cpp - Manages SD card navigation and builds menu-friendly directory listings.
#include "directory_listing.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "ff.h"
#include "listingBuilder.h"

#if DEBUG_FILEIO
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

namespace picostation {

namespace {
    char currentDirectory[c_maxFilePathLength + 1];
    listingBuilder* fileListing;
}  // namespace

void DirectoryListing::init() {
    fileListing = new listingBuilder();
    gotoRoot();
}

void DirectoryListing::gotoRoot() { 
    currentDirectory[0] = '\0';
}

bool DirectoryListing::gotoDirectory(const uint32_t index) { 
    char newFolder[c_maxFilePathLength + 1];
    bool result = getDirectoryEntry(index, newFolder); 
    if (result)
    {
        combinePaths(currentDirectory, newFolder, currentDirectory);
    }
    DEBUG_PRINT("gotoDirectory: %s\n", currentDirectory);
    return result;
}

bool DirectoryListing::getPath(const uint32_t index, char* filePath) { 
    char newFolder[c_maxFilePathLength + 1];
    bool result = getDirectoryEntry(index, newFolder); 
    if (result)
    {
        combinePaths(currentDirectory, newFolder, filePath);
    }
    return result;
}

void DirectoryListing::gotoParentDirectory() {
    uint32_t length = strnlen(currentDirectory, c_maxFilePathLength);
    if (length == 0) {
        return;
    }

    uint32_t position = length - 1;

    while (position > 0) {
        if (currentDirectory[position] == '/') {
            currentDirectory[position] = '\0';
            return;
        }
        currentDirectory[position] = '\0';
        position--;
    }

    currentDirectory[0] = '\0';
}

bool DirectoryListing::getDirectoryEntries(const uint32_t offset) {
    DIR dir;
    FILINFO currentEntry;
    FILINFO nextEntry;
    FRESULT res = f_opendir(&dir, currentDirectory);
    if (res != FR_OK) {
        DEBUG_PRINT("f_opendir error: %s (%d)\n", FRESULT_str(res), res);
        return false;
    }

    fileListing->clear();

    uint16_t fileEntryCount = 0;
    uint16_t filesProcessed = 0;
    bool hasNext = false;

    res = f_readdir(&dir, &currentEntry);
    if (res == FR_OK && currentEntry.fname[0] != '\0') {
        res = f_readdir(&dir, &nextEntry);
        hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        while (true) {
            if (!(currentEntry.fattrib & AM_HID) && (currentEntry.fattrib & AM_DIR || strstr(currentEntry.fname, ".cue"))) {
                if (filesProcessed >= offset) {
                    if (fileListing->addString(currentEntry.fname, currentEntry.fattrib & AM_DIR ? 1 : 0) == false) {
                        break;
                    }
                    fileEntryCount++;
                }
                filesProcessed++;
                if (filesProcessed >= 4096)
                {
                    hasNext = 0;
                }
            }
            
            if (hasNext == 0) {
                break;
            }
            currentEntry = nextEntry;
            res = f_readdir(&dir, &nextEntry);
            hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        }
    }
    
    if (offset == 0)
    {
        uint16_t totalCount = getDirectoryEntriesCount();
        fileListing->addTerminator(hasNext ? 1 : 0, totalCount);
        DEBUG_PRINT("file count: %d\n", totalCount);
    }
    else
    {
        fileListing->addTerminator(hasNext ? 1 : 0, 0xffff);
    }
    
    f_closedir(&dir);
    return true;
}

uint16_t DirectoryListing::getDirectoryEntriesCount() {
    DIR dir;
    FILINFO currentEntry;
    FILINFO nextEntry;
    FRESULT res = f_opendir(&dir, currentDirectory);
    if (res != FR_OK) {
        DEBUG_PRINT("f_opendir error: %s (%d)\n", FRESULT_str(res), res);
        return 0;
    }

    uint16_t fileEntryCount = 0;
    bool hasNext = false;

    res = f_readdir(&dir, &currentEntry);
    if (res == FR_OK && currentEntry.fname[0] != '\0') {
        res = f_readdir(&dir, &nextEntry);
        hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        while (true) {
            if (!(currentEntry.fattrib & AM_HID) && (currentEntry.fattrib & AM_DIR || strstr(currentEntry.fname, ".cue"))) {
				fileEntryCount++;
				if (fileEntryCount >= 4096)
				{
					hasNext = 0;
				}
            }
            
            if (hasNext == 0) {
                break;
            }
            currentEntry = nextEntry;
            res = f_readdir(&dir, &nextEntry);
            hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        }
    }
    f_closedir(&dir);
    return fileEntryCount;
}

uint16_t* DirectoryListing::getFileListingData() {
    return fileListing->getData();
}

// Private

void DirectoryListing::combinePaths(const char* filePath1, const char* filePath2, char* newPath) { 
    char result[c_maxFilePathLength + 1];
    strncpy(result, filePath1, c_maxFilePathLength);
    if (strnlen(result, c_maxFilePathLength) > 0)
    {
        strncat(result, "/", c_maxFilePathLength);
    }
    strncat(result, filePath2, c_maxFilePathLength);
    strncpy(newPath, result, c_maxFilePathLength);
}

bool DirectoryListing::getDirectoryEntry(const uint32_t index, char* filePath) {

    if (index >= 4096)
    {
        return false;
    }

    DIR dir;
    FILINFO currentEntry;
    FILINFO nextEntry;
    FRESULT res = f_opendir(&dir, currentDirectory);
    if (res != FR_OK) {
        DEBUG_PRINT("f_opendir error: %s (%d)\n", FRESULT_str(res), res);
        return false;
    }

    uint32_t filesProcessed = 0;

    res = f_readdir(&dir, &currentEntry);
    if (res == FR_OK && currentEntry.fname[0] != '\0') {
        res = f_readdir(&dir, &nextEntry);
        bool hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        while (true) {
            if (!(currentEntry.fattrib & AM_HID) && (currentEntry.fattrib & AM_DIR || strstr(currentEntry.fname, ".cue"))) {
                if (filesProcessed == index) {
                    strncpy(filePath, currentEntry.fname, c_maxFilePathLength);
                    f_closedir(&dir);
                    return true;
                }
                filesProcessed++;
            }
            if (!hasNext) {
                break;
            }
            currentEntry = nextEntry;
            res = f_readdir(&dir, &nextEntry);
            hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        }
    }

    f_closedir(&dir);
    return false;
}


}  // namespace picostation
