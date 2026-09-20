/**
 * @file ftb-8-md.c
 * @brief Implementation of the Futaba 8-MD-06INK VFD display driver.
 */

#include "ftb-8-md.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

#include <string.h>

static const char *TAG = "FTB8MD";

/** @brief Number of digits on the display */
#define FTB8MD_NUM_DIGITS 8

/** @brief Maximum dimming level */
#define FTB8MD_MAX_DIMMING 240

/** @brief Maximum SPI clock frequency (500 kHz) */
#define FTB8MD_SPI_CLOCK_HZ (500 * 1000)

/* Command prefixes */
#define CMD_PREFIX_DCRAM 0x01 /**< DCRAM write command prefix (001) */
#define CMD_PREFIX_CGRAM 0x02 /**< CGRAM write command prefix (010) */
#define CMD_PREFIX_ADRAM 0x03 /**< ADRAM write command prefix (011) */
#define CMD_PREFIX_URAM 0x04  /**< URAM write command prefix (100) */

/* Control commands */
#define CMD_DIGIT_SET 0xE0    /**< Number of digits setting command */
#define CMD_DIMMING 0xE4      /**< Dimming level setting command */
#define CMD_DISPLAY_ON 0xE8   /**< Display on command */
#define CMD_DISPLAY_OFF 0xEA  /**< Display off command */
#define CMD_MODE_NORMAL 0xEC  /**< Normal mode command */
#define CMD_MODE_STANDBY 0xED /**< Standby mode command */

/**
 * @brief Send a command to the VFD display.
 *
 * @param handle SPI device handle
 * @param cmd Pointer to the command data
 * @param len Length of the command in bytes
 * @return ESP_OK on success, or an error code on failure
 */
static esp_err_t ftb8md_send_command(spi_device_handle_t handle, const uint8_t *cmd, size_t len)
{
    if (handle == NULL || cmd == NULL || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = cmd,
    };

    return spi_device_transmit(handle, &trans);
}

spi_device_handle_t ftb8md_device_register(spi_host_device_t host_id, int cs_pin, int reset_pin)
{
    if (reset_pin >= 0)
    {
        // Configure reset pin
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << reset_pin,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure reset pin: %s", esp_err_to_name(ret));
            return NULL;
        }

        // Perform hardware reset
        gpio_set_level((gpio_num_t)reset_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)reset_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    spi_device_handle_t handle = NULL;

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = FTB8MD_SPI_CLOCK_HZ,
        .mode = 3, // CPOL=1, CPHA=1
        .spics_io_num = cs_pin,
        .queue_size = 1,
        .flags = SPI_DEVICE_BIT_LSBFIRST,
    };

    esp_err_t ret = spi_bus_add_device(host_id, &dev_cfg, &handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return NULL;
    }

    // Initialize display: set 8 digits
    DisplayCommand cmd = {0};
    cmd.ctrl.prefix = CMD_DIGIT_SET;
    cmd.ctrl.arg = FTB8MD_NUM_DIGITS - 1; // 0-7 means 1-8 digits
    ret = ftb8md_send_command(handle, cmd.raw, 2);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set digit count: %s", esp_err_to_name(ret));
    }

    // Set maximum brightness
    ftb8md_set_dimming(handle, FTB8MD_MAX_DIMMING);

    // Turn on display with full brightness
    cmd.ctrl.prefix = CMD_DISPLAY_ON;
    ret = ftb8md_send_command(handle, cmd.raw, 2);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "VFD display initialized successfully");
    return handle;
}

