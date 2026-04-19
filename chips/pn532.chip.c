#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    uint8_t last_command;
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool read)
{
    printf("PN532: Addressed 0x%02X (read=%d)\n", address, read);
    return true; // ACK
}

static uint8_t on_i2c_read(void *user_data)
{
    uint8_t response = 0x42; // exactly what your original simulate function returned
    printf("PN532: Master reading → returning 0x%02X\n", response);
    return response;
}

static bool on_i2c_write(void *user_data, uint8_t data)
{
    chip_state_t *chip = (chip_state_t *)user_data;
    chip->last_command = data;
    printf("PN532: Received command 0x%02X\n", data);
    return true; // ACK
}

static void on_i2c_disconnect(void *user_data)
{
    // optional
}

void chip_init(void)
{
    chip_state_t *chip = malloc(sizeof(chip_state_t));
    chip->last_command = 0;

    const i2c_config_t config = {
        .address = 0x48,
        .sda = pin_init("SDA", INPUT_PULLUP),
        .scl = pin_init("SCL", INPUT_PULLUP),
        .connect = on_i2c_connect,
        .read = on_i2c_read,
        .write = on_i2c_write,
        .disconnect = on_i2c_disconnect,
        .user_data = chip,
    };

    i2c_init(&config);
    printf("PN532 RFID chip initialized at I2C address 0x48\n");
}