#include "image_selector.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "directory_listing.h"
#include "listingBuilder.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

namespace picostation {
namespace {

struct ListingEntry {
    uint16_t index;
    bool isDirectory;
    std::string name;
};

bool waitForUsbConnection(uint32_t timeoutMs) {
    absolute_time_t deadline = make_timeout_time_ms(timeoutMs);
    while (!time_reached(deadline)) {
        if (stdio_usb_connected()) {
            return true;
        }
        sleep_ms(25);
    }
    return stdio_usb_connected();
}

bool readLine(char* buffer, size_t size) {
    if (size == 0) {
        return false;
    }

    size_t position = 0;
    while (true) {
        int ch = getchar_timeout_us(100000);
        if (ch == PICO_ERROR_TIMEOUT) {
            if (!stdio_usb_connected() && position == 0) {
                return false;
            }
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            putchar('\n');
            break;
        }

        if (ch == 3 || ch == 4) {  // Ctrl+C / Ctrl+D
            return false;
        }

        if (ch == '\b' || ch == 127) {
            if (position > 0) {
                --position;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }

        if (position + 1 < size) {
            buffer[position++] = static_cast<char>(ch);
            putchar(ch);
            fflush(stdout);
        }
    }

    buffer[position] = '\0';
    return true;
}

std::string trim(const std::string& input) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    const auto begin = std::find_if_not(input.begin(), input.end(), isSpace);
    if (begin == input.end()) {
        return std::string();
    }
    const auto end = std::find_if_not(input.rbegin(), input.rend(), isSpace).base();
    return std::string(begin, end);
}

std::vector<std::string> splitTokens(const std::string& line) {
    std::vector<std::string> tokens;
    size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        if (position >= line.size()) {
            break;
        }
        size_t start = position;
        while (position < line.size() && !std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        tokens.emplace_back(line.substr(start, position - start));
    }
    return tokens;
}

struct ListingPageResult {
    std::vector<ListingEntry> entries;
    uint16_t totalCount = 0;
};

ListingPageResult loadDirectoryEntries() {
    ListingPageResult result;
    uint16_t offset = 0;
    bool hasNext = false;

    do {
        if (!DirectoryListing::getDirectoryEntries(offset)) {
            printf("Failed to read directory contents.\n");
            fflush(stdout);
            break;
        }

        auto* rawData = reinterpret_cast<uint8_t*>(DirectoryListing::getFileListingData());
        size_t position = 0;
        size_t addedThisPage = 0;
        hasNext = false;

        while (position + 1 < LISTING_SIZE) {
            const uint8_t entryLength = rawData[position];
            const uint8_t flags = rawData[position + 1];
            if (entryLength == 0) {
                hasNext = flags != 0;
                const uint16_t reportedCount = static_cast<uint16_t>((rawData[position + 2] << 8) | rawData[position + 3]);
                if (reportedCount != 0xFFFF) {
                    result.totalCount = reportedCount;
                }
                break;
            }

            std::string name(reinterpret_cast<char*>(rawData + position + 2), entryLength);
            result.entries.push_back({static_cast<uint16_t>(offset + addedThisPage), flags != 0, std::move(name)});
            position += static_cast<size_t>(entryLength) + 2;
            ++addedThisPage;
        }

        offset = static_cast<uint16_t>(result.entries.size());
    } while (hasNext && offset < 4096);

    if (result.totalCount == 0 && !result.entries.empty()) {
        result.totalCount = static_cast<uint16_t>(result.entries.size());
    }

    return result;
}

void printListing(const std::vector<ListingEntry>& entries, uint16_t totalCount) {
    printf("\nIndex  Type  Name\n");
    printf("-----  ----  ------------------------------\n");
    for (const auto& entry : entries) {
        std::string displayName = entry.name;
        if (!entry.isDirectory) {
            displayName += ".cue";
        }
        printf("%4u   %s   %s\n", entry.index, entry.isDirectory ? "DIR" : "CUE", displayName.c_str());
    }
    if (entries.empty()) {
        printf("(directory is empty)\n");
    }
    if (totalCount > 0) {
        printf("Total entries: %u\n", totalCount);
    }
    fflush(stdout);
}

void printHelp() {
    printf(
        "Available commands:\n"
        "  ls/list            - Show directory contents\n"
        "  open <index>       - Enter directory at <index>\n"
        "  back/up            - Go to parent directory\n"
        "  root               - Return to the SD card root\n"
        "  mount <index>      - Mount the .cue file at <index>\n"
        "  quit/exit          - Leave the selector without mounting\n"
        "  help/?             - Show this message\n");
    fflush(stdout);
}

const ListingEntry* findEntry(const std::vector<ListingEntry>& entries, uint16_t index) {
    auto it = std::find_if(entries.begin(), entries.end(), [index](const ListingEntry& entry) {
        return entry.index == index;
    });
    return it == entries.end() ? nullptr : &(*it);
}

bool parseIndex(const std::vector<std::string>& tokens, uint16_t& value) {
    if (tokens.size() < 2) {
        printf("Missing index argument.\n");
        fflush(stdout);
        return false;
    }

    char* endPtr = nullptr;
    long parsed = std::strtol(tokens[1].c_str(), &endPtr, 10);
    if (endPtr == tokens[1].c_str() || *endPtr != '\0' || parsed < 0 || parsed > 0xFFFF) {
        printf("Invalid index '%s'.\n", tokens[1].c_str());
        fflush(stdout);
        return false;
    }

    value = static_cast<uint16_t>(parsed);
    return true;
}

}  // namespace

std::optional<ImageSelection> promptForImageSelection() {
    stdio_usb_init();

    if (!waitForUsbConnection(500)) {
        return std::nullopt;
    }

    printf("\nPicoStation Image Selector\n");
    printf("--------------------------\n");
    printHelp();

    auto view = loadDirectoryEntries();
    printListing(view.entries, view.totalCount);

    char lineBuffer[128];

    while (true) {
        const char* currentDirectory = DirectoryListing::getCurrentDirectory();
        const char* promptPath = (currentDirectory && currentDirectory[0] != '\0') ? currentDirectory : "/";
        printf("[%s]> ", promptPath);
        fflush(stdout);

        if (!readLine(lineBuffer, sizeof(lineBuffer))) {
            printf("\nLeaving image selector.\n");
            fflush(stdout);
            return std::nullopt;
        }

        std::string line = trim(lineBuffer);
        if (line.empty()) {
            continue;
        }

        auto tokens = splitTokens(line);
        if (tokens.empty()) {
            continue;
        }

        std::string command = tokens[0];
        std::transform(command.begin(), command.end(), command.begin(), [](unsigned char c) { return std::tolower(c); });

        if (command == "ls" || command == "list") {
            view = loadDirectoryEntries();
            printListing(view.entries, view.totalCount);
            continue;
        }

        if (command == "help" || command == "?") {
            printHelp();
            continue;
        }

        if (command == "open" || command == "cd") {
            uint16_t targetIndex = 0;
            if (!parseIndex(tokens, targetIndex)) {
                continue;
            }

            const ListingEntry* entry = findEntry(view.entries, targetIndex);
            if (!entry) {
                printf("No entry with index %u.\n", targetIndex);
                fflush(stdout);
                continue;
            }

            if (!entry->isDirectory) {
                printf("Entry %u is not a directory.\n", targetIndex);
                fflush(stdout);
                continue;
            }

            if (!DirectoryListing::gotoDirectory(targetIndex)) {
                printf("Failed to enter directory.\n");
                fflush(stdout);
                continue;
            }

            view = loadDirectoryEntries();
            printListing(view.entries, view.totalCount);
            continue;
        }

        if (command == "back" || command == "up") {
            DirectoryListing::gotoParentDirectory();
            view = loadDirectoryEntries();
            printListing(view.entries, view.totalCount);
            continue;
        }

        if (command == "root") {
            DirectoryListing::gotoRoot();
            view = loadDirectoryEntries();
            printListing(view.entries, view.totalCount);
            continue;
        }

        if (command == "mount" || command == "load" || command == "select") {
            uint16_t targetIndex = 0;
            if (!parseIndex(tokens, targetIndex)) {
                continue;
            }

            const ListingEntry* entry = findEntry(view.entries, targetIndex);
            if (!entry) {
                printf("No entry with index %u.\n", targetIndex);
                fflush(stdout);
                continue;
            }

            if (entry->isDirectory) {
                printf("Entry %u is a directory. Use 'open %u' to enter it.\n", targetIndex, targetIndex);
                fflush(stdout);
                continue;
            }

            ImageSelection selection{};
            selection.index = targetIndex;
            if (!DirectoryListing::getPath(targetIndex, selection.path.data())) {
                printf("Failed to resolve file path.\n");
                fflush(stdout);
                continue;
            }

            printf("Mounting %s...\n", selection.path.data());
            fflush(stdout);
            return selection;
        }

        if (command == "quit" || command == "exit") {
            printf("Exiting without mounting an image.\n");
            fflush(stdout);
            return std::nullopt;
        }

        printf("Unknown command '%s'. Type 'help' for available commands.\n", command.c_str());
        fflush(stdout);
    }
}

}  // namespace picostation
