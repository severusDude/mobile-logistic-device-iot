#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// I2C address
#define PN532_I2C_ADDRESS 0x24

// Command codes
#define PN532_COMMAND_GETFIRMWAREVERSION 0x02
#define PN532_COMMAND_SAMCONFIGURATION 0x14
#define PN532_COMMAND_INLISTPASSIVETARGET 0x4A
#define PN532_COMMAND_INDATAEXCHANGE 0x40
#define PN532_COMMAND_MIFARE_READ 0x30
#define PN532_COMMAND_MIFARE_WRITE 0xA0

// Response codes
#define PN532_RESPONSE_GETFIRMWAREVERSION (PN532_COMMAND_GETFIRMWAREVERSION + 1)
#define PN532_RESPONSE_SAMCONFIGURATION (PN532_COMMAND_SAMCONFIGURATION + 1)
#define PN532_RESPONSE_INLISTPASSIVETARGET (PN532_COMMAND_INLISTPASSIVETARGET + 1)
#define PN532_RESPONSE_INDATAEXCHANGE (PN532_COMMAND_INDATAEXCHANGE + 1)

// Constants
#define PN532_PREAMBLE 0x00
#define PN532_STARTCODE1 0x00
#define PN532_STARTCODE2 0xFF
#define PN532_POSTAMBLE 0x00
#define PN532_HOSTTOPN532 0xD4
#define PN532_PN532TOHOST 0xD5
// #define PN532_ACK_PACKET_SIZE 6
// #define PN532_ACK_PACKET {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00}

// Pure 6-byte ACK frame (no embedded ready byte).
// The ready byte 0x01 is served separately at the start of every I2C read
// transaction so that the Adafruit library's isready() poll and its
// readdata() call each receive a fresh 0x01 leader as they expect.
#define PN532_ACK_PACKET_SIZE 6
#define PN532_ACK_PACKET {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00}

// Card types
#define CARD_TYPE_MIFARE_CLASSIC 0x00

// Virtual card states
#define CARD_STATE_ABSENT 0
#define CARD_STATE_PRESENT 1

// Define UID sizes
#define UID_SIZE_MIFARE_CLASSIC 4

// Maximum number of virtual cards
#define MAX_VIRTUAL_CARDS 2

// Size of a Mifare Classic 1K card (in bytes)
#define MIFARE_1K_SIZE 1024
#define MIFARE_CLASSIC_BLOCK_SIZE 16
#define MIFARE_CLASSIC_BLOCKS_PER_SECTOR 4
#define MIFARE_CLASSIC_SECTOR_COUNT 16

typedef struct
{
    uint8_t state;
    uint8_t uid[7];
    uint8_t uid_length;
    uint8_t card_type;
    uint8_t memory[MIFARE_1K_SIZE]; // Memory contents for Mifare Classic 1K
} virtual_card_t;

typedef struct
{
    pin_t pin_irq;
    pin_t pin_reset;
    pin_t pin_req;

    uint32_t card1_button;
    uint32_t card2_button;
    uint32_t reset_button;

    i2c_dev_t i2c;
    timer_t timer;

    // Communication state
    bool waiting_for_ack;
    bool waiting_for_response;
    uint8_t command;
    uint8_t command_data[64];
    uint8_t command_length;

    uint8_t response_data[64];
    uint8_t response_length;

    // Card simulation
    virtual_card_t cards[MAX_VIRTUAL_CARDS];
    int active_card_index;

    // Card last command
    uint8_t last_command;
    uint8_t last_sector;
    uint8_t last_block;

    uint8_t ack_index;
    uint8_t response_frame[128]; // full framed response goes here
    uint16_t frame_length;       // length of the full frame
    uint16_t frame_index;        // for sequential I2C reads (optional but recommended)
} chip_state_t;

// Function prototypes
static bool on_i2c_connect(void *user_data, uint32_t address, bool read);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void on_i2c_disconnect(void *user_data);
static void on_timer(void *user_data);
static void process_command(chip_state_t *chip);
static bool authenticate_sector(chip_state_t *chip, int card_index, int sector, uint8_t *key);
static void initialize_virtual_card(virtual_card_t *card, int card_number);