esp_err_t ftb8md_show_string(spi_device_handle_t handle, int digit, const char *str)
{
    if (handle == NULL || str == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (digit < 0 || digit >= FTB8MD_NUM_DIGITS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DisplayCommand cmd = {0};
    cmd.dcram_write.byte1.prefix = CMD_PREFIX_DCRAM;
    cmd.dcram_write.byte1.digit = digit;

    size_t max_chars = FTB8MD_NUM_DIGITS - digit;
    size_t str_len = strlen(str);
    size_t chars_to_write = (str_len < max_chars) ? str_len : max_chars;

    for (size_t i = 0; i < chars_to_write; i++)
    {
        // Direct ASCII mapping (display typically uses ASCII-compatible encoding)
        cmd.dcram_write.chr[i] = (uint8_t)str[i];
    }

    // Command byte + character data
    return ftb8md_send_command(handle, cmd.raw, 1 + chars_to_write);
}

esp_err_t ftb8md_set_dimming(spi_device_handle_t handle, uint8_t level)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DisplayCommand cmd = {0};
    cmd.ctrl.prefix = CMD_DIMMING;
    cmd.ctrl.arg = level > FTB8MD_MAX_DIMMING ? FTB8MD_MAX_DIMMING : level;

    return ftb8md_send_command(handle, cmd.raw, 2);
}

esp_err_t ftb8md_enter_standby(spi_device_handle_t handle, bool standby)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DisplayCommand cmd = {0};
    cmd.ctrl.prefix = standby ? CMD_MODE_STANDBY : CMD_MODE_NORMAL;

    return ftb8md_send_command(handle, cmd.raw, 2);
}

esp_err_t ftb8md_set_display_power(spi_device_handle_t handle, bool on)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DisplayCommand cmd = {0};
    cmd.ctrl.prefix = on ? CMD_DISPLAY_ON : CMD_DISPLAY_OFF;

    return ftb8md_send_command(handle, cmd.raw, 2);
}

esp_err_t ftb8md_set_dot(spi_device_handle_t handle, int digit, bool dot_on)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (digit < 0 || digit >= FTB8MD_NUM_DIGITS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DisplayCommand cmd = {0};
    cmd.adram_write.byte1.reserved = CMD_PREFIX_ADRAM;
    cmd.adram_write.byte1.digit = digit;
    cmd.adram_write.byte2.pin = dot_on ? 0x01 : 0x00; // E0 controls decimal point

    return ftb8md_send_command(handle, cmd.raw, 2);
}

esp_err_t ftb8md_set_segment(spi_device_handle_t handle, int digit, uint8_t segments)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (digit < 0 || digit >= FTB8MD_NUM_DIGITS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Write directly to DCRAM with raw segment data
    DisplayCommand cmd = {0};
    cmd.dcram_write.byte1.prefix = CMD_PREFIX_DCRAM;
    cmd.dcram_write.byte1.digit = digit;
    cmd.dcram_write.chr[0] = segments;

    return ftb8md_send_command(handle, cmd.raw, 2);
}

esp_err_t ftb8md_clear_display(spi_device_handle_t handle)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    // Clear all digits by writing spaces
    DisplayCommand cmd = {0};
    cmd.dcram_write.byte1.prefix = CMD_PREFIX_DCRAM;
    cmd.dcram_write.byte1.digit = 0;

    // Fill with space characters (0x20)
    memset(cmd.dcram_write.chr, 0x20, FTB8MD_NUM_DIGITS);

    ret = ftb8md_send_command(handle, cmd.raw, 1 + FTB8MD_NUM_DIGITS);
    if (ret != ESP_OK)
    {
        return ret;
    }

    // Clear all decimal points
    for (int i = 0; i < FTB8MD_NUM_DIGITS; i++)
    {
        ret = ftb8md_set_dot(handle, i, false);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return ESP_OK;
}

esp_err_t ftb8md_write_custom_char(spi_device_handle_t handle, int char_index, const uint8_t grid_data[5])
{
    if (handle == NULL || grid_data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (char_index < 0 || char_index > 7)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DisplayCommand cmd = {0};
    cmd.cgram_write.byte1.prefix = CMD_PREFIX_CGRAM;
    cmd.cgram_write.byte1.addr = char_index;

    memcpy(cmd.cgram_write.data, grid_data, 5);

    return ftb8md_send_command(handle, cmd.raw, 6);
}

esp_err_t ftb8md_set_addressed_char(spi_device_handle_t handle, int digit, int char_index)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (digit < 0 || digit >= FTB8MD_NUM_DIGITS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (char_index < 0 || char_index > 7)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DisplayCommand cmd = {0};
    cmd.dcram_write.byte1.prefix = CMD_PREFIX_DCRAM;
    cmd.dcram_write.byte1.digit = digit;
    // CGRAM characters are addressed at 0x00-0x07
    cmd.dcram_write.chr[0] = (uint8_t)char_index;

    return ftb8md_send_command(handle, cmd.raw, 2);
}
