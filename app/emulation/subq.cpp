// subq.cpp - Generates and transmits CD sub-channel (SubQ) data for the console.
#include "subq.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "emulation/drive_mechanics.h"
#include "emulation/disc_image.h"
#include "hardware/pio.h"
#include "commons/logging.h"
#include "main.pio.h"
#include "picostation.h"
#include "commons/values.h"

#if DEBUG_SUBQ
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

void picostation::SubQ::printf_subq(const uint8_t *data) {
    for (size_t i = 0; i < 12; i++) {
        DEBUG_PRINT("%02X ", data[i]);
    }
}

void __time_critical_func(picostation::SubQ::start_subq)(const int sector) {
    const SubQ::Data tracksubq = m_discImage->generateSubQ(sector);
    
    if (!g_driveMechanics.isSledStopped())
	{
		return;
	}
    
    gpio_put(Pin::SCOR, 1);
	
	add_alarm_in_us( 135,
					[](alarm_id_t id, void *user_data) -> int64_t {
						gpio_put(Pin::SCOR, 0);
						return 0;
					}, NULL, true);
    
    subq_program_init(PIOInstance::SUBQ, SM::SUBQ, g_subqOffset, Pin::SQSO, Pin::SQCK);
    pio_sm_clear_fifos(PIOInstance::SUBQ, SM::SUBQ);
    pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, true);
    
    const uint sub[3] = {
        (uint)((tracksubq.raw[3] << 24) | (tracksubq.raw[2] << 16) | (tracksubq.raw[1] << 8) | (tracksubq.raw[0])),
        (uint)((tracksubq.raw[7] << 24) | (tracksubq.raw[6] << 16) | (tracksubq.raw[5] << 8) | (tracksubq.raw[4])),
        (uint)((tracksubq.raw[11] << 24) | (tracksubq.raw[10] << 16) | (tracksubq.raw[9] << 8) | (tracksubq.raw[8]))};
    pio_sm_put_blocking(PIOInstance::SUBQ, SM::SUBQ, sub[0]);
    pio_sm_put_blocking(PIOInstance::SUBQ, SM::SUBQ, sub[1]);
    pio_sm_put_blocking(PIOInstance::SUBQ, SM::SUBQ, sub[2]);
#if DEBUG_SUBQ
    if (sector % 50 == 0) {
        printf_subq(tracksubq.raw);
        DEBUG_PRINT("%d\n", sector);
    }
#endif
}

/*void picostation::SubQ::stop_subq() {
    pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, false);
    pio_sm_restart(PIOInstance::SUBQ, SM::SUBQ);
    pio_sm_clear_fifos(PIOInstance::SUBQ, SM::SUBQ);
    pio_sm_exec(PIOInstance::SUBQ, SM::SUBQ, pio_encode_jmp(g_subqOffset));
}*/
