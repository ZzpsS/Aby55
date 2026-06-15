/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bsp_err_check.h"
#include "esp_check.h"
#include "esp_io_expander_pi4ioe5v6408.h"

#include "bsp/m5stack_tab5.h"

static esp_io_expander_handle_t io_expander = NULL;  // IO Expander
static esp_io_expander_handle_t io_expander1 = NULL;  // IO Expander
static bool io_expander0_defaults_applied = false;
static bool io_expander1_defaults_applied = false;

static esp_err_t bsp_io_expander_apply_tab5_defaults(esp_io_expander_handle_t expander0,
                                                     esp_io_expander_handle_t expander1)
{
    if (expander0) {
        ESP_RETURN_ON_ERROR(expander0->write_direction_reg(expander0, 0x7f),
                            "BSP_IO_EXP", "Set expander0 direction failed");
        ESP_RETURN_ON_ERROR(expander0->write_highz_reg(expander0, 0x00),
                            "BSP_IO_EXP", "Set expander0 high-z failed");
        ESP_RETURN_ON_ERROR(expander0->write_pullup_sel_reg(expander0, 0x7f),
                            "BSP_IO_EXP", "Set expander0 pull select failed");
        ESP_RETURN_ON_ERROR(expander0->write_pullup_en_reg(expander0, 0x7f),
                            "BSP_IO_EXP", "Set expander0 pull enable failed");
        ESP_RETURN_ON_ERROR(expander0->write_output_reg(expander0, 0x76),
                            "BSP_IO_EXP", "Set expander0 output failed");
    }

    if (expander1) {
        ESP_RETURN_ON_ERROR(expander1->write_direction_reg(expander1, 0xb9),
                            "BSP_IO_EXP", "Set expander1 direction failed");
        ESP_RETURN_ON_ERROR(expander1->write_highz_reg(expander1, 0x06),
                            "BSP_IO_EXP", "Set expander1 high-z failed");
        ESP_RETURN_ON_ERROR(expander1->write_pullup_sel_reg(expander1, 0xb9),
                            "BSP_IO_EXP", "Set expander1 pull select failed");
        ESP_RETURN_ON_ERROR(expander1->write_pullup_en_reg(expander1, 0xf9),
                            "BSP_IO_EXP", "Set expander1 pull enable failed");
        ESP_RETURN_ON_ERROR(expander1->write_output_reg(expander1, 0x09),
                            "BSP_IO_EXP", "Set expander1 output failed");
    }

    return ESP_OK;
}

static esp_err_t bsp_io_expander_apply_tab5_defaults_if_ready(void)
{
    if (io_expander && !io_expander0_defaults_applied) {
        ESP_RETURN_ON_ERROR(bsp_io_expander_apply_tab5_defaults(io_expander, NULL),
                            "BSP_IO_EXP", "Apply Tab5 IO expander0 defaults failed");
        io_expander0_defaults_applied = true;
    }

    if (io_expander1 && !io_expander1_defaults_applied) {
        ESP_RETURN_ON_ERROR(bsp_io_expander_apply_tab5_defaults(NULL, io_expander1),
                            "BSP_IO_EXP", "Apply Tab5 IO expander1 defaults failed");
        io_expander1_defaults_applied = true;
    }

    return ESP_OK;
}

esp_io_expander_handle_t bsp_io_expander_init(void)
{
    if (io_expander) {
        BSP_ERROR_CHECK_RETURN_NULL(bsp_io_expander_apply_tab5_defaults_if_ready());
        return io_expander;
    }
    /* Initilize I2C */
    BSP_ERROR_CHECK_RETURN_NULL(bsp_i2c_init());

    BSP_ERROR_CHECK_RETURN_NULL(esp_io_expander_new_i2c_pi4ioe5v6408(bsp_i2c_get_handle(),
                                BSP_IO_EXPANDER_ADDRESS, &io_expander));

    BSP_ERROR_CHECK_RETURN_NULL(bsp_io_expander_apply_tab5_defaults_if_ready());
    return io_expander;
}

esp_io_expander_handle_t bsp_io_expander1_init(void)
{
    if (io_expander1) {
        BSP_ERROR_CHECK_RETURN_NULL(bsp_io_expander_apply_tab5_defaults_if_ready());
        return io_expander1;
    }
    /* Initilize I2C */
    BSP_ERROR_CHECK_RETURN_NULL(bsp_i2c_init());

    BSP_ERROR_CHECK_RETURN_NULL(esp_io_expander_new_i2c_pi4ioe5v6408(bsp_i2c_get_handle(),
                                BSP_IO_EXPANDER_ADDRESS_1, &io_expander1));

    BSP_ERROR_CHECK_RETURN_NULL(bsp_io_expander_apply_tab5_defaults_if_ready());
    return io_expander1;
}
