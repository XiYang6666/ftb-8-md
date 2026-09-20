/**
 * @file ftb-8-md.h
 * @brief Driver for Futaba 8-MD-06INK VFD (Vacuum Fluorescent Display) module.
 *
 * This driver provides an interface to control the Futaba 8-MD-06INK VFD display
 * via SPI communication. The display supports 8 digits with customizable characters
 * and brightness control.
 *
 * @note SPI Timing Specifications:
 *       - CS (Chip Select) is active LOW
 *       - Byte order: LSB First
 *       - Data is sampled on the rising edge of the clock
 *       - Clock idles HIGH (CPOL=1, CPHA=1)
 *       - Maximum clock frequency: 0.5 MHz
 */

#pragma once

#include "esp_err.h"
#include "driver/spi_master.h"

#include <stdint.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Union representing all possible display command formats.
 *
 * This union provides a structured way to construct various commands
 * for the VFD display controller. Each member corresponds to a specific
 * command type with its own data format.
 */
typedef union
{
    uint8_t raw[9]; /**< Raw byte array, maximum 9 bytes per command */

    /**
     * @brief DCRAM (Display Character RAM) data write command.
     *
     * Used to write character codes to the display at specified digit positions.
     * Command prefix: 001 (bits B7-B5)
     */
    struct
    {
        struct
        {
            uint8_t digit : 5;  /**< Digit position (X4-X0, bits B4-B0) */
            uint8_t prefix : 3; /**< Command prefix: 001 (bits B7-B5) */
        } byte1;
        uint8_t chr[8]; /**< Character data bytes (2nd to 9th byte) */
    } dcram_write;

    /**
     * @brief CGRAM (Character Generator RAM) data write command.
     *
     * Used to define custom character patterns in the character generator RAM.
     * Command prefix: 010 (bits B7-B5)
     */
    struct
    {
        struct
        {
            uint8_t addr : 3;     /**< CGRAM address (bits B2-B0) */
            uint8_t reserved : 2; /**< Reserved bits */
            uint8_t prefix : 3;   /**< Command prefix: 010 (bits B7-B5) */
        } byte1;
        uint8_t data[5]; /**< Character pattern data (2nd to 6th byte) */
    } cgram_write;

    /**
     * @brief ADRAM (Additional Display RAM) data write command.
     *
     * Used to control additional display elements like decimal points.
     * Command prefix: 011 (bits B7-B5)
     */
    struct
    {
        struct
        {
            uint8_t digit : 5;    /**< Digit position (bits B4-B0) */
            uint8_t reserved : 3; /**< Command prefix: 011 (bits B7-B5) */
        } byte1;
        struct
        {
            uint8_t pin : 4;      /**< Pin selection (E3-E0) */
            uint8_t reserved : 4; /**< Reserved bits */
        } byte2;
    } adram_write;

    /**
     * @brief URAM (User RAM) data write command.
     *
     * Used to write data to user-defined RAM areas for grid control.
     * Command prefix: 100 (bits B7-B5)
     */
    struct
    {
        struct
        {
            uint8_t addr : 3;     /**< URAM address (bits B2-B0) */
            uint8_t reserved : 2; /**< Reserved bits */
            uint8_t prefix : 3;   /**< Command prefix: 100 (bits B7-B5) */
        } byte1;
        uint8_t grid_l; /**< Grid data for grids 1G-8G */
        uint8_t grid_h; /**< Grid data for grids 9G-16G */
    } uram_write;

    /**
     * @brief Control command structure.
     *
     * Used for configuration commands such as digit count setting,
     * dimming level, display on/off, and standby mode.
     */
    struct
    {
        uint8_t prefix; /**< Command identifier (bits B7-B2) */
        uint8_t arg;    /**< Command argument (second byte) */
    } ctrl;

} DisplayCommand;

static_assert(sizeof(DisplayCommand) == 9, "DisplayCommand size must be 9 bytes");

/**
 * @brief Register and initialize the VFD display device on the SPI bus.
 *
 * This function configures the SPI device with appropriate settings for the
 * Futaba 8-MD-06INK VFD display (LSB first, CPOL=1, CPHA=1, 500kHz max).
 *
 * @param host_id The SPI host peripheral to use (e.g., SPI2_HOST, SPI3_HOST).
 * @param cs_pin The GPIO pin number to use as chip select (CS).
 * @param reset_pin The GPIO pin number to use as reset.
 * @return The SPI device handle on success, or NULL on failure.
 *
 * @note The SPI bus must be initialized before calling this function.
 * @see spi_bus_initialize()
 */
spi_device_handle_t ftb8md_device_register(spi_host_device_t host_id, int cs_pin, int reset_pin);

