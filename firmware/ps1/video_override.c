#include "video_override.h"

#include <stddef.h>
#include <stdint.h>

#define GPU_GP1_COMMAND_PORT      (*(volatile uint32_t *)0x1F801814u)  // GPU control port
#define GPU_GP1_CMD_DISPLAY_MODE  0x08000000u                           // GP1 command index 0x08
#define GPU_DISPLAY_MODE_PAL_BIT  0x00000008u                           // Bit 3 selects PAL when set
#define GPU_DISPLAY_MODE_MASK     0x000003FFu                           // All valid display mode bits

#define JOY_DATA_PORT             (*(volatile uint16_t *)0x1F801040u)   // SIO data register
#define JOY_STATUS_PORT           (*(volatile uint16_t *)0x1F801044u)   // SIO status register
#define JOY_MODE_PORT             (*(volatile uint16_t *)0x1F801048u)   // SIO mode register
#define JOY_CTRL_PORT             (*(volatile uint16_t *)0x1F80104Au)   // SIO control register
#define JOY_BAUD_PORT             (*(volatile uint16_t *)0x1F80104Eu)   // SIO baud rate register

#define BIOS_B0_TABLE_BASE        0x80000100u                           // Kernel B0 table base in KSEG0
#define BIOS_TABLE_ENTRY(group_base, index) (*((volatile uint32_t *)((group_base) + ((index) * sizeof(uint32_t)))))
#define BIOS_SET_VIDEOMODE_INDEX  0x44u                                 // B0 syscall entry for SetVideoMode

#define VIDEO_OVERRIDE_MAGIC      0x5649444Fu                          // 'VIDO'
#define VIDEO_OVERRIDE_VERSION    1u
#define VIDEO_OVERRIDE_STORE_ADDR 0x1F000200u                          // Backed storage (custom NVRAM window)

#define PAD_BUTTON_SELECT         0x0001u
#define PAD_BUTTON_R1             0x0800u
#define PAD_PACKET_ID_DIGITAL     0x5Au                                // Digital pad signature
#define PAD_TX_READY              0x0001u                              // Ready to send byte
#define PAD_RX_READY              0x0002u                              // Byte received

typedef int (*set_video_mode_fn)(int mode);

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t mode;
    uint8_t reserved[2];
} video_override_store_t;

static volatile video_override_store_t *const g_nvram =
    (volatile video_override_store_t *)VIDEO_OVERRIDE_STORE_ADDR;

static volatile ps1_video_mode_t g_forcedMode = PS1_VIDEO_MODE_NTSC;
static volatile uint32_t g_lastRequestedMode = 0u;
static set_video_mode_fn g_originalSetVideoMode = NULL;

static inline void gpu_write_gp1(uint32_t data) {
    GPU_GP1_COMMAND_PORT = GPU_GP1_CMD_DISPLAY_MODE | (data & GPU_DISPLAY_MODE_MASK);
}

static inline uint32_t to_kseg0(uintptr_t addr) {
    // Mirror the physical address into KSEG0 (cached kernel view).
    return (uint32_t)((addr & 0x1FFFFFFFu) | 0x80000000u);
}

static inline uint32_t to_kseg1(uintptr_t addr) {
    // Mirror the physical address into KSEG1 (uncached kernel view).
    return (uint32_t)((addr & 0x1FFFFFFFu) | 0xA0000000u);
}

static void persist_mode(ps1_video_mode_t mode) {
    // Simple wear-leveling friendly write: copy a shadow structure word by word.
    video_override_store_t snapshot = {
        .magic = VIDEO_OVERRIDE_MAGIC,
        .version = VIDEO_OVERRIDE_VERSION,
        .mode = (uint8_t)mode,
        .reserved = {0, 0},
    };

    const uint32_t *src = (const uint32_t *)&snapshot;
    volatile uint32_t *dst = (volatile uint32_t *)to_kseg1((uintptr_t)g_nvram);
    for (size_t i = 0; i < sizeof(snapshot) / sizeof(uint32_t); ++i) {
        dst[i] = src[i];
    }
}

static void load_mode_from_nvram(void) {
    if ((g_nvram->magic != VIDEO_OVERRIDE_MAGIC) || (g_nvram->version != VIDEO_OVERRIDE_VERSION)) {
        // Initialize NVRAM if the slot is blank or outdated.
        persist_mode(PS1_VIDEO_MODE_NTSC);
        g_forcedMode = PS1_VIDEO_MODE_NTSC;
        return;
    }

    switch ((ps1_video_mode_t)g_nvram->mode) {
        case PS1_VIDEO_MODE_PAL:
            g_forcedMode = PS1_VIDEO_MODE_PAL;
            break;
        case PS1_VIDEO_MODE_NTSC:
        default:
            g_forcedMode = PS1_VIDEO_MODE_NTSC;
            break;
    }
}

