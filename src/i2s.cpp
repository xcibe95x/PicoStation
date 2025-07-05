#include "i2s.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <array>

#include "cmd.h"
#include "directory_listing.h"
#include "disc_image.h"
#include "drive_mechanics.h"
#include "ff.h"
#include "global.h"
#include "hardware/pio.h"
#include "logging.h"
#include "main.pio.h"
#include "modchip.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "pseudo_atomics.h"
#include "subq.h"
#include "values.h"
#include "listingBuilder.h"

#if DEBUG_I2S
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

#define CACHED_SECS		8 /* Only 2, 4, 8, 16, 32 */

pseudoatomic<int> g_imageIndex;  // To-do: Implement a console side menu to select the cue file
pseudoatomic<int> g_listingMode;

picostation::DiscImage::DataLocation s_dataLocation = picostation::DiscImage::DataLocation::RAM;
static FATFS s_fatFS;

static uint16_t *generateScramblingLUT() {
    static uint16_t ScramblingLUT[1176] = {0};
    int shift = 1;
	
	for (int i = 0; i < 6; i++) {
		ScramblingLUT[i] = 0;
	}
	
    for (size_t i = 6; i < 1176; i++) {
        uint8_t upper = shift & 0xFF;
        for (size_t j = 0; j < 8; j++) {
            unsigned bit = ((shift & 1) ^ ((shift & 2) >> 1)) << 15;
            shift = (bit | shift) >> 1;
        }

        uint8_t lower = shift & 0xFF;

        ScramblingLUT[i] = (lower << 8) | upper;

        for (size_t j = 0; j < 8; j++) {
            unsigned bit = ((shift & 1) ^ ((shift & 2) >> 1)) << 15;
            shift = (bit | shift) >> 1;
        }
    }

    return ScramblingLUT;
}

void picostation::I2S::mountSDCard() {
    FRESULT fr = f_mount(&s_fatFS, "", 1);
    if (FR_OK != fr) {
        panic("f_mount error: (%d)\n", fr);
    }
}

int picostation::I2S::initDMA(const volatile void *read_addr, unsigned int transfer_count) {
    int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    const unsigned int i2sDREQ = PIOInstance::I2S_DATA == pio0 ? DREQ_PIO0_TX0 : DREQ_PIO1_TX0;
    channel_config_set_dreq(&c, i2sDREQ);
    dma_channel_configure(channel, &c, &PIOInstance::I2S_DATA->txf[SM::I2S_DATA], read_addr, transfer_count, false);

    return channel;
}

