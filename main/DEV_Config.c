/*****************************************************************************
* | File      	:   DEV_Config.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2020-02-19
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "DEV_Config.h"
#include "esp_log.h"

static const char *TAG = "DEV_Config";
static spi_device_handle_t s_epd_spi = NULL;

static esp_err_t GPIO_Config(void)
{
    const uint64_t output_pins = (1ULL << EPD_RST_PIN) |
                                 (1ULL << EPD_DC_PIN)  |
                                 (1ULL << EPD_SCK_PIN) |
                                 (1ULL << EPD_MOSI_PIN) |
                                 (1ULL << EPD_CS_PIN);

    gpio_config_t out_conf = {};
    out_conf.pin_bit_mask = output_pins;
    out_conf.mode = GPIO_MODE_OUTPUT;
    out_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    out_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out_conf.intr_type = GPIO_INTR_DISABLE;

    gpio_config_t in_conf = {};
    in_conf.pin_bit_mask = (1ULL << EPD_BUSY_PIN);
    in_conf.mode = GPIO_MODE_INPUT;
    in_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    in_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    in_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&out_conf);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_config(&in_conf);
    if (err != ESP_OK) {
        return err;
    }

    DEV_Digital_Write(EPD_CS_PIN, GPIO_PIN_SET);
    DEV_Digital_Write(EPD_SCK_PIN, GPIO_PIN_RESET);
    DEV_Digital_Write(EPD_MOSI_PIN, GPIO_PIN_RESET);
    DEV_Digital_Write(EPD_RST_PIN, GPIO_PIN_SET);
    DEV_Digital_Write(EPD_DC_PIN, GPIO_PIN_SET);

    return ESP_OK;
}

void GPIO_Mode(UWORD GPIO_Pin, UWORD Mode)
{
    gpio_mode_t mode = (Mode == 0) ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT;
    gpio_set_direction((gpio_num_t)GPIO_Pin, mode);
}

/******************************************************************************
function:	Module Initialize, configure ESP-IDF GPIO and SPI peripherals
******************************************************************************/
UBYTE DEV_Module_Init(void)
{
    if (s_epd_spi != NULL) {
        return 0;
    }

    esp_err_t err = GPIO_Config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO init failed: %s", esp_err_to_name(err));
        return 1;
    }

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = EPD_MOSI_PIN;
    buscfg.miso_io_num = GPIO_NUM_NC;
    buscfg.sclk_io_num = EPD_SCK_PIN;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = 4096;

    err = spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return 1;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = EPD_SPI_CLOCK_HZ;
    devcfg.spics_io_num = EPD_CS_PIN;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX;
    devcfg.queue_size = 1;

    err = spi_bus_add_device(EPD_SPI_HOST, &devcfg, &s_epd_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        s_epd_spi = NULL;
        return 1;
    }

    ESP_LOGI(TAG, "ePaper interface initialized");
    return 0;
}

/******************************************************************************
function:
			SPI read and write
******************************************************************************/
void DEV_SPI_WriteByte(UBYTE data)
{
    if (s_epd_spi == NULL) {
        ESP_LOGE(TAG, "SPI device not initialized");
        return;
    }

    spi_transaction_t t = {};
    t.flags = SPI_TRANS_USE_TXDATA;
    t.length = 8;
    t.tx_data[0] = data;
    spi_device_polling_transmit(s_epd_spi, &t);
}

UBYTE DEV_SPI_ReadByte()
{
    if (s_epd_spi == NULL) {
        ESP_LOGE(TAG, "SPI device not initialized");
        return 0xFF;
    }

    spi_transaction_t t = {};
    t.flags = SPI_TRANS_USE_RXDATA;
    t.length = 8;
    spi_device_polling_transmit(s_epd_spi, &t);
    return t.rx_data[0];
}

void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len)
{
    if (pData == NULL || len == 0) {
        return;
    }

    for (UDOUBLE i = 0; i < len; ++i) {
        DEV_SPI_WriteByte(pData[i]);
    }
}