static uint8_t pad_transfer(uint8_t value) {
    // Serially clock a single byte to/from the controller port.
    while (!(JOY_STATUS_PORT & PAD_TX_READY)) {
    }

    JOY_DATA_PORT = value;

    while (!(JOY_STATUS_PORT & PAD_RX_READY)) {
    }

    return (uint8_t)(JOY_DATA_PORT & 0xFFu);
}

static void pad_idle(void) {
    JOY_CTRL_PORT = 0;
}

static uint16_t poll_pad_once(void) {
    // Configure SIO0 for standard controller speed (250 kbit).
    JOY_CTRL_PORT = 0;
    JOY_MODE_PORT = 0x000Du;
    JOY_BAUD_PORT = 0x0088u;
    JOY_CTRL_PORT = 0x1003u;

    // Issue the standard digital pad poll command sequence.
    pad_transfer(0x01u);
    pad_transfer(0x42u);

    uint8_t state = pad_transfer(0x00u);
    if (state != PAD_PACKET_ID_DIGITAL) {
        pad_idle();
        return 0xFFFFu;
    }

    uint8_t buttonsLo = pad_transfer(0x00u);
    uint8_t buttonsHi = pad_transfer(0x00u);

    pad_idle();

    return (uint16_t)((uint16_t)buttonsLo | ((uint16_t)buttonsHi << 8));
}

bool ps1_video_override_toggle_from_input(void) {
    const uint16_t buttons = poll_pad_once();
    if (buttons == 0xFFFFu) {
        return false;
    }

    const bool selectHeld = (buttons & PAD_BUTTON_SELECT) == 0u;
    const bool r1Held = (buttons & PAD_BUTTON_R1) == 0u;

    if (selectHeld && r1Held) {
        // Toggle the preference and immediately persist it for the next boot.
        g_forcedMode = (g_forcedMode == PS1_VIDEO_MODE_PAL) ? PS1_VIDEO_MODE_NTSC : PS1_VIDEO_MODE_PAL;
        persist_mode(g_forcedMode);
        apply_forced_mode();
        return true;
    }

    return false;
}

static void apply_forced_mode(void) {
    // Preserve all display settings requested by the game except for the PAL/NTSC bit.
    const uint32_t requested = g_lastRequestedMode & GPU_DISPLAY_MODE_MASK;
    const uint32_t cleared = requested & ~GPU_DISPLAY_MODE_PAL_BIT;
    const uint32_t overrideBit = (g_forcedMode == PS1_VIDEO_MODE_PAL) ? GPU_DISPLAY_MODE_PAL_BIT : 0u;
    gpu_write_gp1(cleared | overrideBit);
}

static int set_video_mode_hook(int mode) {
    // Remember the raw mode bits requested by the caller for later reuse.
    g_lastRequestedMode = (uint32_t)mode;

    int result = 0;
    if (g_originalSetVideoMode) {
        result = g_originalSetVideoMode(mode);
    }

    apply_forced_mode();
    return result;
}

void ps1_video_override_set_mode(ps1_video_mode_t mode) {
    if (mode != g_forcedMode) {
        g_forcedMode = mode;
        persist_mode(mode);
    }
    apply_forced_mode();
}

ps1_video_mode_t ps1_video_override_get_mode(void) {
    return g_forcedMode;
}

void ps1_video_override_install(void) {
    load_mode_from_nvram();
    ps1_video_override_toggle_from_input();

    // Patch the BIOS SetVideoMode entry so we can enforce the timing bit.
    const uintptr_t tableAddr = BIOS_B0_TABLE_BASE + (BIOS_SET_VIDEOMODE_INDEX * sizeof(uint32_t));
    volatile uint32_t *const entry = (volatile uint32_t *)to_kseg1(tableAddr);
    const uint32_t original = *entry;
    g_originalSetVideoMode = (set_video_mode_fn)to_kseg0((uintptr_t)original);
    *entry = to_kseg0((uintptr_t)set_video_mode_hook);

    // Force the GPU to our preference right away after install.
    g_lastRequestedMode = 0u;
    apply_forced_mode();
}