[[noreturn]] void __time_critical_func(picostation::I2S::start)(MechCommand &mechCommand) {
    picostation::ModChip modChip;
    static uint32_t pioSamples[CACHED_SECS][1176];
    static uint16_t *cdScramblingLUT = generateScramblingLUT();

    static uint8_t bufferForDMA = 1;
    static uint8_t bufferForSDRead = 0;
    static int loadedSector[CACHED_SECS];
    static int currentSector = -1;
    static int lastSector = -1;
    
    //static bool first_run = true;
    
    m_sectorSending = -1;
    static int loadedImageIndex = 0;

    g_imageIndex = -1;
    menu_active = true;
    
    for (int i = 0; i < CACHED_SECS; i++) {
		loadedSector[i] = -2;
	}
	
    
    dmaChannel = initDMA(pioSamples[0], 1176);

    g_coreReady[1] = true;          // Core 1 is ready
    while (!g_coreReady[0].Load())  // Wait for Core 0 to be ready
    {
        tight_loop_contents();
    }

    modChip.init();

#if DEBUG_I2S
    uint64_t startTime;
    uint64_t endTime;
#endif

	mountSDCard();
	
    // this need to be moved to diskimage
    picostation::DirectoryListing::init();
    picostation::DirectoryListing::gotoRoot();
    picostation::DirectoryListing::getDirectoryEntries(0);

    while (true) {
        // Update latching, output SENS

        // Sector could change during the loop, so we need to keep track of it
        currentSector = g_driveMechanics.getSector();
        
        modChip.sendLicenseString(currentSector, mechCommand);
		
		if(menu_active) {
			// Load the disc image if it has changed
			const int imageIndex = g_imageIndex.Load();

			// Hacky load image from target data location
			if (imageIndex == -1) {
				s_dataLocation = picostation::DiscImage::DataLocation::RAM;
			} else {
				s_dataLocation = picostation::DiscImage::DataLocation::SDCard;
			}
			
			FileListingStates curstate = g_fileListingState.Load();
			
			if (loadedImageIndex != imageIndex || curstate == FileListingStates::MOUNT_FILE) {
				if (s_dataLocation == picostation::DiscImage::DataLocation::SDCard) {
					char filePath[c_maxFilePathLength + 1];
					picostation::DirectoryListing::getPath(imageIndex, filePath);
					g_discImage.load(filePath);
					menu_active = false;
				} else if (s_dataLocation == picostation::DiscImage::DataLocation::RAM) {
					g_discImage.makeDummyCue();
				}

				loadedImageIndex = imageIndex;

				// Reset cache and loaded sectors
				/*for (int i = 0; i < CACHED_SECS; i++) {
					loadedSector[i] = -2;
				}
				
				bufferForDMA = 1;
				bufferForSDRead = 0;*/
				//memset(pioSamples, 0, sizeof(pioSamples));
			}
			
			switch (curstate)
			{
				case FileListingStates::GOTO_ROOT:
					//printf("Processing GOTO_ROOT\n");
					picostation::DirectoryListing::gotoRoot();
					picostation::DirectoryListing::getDirectoryEntries(0);
					break;
				
				case FileListingStates::GOTO_PARENT:
					//printf("Processing GOTO_PARENT\n");
					picostation::DirectoryListing::gotoParentDirectory();
					picostation::DirectoryListing::getDirectoryEntries(0);
					break;
				
				case FileListingStates::GOTO_DIRECTORY:
					//printf("Processing GOTO_DIRECTORY %i\n", g_fileArg.Load());
					picostation::DirectoryListing::gotoDirectory(g_fileArg.Load());
					picostation::DirectoryListing::getDirectoryEntries(0);
					break;
				
				case FileListingStates::GET_NEXT_CONTENTS:
					picostation::DirectoryListing::getDirectoryEntries(g_fileArg.Load());
					break;
				
				case FileListingStates::MOUNT_FILE:
					//printf("Processing MOUNT_FILE\n");
					// move mounting here;
					gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_LOW, false);
					gpio_set_dir(Pin::RESET, GPIO_OUT);
					gpio_put(Pin::RESET, 0);
					sleep_ms(300);
					gpio_put(Pin::RESET, 1);
					gpio_set_dir(Pin::RESET, GPIO_IN);
					gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_LOW, true);
					break;
				
				default:
					break;
			}
			
			g_fileListingState = FileListingStates::IDLE;
		}
		
        // Data sent via DMA, load the next sector
        if (currentSector != lastSector) {
			if (currentSector <= 4650) {
				loadedSector[bufferForDMA] = currentSector;
				lastSector = currentSector;
				goto continue_transfer;
			}
			
			for (int i = 0; i < CACHED_SECS; i++) {
				if (loadedSector[i] == currentSector) {
					// already in cache
					bufferForDMA = i;
					lastSector = currentSector;
					goto continue_transfer;
				}
			}
			
			while (loadedSector[bufferForSDRead] == getSectorSending()){
				++bufferForSDRead &= (CACHED_SECS-1);
			}
			
            if (menu_active && (currentSector - c_leadIn - c_preGap) == 100 && g_fileListingState.Load() == FileListingStates::IDLE) {
                g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], 
										(uint16_t *) picostation::DirectoryListing::getFileListingData(), cdScramblingLUT);
            } else {
#if DEBUG_I2S
				startTime = time_us_64();
#endif
                // Load the next sector
                g_discImage.readSector(pioSamples[bufferForSDRead], currentSector - c_leadIn, s_dataLocation, cdScramblingLUT);
#if DEBUG_I2S
				endTime = time_us_64();
				DEBUG_PRINT("read time: %lluus (%d)\n", endTime-startTime, currentSector);
#endif
            }

            loadedSector[bufferForSDRead] = currentSector;
            bufferForDMA = bufferForSDRead;
			++bufferForSDRead &= (CACHED_SECS - 1);
            lastSector = currentSector;
        }
continue_transfer:

        // Start the next transfer if the DMA channel is not busy
        if (!dma_channel_is_busy(dmaChannel)) {
			if (currentSector > 0) {
				m_sectorSending = loadedSector[bufferForDMA];
				m_lastSectorTime = time_us_64();
			}
            
            if (currentSector > 4650) {
				/*if (!first_run)
				{
					(void) pio_sm_get_blocking(PIOInstance::SUBQ, SM::SUBQ);
				}*/
				//updatePlaybackSpeed();
				
				dma_hw->ch[dmaChannel].read_addr = (uint32_t)pioSamples[bufferForDMA];

				// Sync with the I2S clock
				while (gpio_get(Pin::LRCK) == 1) {
					tight_loop_contents();
				}
				while (gpio_get(Pin::LRCK) == 0) {
					tight_loop_contents();
				}

				dma_channel_start(dmaChannel);
			}
			//first_run = false;
        }
    }
    __builtin_unreachable();
}