// Utility function to convert a hex value to ASCII for printing
static char hexchar(uint8_t val)
{
    if (val < 10)
        return '0' + val;
    return 'A' + (val - 10);
}

// Builds the full PN532 I2C frame (ready byte + preamble + checksums + postamble)
static void prepare_full_response(chip_state_t *chip)
{
    if (chip->response_length == 0)
    {
        chip->frame_length = 0;
        return;
    }

    uint8_t payload_len = chip->response_length;
    uint8_t len = 1 + payload_len; // TFI + payload
    uint8_t lcs = (uint8_t)(0x100 - len);

    uint16_t sum = 0xD5; // TFI
    for (uint8_t i = 0; i < payload_len; i++)
    {
        sum += chip->response_data[i];
    }
    uint8_t dcs = (uint8_t)(0x100 - (sum & 0xFF));

    uint8_t *frame = chip->response_frame;
    uint16_t idx = 0;

    // === FULL CORRECT PN532 RESPONSE FRAME ===
    frame[idx++] = 0x00; // Preamble
    frame[idx++] = 0x00; // StartCode1
    frame[idx++] = 0xFF; // StartCode2
    frame[idx++] = len;
    frame[idx++] = lcs;
    frame[idx++] = 0xD5; // TFI (PN532 → host)

    for (uint8_t i = 0; i < payload_len; i++)
    {
        frame[idx++] = chip->response_data[i];
    }

    frame[idx++] = dcs;
    frame[idx++] = 0x00; // Postamble

    chip->frame_length = idx;
    chip->frame_index = 0;

    printf("[chip-pn532] Response frame prepared (%d bytes): ", idx);
    for (uint16_t i = 0; i < idx && i < 32; i++)
    {
        printf("%02X ", frame[i]);
    }
    printf("\n");
}

void chip_init()
{
    chip_state_t *chip = malloc(sizeof(chip_state_t));
    memset(chip, 0, sizeof(chip_state_t));

    // Initialize pins
    chip->pin_irq = pin_init("IRQ", OUTPUT_HIGH);
    chip->pin_reset = pin_init("RST", INPUT);
    chip->pin_req = pin_init("REQ", INPUT); // Hardware request pin

    // Initialize attributes
    chip->card1_button = attr_init("card1", 0);
    chip->card2_button = attr_init("card2", 0);
    chip->reset_button = attr_init("reset", 0);

    // Initialize I2C interface
    const i2c_config_t i2c_config = {
        .user_data = chip,
        .address = PN532_I2C_ADDRESS,
        .scl = pin_init("SCL", INPUT_PULLUP),
        .sda = pin_init("SDA", INPUT_PULLUP),
        .connect = on_i2c_connect,
        .read = on_i2c_read,
        .write = on_i2c_write,
        .disconnect = on_i2c_disconnect,
    };
    chip->i2c = i2c_init(&i2c_config);

    // Initialize timer
    const timer_config_t timer_config = {
        .callback = on_timer,
        .user_data = chip,
    };
    chip->timer = timer_init(&timer_config);

    // Initialize virtual cards
    for (int i = 0; i < MAX_VIRTUAL_CARDS; i++)
    {
        initialize_virtual_card(&chip->cards[i], i + 1);
    }

    // Set to no active card initially
    chip->active_card_index = -1;

    printf("PN532 NFC/RFID Custom Chip initialized\n");
    pin_write(chip->pin_irq, LOW);
}

