/*
 * EPC901.cpp
 *
 *  Created on: Sep 26, 2020
 *      Author: adrian
 */

#include "main.h"
#include "EPC901.hpp"
#include "dwt_stm32_delay.h"
#include <inttypes.h>
#include <cstring>

const uint8_t EPC_I2C_ADDR = 0x15 << 1;

EPC901::EPC901() {
	_chip_rev = 0;
	_i2c_addr = 0;
	_i2c_handle = 0;
}

EPC901::~EPC901() {
}

uint8_t EPC901::init(I2C_HandleTypeDef* i2c_handle, uint8_t i2c_addr, SPI_HandleTypeDef* spi_handle) {
	_i2c_handle = i2c_handle;
	_i2c_addr = i2c_addr << 1;
	_spi_handle = spi_handle;

	// set all pins to sane defaults
	HAL_GPIO_WritePin(PWR_DOWN_GPIO_Port, PWR_DOWN_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(CLR_DATA_GPIO_Port, CLR_DATA_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(CLR_PIX_GPIO_Port, CLR_PIX_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(SHUTTER_GPIO_Port, SHUTTER_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(READ_GPIO_Port, READ_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET);

	// configure and enable SPI peripheral
	__HAL_LOCK(_spi_handle);
	SET_BIT(_spi_handle->Instance->CR2, SPI_RXFIFO_THRESHOLD);
	__HAL_SPI_ENABLE(_spi_handle);
	__HAL_UNLOCK(_spi_handle);

	// init sensor

	_powerUp();

	// read chip revision

	uint8_t revision;
	if (HAL_OK == HAL_I2C_Mem_Read(_i2c_handle, EPC_I2C_ADDR, 0xff, 1, &revision, 1, 100)) {
		_chip_rev = revision;
	} else {
		_powerDown();
		return 1;
	}

	// TODO: oscillator calibration
	// may be negligible for exposure times >1us

	_powerDown();

	return 0;
}

uint16_t EPC901::captureImage(uint32_t exposure_us, uint16_t* buffer) {
	_powerUp();
	_exposeImage(exposure_us);
	uint16_t pixels = _readImage(buffer);
	_powerDown();
	return pixels;
}

uint8_t EPC901::getRevision() {
	return _chip_rev;
}

uint8_t EPC901::readRegister(uint8_t address, uint8_t* value) {
	uint8_t val = 0;
	uint8_t err = 0;
	_powerUp();
	err = HAL_I2C_Mem_Read(_i2c_handle, EPC_I2C_ADDR, address, 1, &val, 1, 100);
	if (err == HAL_OK) {
		*value = val;
	}
	_powerDown();
	return err;
}

uint8_t EPC901::writeRegister(uint8_t address, uint8_t value) {
	uint8_t val = value;
	uint8_t err = 0;
	_powerUp();
	err = HAL_I2C_Mem_Write(_i2c_handle, EPC_I2C_ADDR, address, 1, &val, 1, 100);
	_powerDown();
	return err;
}

uint8_t EPC901::reset(void) {
	uint8_t val = 6;
	uint8_t err = 0;
	_powerUp();
	err = HAL_I2C_Master_Transmit(_i2c_handle, 0, &val, 1, 100);
	_powerDown();
	return err;
}

void EPC901::_powerUp() {
	// power up sensor
	HAL_GPIO_WritePin(PWR_DOWN_GPIO_Port, PWR_DOWN_Pin, GPIO_PIN_RESET);
	DWT_Delay_us(12);
}

void EPC901::_powerDown() {
	// power down sensor
	HAL_GPIO_WritePin(PWR_DOWN_GPIO_Port, PWR_DOWN_Pin, GPIO_PIN_SET);
}

void EPC901::_exposeImage(uint32_t exposure_us) {
	// clear data (necessary when using power-down?)
//	HAL_GPIO_WritePin(CLR_DATA_GPIO_Port, CLR_DATA_Pin, GPIO_PIN_SET);
//	DWT_Delay_us(1);
//	HAL_GPIO_WritePin(CLR_DATA_GPIO_Port, CLR_DATA_Pin, GPIO_PIN_RESET);
//	DWT_Delay_us(10);
	// clear pixels (necessary when using power-down?)
//	HAL_GPIO_WritePin(CLR_PIX_GPIO_Port, CLR_PIX_Pin, GPIO_PIN_SET);
//	DWT_Delay_us(1);
//	HAL_GPIO_WritePin(CLR_PIX_GPIO_Port, CLR_PIX_Pin, GPIO_PIN_RESET);
//	DWT_Delay_us(10);
	// open shutter
	HAL_GPIO_WritePin(SHUTTER_GPIO_Port, SHUTTER_Pin, GPIO_PIN_SET);
	// ~1us flush and ~1us shift with osc ~36MHz
	if (exposure_us != 0) {
		DWT_Delay_us(exposure_us);
	} else {
		DWT_Delay_us(1);
	}
	// close shutter
	HAL_GPIO_WritePin(SHUTTER_GPIO_Port, SHUTTER_Pin, GPIO_PIN_RESET);
	// wait for data ready
	while (GPIO_PIN_RESET == HAL_GPIO_ReadPin(DATA_RDY_GPIO_Port, DATA_RDY_Pin)) {
		// TODO: timeout?
		// break; // no sensor yet
	}
}

uint16_t EPC901::_readImage(uint16_t* buffer) {
	// read pulse
	HAL_GPIO_WritePin(READ_GPIO_Port, READ_Pin, GPIO_PIN_SET);
	// wait 3 osc cycles - ~0.1us with osc ~36MHz
	DWT_Delay_us(1);
	HAL_GPIO_WritePin(READ_GPIO_Port, READ_Pin, GPIO_PIN_RESET);
	// wait for conversion (37-3 osc cycles) - ~1us with osc ~36MHz
	DWT_Delay_us(2);	// TODO: can be reduced to 1us with calibrated oscillator

	// lock SPI handle
	SPI_TypeDef* spi = _spi_handle->Instance;
	__HAL_LOCK(_spi_handle);

	// read pixels
	uint16_t pixel = 0;
	uint16_t count = 0;
	uint8_t data_msb, data_lsb;

	while (pixel < 1024) {
		// ADC CS low - sample HOLD
		ADC_CS_GPIO_Port->BRR = (uint32_t)ADC_CS_Pin;
		// start transfer of 2 bytes from ADC
		spi->DR = (uint16_t) 0;
		// READ high
		READ_GPIO_Port->BSRR = (uint32_t)READ_Pin;
		// wait for and read 1 byte from ADC
		while (!(spi->SR & SPI_FLAG_RXNE));
		data_msb = *(__IO uint8_t *)&spi->DR;
		// READ low
		READ_GPIO_Port->BRR = (uint32_t)READ_Pin;
		// wait for and read 1 byte from ADC (ADC switches to SAMPLE on 13th bit)
		while (!(spi->SR & SPI_FLAG_RXNE));
		data_lsb = *(__IO uint8_t *)&spi->DR;
		// ADC CS high
		ADC_CS_GPIO_Port->BSRR = (uint32_t)ADC_CS_Pin;
		if (count < 4) {
			// discard first 4 samples (3 pre-loading cycles, 1 ADC sample/conversion pipeline)
		} else {
			// store pixel
			buffer[pixel] = (data_msb << 7) | (data_lsb >> 1);
			pixel++;
		}
		count++;
	}

	// release SPI peripheral
	__HAL_UNLOCK(_spi_handle);

	return pixel;
}