/**
 * @brief Display a string on the VFD starting at the specified digit position.
 *
 * This function writes a null-terminated string to the display's DCRAM,
 * starting from the specified digit position. Characters are mapped from
 * ASCII to the display's character set.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @param digit The starting digit position (0-7, where 0 is the leftmost digit).
 * @param str Pointer to the null-terminated string to display.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid parameter (NULL handle or string)
 *      - ESP_FAIL: SPI communication error
 *
 * @note Characters beyond the display width will be truncated.
 */
esp_err_t ftb8md_show_string(spi_device_handle_t handle, int digit, const char *str);

/**
 * @brief Set the display brightness (dimming) level.
 *
 * This function adjusts the brightness of the VFD display by controlling
 * the duty cycle of the display grid.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @param level Brightness level (0-240, where 0 is dimmest and 240 is brightest).
 *              Values above 240 will be capped to 240.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid handle
 *      - ESP_FAIL: SPI communication error
 */
esp_err_t ftb8md_set_dimming(spi_device_handle_t handle, uint8_t level);

/**
 * @brief Enter or exit standby (low power) mode.
 *
 * In standby mode, the display is turned off and power consumption is reduced.
 * The display contents are preserved and will be restored when exiting standby.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @param standby Set to true to enter standby mode, false to exit and resume normal operation.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid handle
 *      - ESP_FAIL: SPI communication error
 */
esp_err_t ftb8md_enter_standby(spi_device_handle_t handle, bool standby);

/**
 * @brief Turn the display on or off.
 *
 * This function controls the display power state. When off, all segments are
 * turned off but the display contents in memory are preserved. This is different
 * from standby mode which also reduces overall power consumption.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @param on Set to true to turn on the display, false to turn it off.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid handle
 *      - ESP_FAIL: SPI communication error
 *
 * @note Use ftb8md_enter_standby() for deeper power saving when display is not needed.
 * @see ftb8md_enter_standby()
 */
esp_err_t ftb8md_set_display_power(spi_device_handle_t handle, bool on);

/**
 * @brief Set or clear the decimal point for a specific digit.
 *
 * This function controls the decimal point (dot) segment associated with
 * a specific digit position on the display.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @param digit The digit position (0-7) for which to set the decimal point.
 * @param dot_on Set to true to turn on the decimal point, false to turn it off.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid handle or digit out of range
 *      - ESP_FAIL: SPI communication error
 */
esp_err_t ftb8md_set_dot(spi_device_handle_t handle, int digit, bool dot_on);

/**
 * @brief Directly control individual segments of a digit.
 *
 * This function allows direct control over the individual segments of a
 * specific digit, enabling custom patterns or animations.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @param digit The digit position (0-7) to control.
 * @param segments Bitmask representing segment states (each bit controls one segment).
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid handle or digit out of range
 *      - ESP_FAIL: SPI communication error
 *
 * @note Segment bit mapping depends on the specific display hardware.
 */
esp_err_t ftb8md_set_segment(spi_device_handle_t handle, int digit, uint8_t segments);

/**
 * @brief Clear all digits on the display.
 *
 * This function turns off all segments and decimal points on the display,
 * effectively clearing the entire screen.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid handle
 *      - ESP_FAIL: SPI communication error
 */
esp_err_t ftb8md_clear_display(spi_device_handle_t handle);

/**
 * @brief Define a custom character pattern in CGRAM.
 *
 * This function writes a custom character pattern to the Character Generator RAM
 * (CGRAM), allowing user-defined characters to be displayed.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @param char_index The CGRAM index (0-7) where the custom character will be stored.
 * @param grid_data Pointer to a 5-byte array containing the character pattern data.
 *                  Each byte represents one column of the character matrix.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid handle, char_index out of range, or NULL grid_data
 *      - ESP_FAIL: SPI communication error
 *
 * @note Custom characters can be displayed using ftb8md_set_addressed_char().
 * @see ftb8md_set_addressed_char()
 */
esp_err_t ftb8md_write_custom_char(spi_device_handle_t handle, int char_index, const uint8_t grid_data[5]);

/**
 * @brief Display a character from CGRAM at the specified digit position.
 *
 * This function displays a custom character (previously defined in CGRAM)
 * at the specified digit position on the display.
 *
 * @param handle The SPI device handle obtained from ftb8md_device_register().
 * @param digit The digit position (0-7) where the character will be displayed.
 * @param char_index The CGRAM index (0-7) of the custom character to display.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid handle, digit or char_index out of range
 *      - ESP_FAIL: SPI communication error
 *
 * @note The custom character must be defined first using ftb8md_write_custom_char().
 * @see ftb8md_write_custom_char()
 */
esp_err_t ftb8md_set_addressed_char(spi_device_handle_t handle, int digit, int char_index);

#ifdef __cplusplus
}
#endif