static void initialize_virtual_card(virtual_card_t *card, int card_number)
{
    card->state = CARD_STATE_ABSENT;
    card->card_type = CARD_TYPE_MIFARE_CLASSIC;
    card->uid_length = UID_SIZE_MIFARE_CLASSIC;

    // Generate a unique UID for each card
    if (card_number == 1)
    {
        // Card 1 UID: DE AD BE EF
        card->uid[0] = 0xDE;
        card->uid[1] = 0xAD;
        card->uid[2] = 0xBE;
        card->uid[3] = 0xEF;
        printf("Card 1 UID: DE AD BE EF\n");
    }
    else
    {
        // Card 2 UID: CA FE BA BE
        card->uid[0] = 0xCA;
        card->uid[1] = 0xFE;
        card->uid[2] = 0xBA;
        card->uid[3] = 0xBE;
        printf("Card 2 UID: CA FE BA BE\n");
    }

    // Initialize card memory
    memset(card->memory, 0, MIFARE_1K_SIZE);

    // Default card memory initialization:
    // Manufacturer block (block 0)
    memcpy(card->memory, card->uid, 4);

    // Set up all sector trailers (every 4th block) with default keys
    for (int sector = 0; sector < MIFARE_CLASSIC_SECTOR_COUNT; sector++)
    {
        int block = sector * MIFARE_CLASSIC_BLOCKS_PER_SECTOR + (MIFARE_CLASSIC_BLOCKS_PER_SECTOR - 1);
        int offset = block * MIFARE_CLASSIC_BLOCK_SIZE;

        // Key A - Default 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF
        for (int i = 0; i < 6; i++)
        {
            card->memory[offset + i] = 0xFF;
        }

        // Access bits - Default permissions
        card->memory[offset + 6] = 0xFF;
        card->memory[offset + 7] = 0x07;
        card->memory[offset + 8] = 0x80;
        card->memory[offset + 9] = 0x69;

        // Key B - Default 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF
        for (int i = 10; i < 16; i++)
        {
            card->memory[offset + i] = 0xFF;
        }
    }
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read)
{
    return true; // Always ACK
}

static uint8_t on_i2c_read(void *user_data)
{
    chip_state_t *chip = (chip_state_t *)user_data;

    // Update virtual card state when idle
    if (!chip->waiting_for_ack && !chip->waiting_for_response)
    {
        uint32_t card1_state = attr_read(chip->card1_button);
        uint32_t card2_state = attr_read(chip->card2_button);
        uint32_t reset_state = attr_read(chip->reset_button);

        if (reset_state)
        {
            chip->active_card_index = -1;
            chip->cards[0].state = CARD_STATE_ABSENT;
            chip->cards[1].state = CARD_STATE_ABSENT;
            printf("Card field reset - all cards removed\n");
        }

        if (card1_state && chip->cards[0].state == CARD_STATE_ABSENT)
        {
            chip->cards[0].state = CARD_STATE_PRESENT;
            chip->active_card_index = 0;
            chip->cards[1].state = CARD_STATE_ABSENT;
            printf("Card 1 placed in field\n");
        }

        if (card2_state && chip->cards[1].state == CARD_STATE_ABSENT)
        {
            chip->cards[1].state = CARD_STATE_PRESENT;
            chip->active_card_index = 1;
            chip->cards[0].state = CARD_STATE_ABSENT;
            printf("Card 2 placed in field\n");
        }
    }

    // === Serve ACK ===
    // The Adafruit library issues two separate I2C read transactions per command:
    //   Transaction 1 — isready():    reads 1 byte, expects 0x01 (= ready)
    //   Transaction 2 — readdata(6):  reads 7 bytes internally (rbuff[n+1]);
    //                                 rbuff[0] is a fresh ready byte (discarded),
    //                                 rbuff[1..6] must equal {00,00,FF,00,FF,00}
    //
    // on_i2c_disconnect() resets ack_index to 0 between transactions, so every
    // new I2C read starts at index 0 = ready byte, then 1..6 = ACK frame bytes.
    if (chip->waiting_for_ack)
    {
        static const uint8_t ack_packet[] = PN532_ACK_PACKET;

        if (chip->ack_index == 0)
        {
            // Start of any I2C read transaction while ACK is pending:
            // always hand back the ready byte first.
            chip->ack_index = 1;
            printf("[chip-pn532] Serving ACK ready byte 0x01 (transaction start)\n");
            return 0x01;
        }

        // Indices 1..6: stream the 6-byte ACK frame
        uint8_t frame_pos = chip->ack_index - 1; // 0-based into ack_packet[]
        uint8_t byte = ack_packet[frame_pos];
        chip->ack_index++;
        printf("[chip-pn532] Serving ACK frame byte %d/6: 0x%02X\n", frame_pos, byte);

        if (chip->ack_index > PN532_ACK_PACKET_SIZE)
        {
            // All 6 ACK frame bytes have been delivered; the ACK exchange is done.
            chip->ack_index = 0;
            chip->waiting_for_ack = false;
            printf("[chip-pn532] Full ACK delivered for command 0x%02X\n", chip->command);
        }
        return byte;
    }

    // === Serve response (ready byte first, then full PN532 frame) ===
    if (chip->waiting_for_response && chip->frame_length > 0)
    {
        if (chip->frame_index == 0)
        {
            // First read = ready byte (what isready() and readdata's leading byte expect)
            chip->frame_index = 1;
            printf("[chip-pn532] Serving response ready byte 0x01\n");
            return 0x01;
        }

        // Subsequent reads = actual frame bytes
        if (chip->frame_index - 1 < chip->frame_length)
        {
            uint8_t byte = chip->response_frame[chip->frame_index - 1];
            chip->frame_index++;

            if (chip->frame_index - 1 >= chip->frame_length)
            {
                chip->waiting_for_response = false;
                chip->frame_index = 0;
                printf("[chip-pn532] Full response frame delivered (%d bytes)\n", chip->frame_length);
            }
            return byte;
        }
    }

    // Idle / no data ready → return 0x00 (critical for clean polling)
    return 0x00;
}

static bool on_i2c_write(void *user_data, uint8_t data)
{
    chip_state_t *chip = (chip_state_t *)user_data;

    static uint8_t frame_state = 0;
    static uint8_t frame_length = 0;
    static uint8_t frame_length_checksum = 0;
    static uint8_t frame_data_index = 0;
    static uint8_t frame_checksum = 0;

    // State machine for I2C write frame processing
    switch (frame_state)
    {
    case 0: // Preamble
        if (data == PN532_PREAMBLE)
        {
            frame_state = 1;
        }
        break;

    case 1: // Start code 1
        if (data == PN532_STARTCODE1)
        {
            frame_state = 2;
        }
        else
        {
            frame_state = 0; // Reset
        }
        break;

    case 2: // Start code 2
        if (data == PN532_STARTCODE2)
        {
            frame_state = 3;
        }
        else
        {
            frame_state = 0; // Reset
        }
        break;

    case 3: // Length
        frame_length = data;
        frame_state = 4;
        break;

    case 4: // Length checksum
        frame_length_checksum = data;
        if ((frame_length + frame_length_checksum) & 0xFF)
        {
            // Length checksum error
            frame_state = 0;
        }
        else
        {
            frame_state = 5;
            frame_data_index = 0;
            frame_checksum = 0;
        }
        break;

    case 5: // TFI (Host to PN532)
        if (data == PN532_HOSTTOPN532)
        {
            frame_state = 6;
            frame_checksum = data;
        }
        else
        {
            frame_state = 0; // Reset
        }
        break;

    case 6: // Command byte
        chip->command = data;
        chip->command_data[0] = data;
        frame_data_index = 1;
        frame_checksum += data;
        frame_state = 7;
        break;

    case 7: // Command data
        if (frame_data_index < frame_length - 1)
        { // -1 because we already got command byte
            chip->command_data[frame_data_index] = data;
            frame_checksum += data;
            frame_data_index++;
        }
        else
        {
            // This should be the checksum
            if ((frame_checksum + data) & 0xFF)
            {
                // Checksum error
                frame_state = 0;
            }
            else
            {
                frame_state = 8;
            }
        }
        break;

    case 8: // Postamble
        if (data == PN532_POSTAMBLE)
        {
            // Valid frame received
            chip->command_length = frame_length - 1;

            // (response frame is now ready before library starts reading ACK)
            process_command(chip);

            chip->waiting_for_ack = true;
            chip->ack_index = 0; // ← CRITICAL: reset for every new command

            // Keep tiny processing delay for realism (can be 1-10ms)
            timer_start(chip->timer, 1000, false);

            printf("[chip-pn532] Command 0x%02X received → ACK + response prepared immediately\n", chip->command);
        }
        frame_state = 0; // Reset
        break;
    }

    return true; // Always ACK
}

static void on_i2c_disconnect(void *user_data)
{
    chip_state_t *chip = (chip_state_t *)user_data;

    if (chip->waiting_for_ack)
    {
        chip->ack_index = 0;
        printf("[chip-pn532] I2C disconnect mid-ACK — ack_index reset to 0 for next transaction\n");
    }
    if (chip->waiting_for_response)
    {
        chip->frame_index = 0;
        printf("[chip-pn532] I2C disconnect mid-response — frame_index reset to 0 for next transaction\n");
    }
}

static void on_timer(void *user_data)
{
    chip_state_t *chip = (chip_state_t *)user_data;

    // Set the IRQ pin low to indicate ready
    pin_write(chip->pin_irq, LOW);
}

static void process_command(chip_state_t *chip)
{
    printf("Processing command: 0x%02X\n", chip->command);

    chip->waiting_for_response = true;
    chip->response_length = 0; // will be set by each case

    switch (chip->command)
    {
    case PN532_COMMAND_GETFIRMWAREVERSION:
    {
        chip->response_data[0] = PN532_RESPONSE_GETFIRMWAREVERSION;
        chip->response_data[1] = 0x32; // IC version: PN532
        chip->response_data[2] = 0x01; // Firmware version 1
        chip->response_data[3] = 0x06; // Firmware revision 6
        chip->response_data[4] = 0x07; // Various capabilities
        chip->response_length = 5;

        printf("Responded with firmware version: PN532 v1.6\n");
        break;
    }

    case PN532_COMMAND_SAMCONFIGURATION:
    {
        chip->response_data[0] = PN532_RESPONSE_SAMCONFIGURATION;
        chip->response_data[1] = 0x00; // Status OK
        chip->response_length = 2;

        printf("Configured SAM\n");
        break;
    }

    case PN532_COMMAND_INLISTPASSIVETARGET:
    {
        uint8_t max_targets = chip->command_data[1];
        uint8_t card_baud_rate = chip->command_data[2];

        chip->response_data[0] = PN532_RESPONSE_INLISTPASSIVETARGET;

        // Check if we have an active card
        if (chip->active_card_index >= 0 &&
            chip->cards[chip->active_card_index].state == CARD_STATE_PRESENT)
        {
            virtual_card_t *active_card = &chip->cards[chip->active_card_index];

            chip->response_data[1] = 0x01; // Number of targets found
            chip->response_data[2] = 0x01; // Target number

            if (card_baud_rate == 0) // Mifare cards (ISO/IEC 14443A)
            {
                // Match the packet layout expected by Adafruit_PN532:
                // count, target, SENS_RES[2], SEL_RES, NFCID Length, NFCID bytes
                chip->response_data[3] = 0x00;                         // Card ATQA MSB
                chip->response_data[4] = 0x04;                         // Card ATQA LSB
                chip->response_data[5] = 0x08;                         // SAK / SEL_RES for Mifare Classic 1K
                chip->response_data[6] = active_card->uid_length;      // UID length

                // Copy UID
                for (int i = 0; i < active_card->uid_length; i++)
                {
                    chip->response_data[7 + i] = active_card->uid[i];
                }

                chip->response_length = 7 + active_card->uid_length;

                printf("Card found - UID: ");
                for (int i = 0; i < active_card->uid_length; i++)
                {
                    printf("%02X ", active_card->uid[i]);
                }
                printf("\n");
            }
            else
            {
                // Unsupported card type
                chip->response_data[1] = 0x00; // No targets found
                chip->response_length = 2;
                printf("Unsupported card type requested\n");
            }
        }
        else
        {
            // No card present
            chip->response_data[1] = 0x00; // No targets found
            chip->response_length = 2;
            printf("No card found in field\n");
        }
        break;
    }

    case PN532_COMMAND_INDATAEXCHANGE:
    {
        uint8_t target_number = chip->command_data[1];
        uint8_t mifare_command = chip->command_data[2];

        chip->response_data[0] = PN532_RESPONSE_INDATAEXCHANGE;

        // Make sure we have an active card
        if (chip->active_card_index >= 0 &&
            chip->cards[chip->active_card_index].state == CARD_STATE_PRESENT)
        {
            virtual_card_t *active_card = &chip->cards[chip->active_card_index];

            switch (mifare_command)
            {
            case 0x60: // Auth A
            case 0x61: // Auth B
            {
                uint8_t block_number = chip->command_data[3];
                uint8_t *key = &chip->command_data[4]; // 6-byte key
                int sector = block_number / 4;

                chip->last_command = mifare_command;
                chip->last_block = block_number;
                chip->last_sector = sector;

                if (authenticate_sector(chip, chip->active_card_index, sector, key))
                {
                    chip->response_data[1] = 0x00; // Authentication successful
                    printf("Authentication successful for sector %d\n", sector);
                }
                else
                {
                    chip->response_data[1] = 0x01; // Authentication failed
                    printf("Authentication failed for sector %d\n", sector);
                }
                chip->response_length = 2;
                break;
            }

            case PN532_COMMAND_MIFARE_READ:
            {
                uint8_t block_number = chip->command_data[3];
                int sector = block_number / 4;
                int block_offset = block_number * MIFARE_CLASSIC_BLOCK_SIZE;

                // Check authentication
                if (sector == chip->last_sector)
                {
                    chip->response_data[1] = 0x00; // Status OK

                    // Copy 16 bytes from card memory
                    for (int i = 0; i < MIFARE_CLASSIC_BLOCK_SIZE; i++)
                    {
                        chip->response_data[2 + i] = active_card->memory[block_offset + i];
                    }

                    chip->response_length = 2 + MIFARE_CLASSIC_BLOCK_SIZE;

                    printf("Read block %d from sector %d\n", block_number, sector);
                }
                else
                {
                    chip->response_data[1] = 0x01; // Authentication required
                    chip->response_length = 2;
                    printf("Authentication required for sector %d\n", sector);
                }
                break;
            }

            case PN532_COMMAND_MIFARE_WRITE:
            {
                uint8_t block_number = chip->command_data[3];
                int sector = block_number / 4;
                int block_offset = block_number * MIFARE_CLASSIC_BLOCK_SIZE;

                // Check authentication
                if (sector == chip->last_sector)
                {
                    // Copy 16 bytes to card memory
                    for (int i = 0; i < MIFARE_CLASSIC_BLOCK_SIZE; i++)
                    {
                        active_card->memory[block_offset + i] = chip->command_data[4 + i];
                    }

                    chip->response_data[1] = 0x00; // Status OK
                    chip->response_length = 2;

                    printf("Wrote block %d in sector %d\n", block_number, sector);
                }
                else
                {
                    chip->response_data[1] = 0x01; // Authentication required
                    chip->response_length = 2;
                    printf("Authentication required for sector %d\n", sector);
                }
                break;
            }

            default:
                // Unsupported command
                chip->response_data[1] = 0x01; // Error
                chip->response_length = 2;
                printf("Unsupported Mifare command: 0x%02X\n", mifare_command);
                break;
            }
        }
        else
        {
            // No card present
            chip->response_data[1] = 0x01; // Error
            chip->response_length = 2;
            printf("No card in field for data exchange\n");
        }
        break;
    }

    default:
        printf("Unsupported command: 0x%02X\n", chip->command);
        chip->waiting_for_response = false;
        return;
    }

    // === Build the properly framed response ===
    if (chip->waiting_for_response && chip->response_length > 0)
    {
        prepare_full_response(chip);
        chip->waiting_for_response = true;
        chip->frame_index = 0;
        pin_write(chip->pin_irq, LOW);
    }
}

static bool authenticate_sector(chip_state_t *chip, int card_index, int sector, uint8_t *key)
{
    if (card_index < 0 || card_index >= MAX_VIRTUAL_CARDS)
    {
        return false;
    }

    if (sector < 0 || sector >= MIFARE_CLASSIC_SECTOR_COUNT)
    {
        return false;
    }

    // Get the sector trailer block
    int trailer_block = (sector * MIFARE_CLASSIC_BLOCKS_PER_SECTOR) + (MIFARE_CLASSIC_BLOCKS_PER_SECTOR - 1);
    int trailer_offset = trailer_block * MIFARE_CLASSIC_BLOCK_SIZE;

    // Compare with stored keys (Key A or Key B based on command)
    uint8_t *stored_key;
    if (chip->last_command == 0x60)
    {                                                                 // Auth with Key A
        stored_key = &chip->cards[card_index].memory[trailer_offset]; // First 6 bytes
    }
    else
    {                                                                      // Auth with Key B
        stored_key = &chip->cards[card_index].memory[trailer_offset + 10]; // Last 6 bytes
    }

    // Compare keys
    for (int i = 0; i < 6; i++)
    {
        if (key[i] != stored_key[i])
        {
            return false;
        }
    }

    return true;
}
