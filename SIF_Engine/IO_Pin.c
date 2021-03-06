
#include "sebEngine.h"

//u32 before,middle,after;
// actually, this is just a bit in memory, which could also be a signal or a control register bit etc...
// it might be more generic than we could think of...

GPIO_TypeDef* GPIOs[] = {
  GPIOA, GPIOB, GPIOC, GPIOD, 
  GPIOE, GPIOF, GPIOG, GPIOH, 
  GPIOI//, GPIOJ, GPIOK // 216 pins 
};


IO_Pin_t* NewIO_Pin(IO_Pin_t* Pin, PinNameDef Name ) {

//  GPIO_TypeDef* GPIOx = GPIOs[Name>>4];
//  u8 PinPos = Name & 0xF;
  if(Pin==0) while(1); // error
  Check_Pin(Pin->Name); // out of range check, cost time  
  
  // initialize members
  Pin->Name = Name;
  Pin->GPIOx = GPIOs[Name>>4];
  Pin->BitMask = 1L<<(Name&0xF);
  
  return Pin;
}

u32 IO_PinGetPR(IO_Pin_t* Pin) {
  
  if((EXTI->PR)&(Pin->BitMask))
    return 1;
  
  return 0;
}
void IO_PinClearPR(IO_Pin_t* Pin) {
  
  EXTI->PR = Pin->BitMask; // setting the bit will clear it
}




void IO_PinClockEnable(IO_Pin_t* Pin) {
  
  BookPin(Pin->Name);
  if(IsPinLocked(Pin->Name)) return; // already configured
  // we activate the clock of the corresponding GPIO port
  //ClearAndSetShiftedBits(RCC->AHB1ENR, 0, 1, (Pin->Name)>>4);
  RCC->AHB1ENR |= 1<<(Pin->Name>>4);
}

void IO_PinSetHigh(IO_Pin_t* Pin) { // set the output register high
  Pin->GPIOx->BSRRL = Pin->BitMask; // set
}

void IO_PinSetLow(IO_Pin_t* Pin) { // set a pin low
  Pin->GPIOx->BSRRH = Pin->BitMask; // reset
}  

// higher level
void IO_PinSet(IO_Pin_t* Pin, u32 value) {
  if(value) {
    IO_PinSetHigh(Pin); // set
  }else{
    IO_PinSetLow(Pin);
  };
}
  
s32 IO_PinGet(IO_Pin_t* Pin) { // returns the value
  u8 Bit = 0;
  if((Pin->GPIOx->IDR)&(Pin->BitMask))
    Bit = 1;
  return Bit;
}

// higher level
void IO_PinToggle(IO_Pin_t* Pin) { // if pin name def is negative, can decide to do something?
  IO_PinSet(Pin, IO_PinGet(Pin)^1);
}

// how about the input shmiddt trigger?

void IO_PinSetSpeedMHz(IO_Pin_t* Pin, u32 MHz) {
  // we can also take care of the output speed compensation
  u32 set = 0;

  if(IsPinLocked(Pin->Name)) return; // already configured
  
  if(MHz>=50)   {set = 3;} // 2 bits per speed
  else
    if(MHz>=25)     {set = 1;}
  
  ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->OSPEEDR, 3, set, ((Pin->Name&0xF)*2));
}

void IO_PinEnablePullUp(IO_Pin_t* Pin, FunctionalState Enable) {

  ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->PUPDR, 1, (Enable==DISABLE)?0:1, (Pin->Name&0xF)*2);  
}

void IO_PinEnablePullDown(IO_Pin_t* Pin, FunctionalState Enable) {
  
  ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->PUPDR, 2, (Enable==DISABLE)?0:2, (Pin->Name&0xF)*2);  
} 

void IO_PinEnablePullUpDown(IO_Pin_t* Pin, FunctionalState UpEnable, FunctionalState DownEnable) {
  IO_PinEnablePullUp(Pin, UpEnable);
  IO_PinEnablePullDown(Pin, DownEnable);
}

void IO_PinEnableHighDrive(IO_Pin_t* Pin, FunctionalState Enable) {
  
  if(IsPinLocked(Pin->Name)) return; // already configured
  
  ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->OTYPER, 1, (Enable==DISABLE)?1:0, Pin->Name&0xF);  // 0 push pull, 1 open drain
}


// we should also catch if an alternate is already assigned to this pin, but all 16 values are valid...
void IO_PinConfiguredAs(IO_Pin_t* Pin, u32 AF) {

  if(AF & 0xF0) // special mode
  {
    switch(AF) { // this does not affect the alternate function value
      
    case GPIO_AF16_DIGITAL_INPUT:
//        ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->MODER, 3, 0, (Pin->Name&0xF)*2);  // 00 = input, 01 = output, 10 = alternate, 11 = analog
        ClearAndSetShiftedBits(Pin->GPIOx->MODER, 3, 0, (Pin->Name&0xF)*2);  // 00 = input, 01 = output, 10 = alternate, 11 = analog
      break;
      
    case GPIO_AF17_DIGITAL_OUTPUT:
//        ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->MODER, 3, 1, (Pin->Name&0xF)*2);  // 00 = input, 01 = output, 10 = alternate, 11 = analog
        ClearAndSetShiftedBits(Pin->GPIOx->MODER, 3, 1, (Pin->Name&0xF)*2);  // 00 = input, 01 = output, 10 = alternate, 11 = analog
      break;
      
    case GPIO_AF18_ANALOG:
//        ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->MODER, 3, 3, (Pin->Name&0xF)*2);  // 00 = input, 01 = output, 10 = alternate, 11 = analog
        ClearAndSetShiftedBits(Pin->GPIOx->MODER, 3, 3, (Pin->Name&0xF)*2);  // 00 = input, 01 = output, 10 = alternate, 11 = analog
      break;
      
    case GPIO_AF20_DISABLED:
      break;
      
    default: while(1);
    }
    
    return;
  }

  if(IsPinLocked(Pin->Name)&&(AF!=Pin->AF)) while(1); // no you can't modify the AF as it's been already done by someone!
  
  Pin->AF = AF;
  // normal alternate function 0..15
//  ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->AFR[(Pin->Name>>3)&1], 15, signal, (Pin->Name&0x7)*4);  // 32 bit = 8 pin x 4 bit field
//  ClearAndSetShiftedBits(GPIOs[Pin->Name>>4]->MODER, 3, 2, (Pin->Name&0xF)*2);  // 00 = input, 01 = output, 10 = alternate, 11 = analog
  ClearAndSetShiftedBits(Pin->GPIOx->AFR[(Pin->Name>>3)&1], 15, AF, (Pin->Name&0x7)*4);  // 32 bit = 8 pin x 4 bit field
  ClearAndSetShiftedBits(Pin->GPIOx->MODER, 3, 2, (Pin->Name&0xF)*2);  // 00 = input, 01 = output, 10 = alternate, 11 = analog
}

// Alternate function: We need to find the right alternate function for the corresponding Peripheral...
// This is the most difficult: We enter a pin and the peripheral name, and we get the alternate function, if any.
// for each pin, we give a list of alternates.
// Let's create a list
#include "alternates.h"


//=========================------------------------> Here is a very long list which contains all
#define SYS 254
#define LCD 255
//#define SDIO 256
//#define ETH 257

const PinAlternateDescription_t PAD[] = {
  
{ PA0, (u32) TIM2, TIM2_CH1, 1, "TIM2_CH1" },
{ PA0, (u32) TIM2, TIM2_ETR, 1, "TIM2_ETR" },
{ PA0, (u32) TIM5, TIM5_CH1, 2, "TIM5_CH1" },
{ PA0, (u32) TIM8, TIM8_ETR, 3, "TIM8_ETR" },
{ PA0, (u32) USART2, USART2_CTS, 7, "USART2_CTS" },
{ PA0, (u32) UART4, UART4_TX, 8, "UART4_TX" },
{ PA0, (u32) ETH, ETH_MII_CRS, 11, "ETH_MII_CRS" },
{ PA0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PA0, (u32) ADC1, ADC1_IN0, 0, "ADC1_IN0" }, // *** ADC
{ PA0, (u32) ADC2, ADC2_IN0, 0, "ADC2_IN0" }, // *** ADC
{ PA0, (u32) ADC3, ADC3_IN0, 0, "ADC3_IN0" }, // *** ADC

{ PA1, (u32) TIM2, TIM2_CH2, 1, "TIM2_CH2" },
{ PA1, (u32) TIM5, TIM5_CH2, 2, "TIM5_CH2" },
{ PA1, (u32) USART2, USART2_RTS, 7, "USART2_RTS" },
{ PA1, (u32) UART4, UART4_RX, 8, "UART4_RX" },
{ PA1, (u32) ETH, ETH_MII_RX_CLK, 11, "ETH_MII_RX_CLK" },
{ PA1, (u32) ETH, ETH_RMII_REF_CLK, 11, "ETH_RMII_REF_CLK"},
{ PA1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PA1, (u32) ADC1, ADC1_IN1, 0, "ADC1_IN1" }, // *** ADC
{ PA1, (u32) ADC2, ADC2_IN1, 0, "ADC2_IN1" }, // *** ADC
{ PA1, (u32) ADC3, ADC3_IN1, 0, "ADC3_IN1" }, // *** ADC

{ PA2, (u32) TIM2, TIM2_CH3, 1, "TIM2_CH3" },
{ PA2, (u32) TIM5, TIM5_CH3, 2, "TIM5_CH2" },
{ PA2, (u32) TIM9, TIM9_CH1, 3, "TIM9_CH1" },
{ PA2, (u32) USART2, USART2_TX, 7, "USART2_TX" },
{ PA2, (u32) ETH, ETH_MDIO, 11, "ETH_MDIO" },
{ PA2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PA2, (u32) ADC1, ADC1_IN2, 0, "ADC1_IN2" }, // *** ADC
{ PA2, (u32) ADC2, ADC2_IN2, 0, "ADC2_IN2" }, // *** ADC
{ PA2, (u32) ADC3, ADC3_IN2, 0, "ADC3_IN2" }, // *** ADC

{ PA3, (u32) TIM2, TIM2_CH4, 1, "TIM2_CH4" },
{ PA3, (u32) TIM5, TIM5_CH4, 2, "TIM5_CH4" },
{ PA3, (u32) TIM9, TIM9_CH2, 3, "TIM9_CH2" },
{ PA3, (u32) USART2, USART2_RX, 7, "USART2_RX" },
//{ PA3, (u32) OTG2_HS, OTG_HS_ULPI_D0, 10, "OTG_HS_ULPI_D0" },
{ PA3, (u32) ETH, ETH_MII_COL, 11, "ETH_MII_COL" },
{ PA3, (u32) LCD, LCD_B5, 14, "LCD_B5" },
{ PA3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PA3, (u32) ADC1, ADC1_IN3, 0, "ADC1_IN3" }, // *** ADC
{ PA3, (u32) ADC2, ADC2_IN3, 0, "ADC2_IN3" }, // *** ADC
{ PA3, (u32) ADC3, ADC3_IN3, 0, "ADC3_IN3" }, // *** ADC


{ PA4, (u32) SPI1, SPI1_NSS, 5, "SPI1_NSS" },
{ PA4, (u32) SPI3, SPI3_NSS, 6, "SPI3_NSS" },
{ PA4, (u32) I2S3ext, I2S3_WS, 6, "I2S3_WR" },
{ PA4, (u32) USART2, USART2_CK, 7, "USART2_CK" },
//{ PA4, (u32) OTG2_HS, OTG_HS_SOF, 12 , "OTG_HS_SOF" },
{ PA4, (u32) DCMI, DCMI_HSYNC, 13, "DCMI_HSYNC" },
{ PA4, (u32) LCD, LCD_VSYNC, 14, "LCD_VSYNC" },
{ PA4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PA4, (u32) ADC1, ADC1_IN4, 0, "ADC1_IN4" }, // *** ADC
{ PA4, (u32) ADC2, ADC2_IN4, 0, "ADC2_IN4" }, // *** ADC
{ PA4, (u32) DAC/*1*/, DAC1_ANALOG, 0, "DAC1_ANALOG" }, // TBD


{ PA5, (u32) TIM2, TIM2_CH1, 1, "TIM2_CH1" },
{ PA5, (u32) TIM2, TIM2_ETR, 1, "TIM2_ETR" },
{ PA5, (u32) TIM8, TIM8_CH1N, 3, "TIM8_CH1N" },
{ PA5, (u32) SPI1, SPI1_SCK, 5, "SPI1_SCK" },
// ULPI_CLK
{ PA5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PA5, (u32) ADC1, ADC1_IN5, 0, "ADC1_IN5" }, // *** ADC
{ PA5, (u32) ADC2, ADC2_IN5, 0, "ADC2_IN5" }, // *** ADC
{ PA5, (u32) DAC/*2*/, DAC2_ANALOG, 0, "DAC2_ANALOG" }, // TBD

{ PA6, (u32) TIM1, TIM1_BKIN, 1, "TIM1_BKIN" },
{ PA6, (u32) TIM3, TIM3_CH1, 2, "TIM3_CH1" },
{ PA6, (u32) TIM8, TIM8_BKIN, 3, "TIM8_BKIN" },
{ PA6, (u32) SPI1, SPI1_MISO, 5, "SPI1_MISO" },
{ PA6, (u32) TIM13, TIM13_CH1, 9, "TIM13_CH1" },
{ PA6, (u32) DCMI, DCMI_PIXCLK, 13, "DCMI_PIXCLK" },
{ PA6, (u32) LCD, LCD_G2, 14, "LCD_G2" },
{ PA6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PA6, (u32) ADC1, ADC1_IN6, 0, "ADC1_IN6" }, // *** ADC
{ PA6, (u32) ADC2, ADC2_IN6, 0, "ADC2_IN6" }, // *** ADC

{ PA7, (u32) TIM1, TIM1_CH1N, 1, "TIM1_CH1N" },
{ PA7, (u32) TIM3, TIM3_CH2, 2, "TIM3_CH2" },
{ PA7, (u32) TIM8, TIM8_CH1N, 3, "TIM8_CH1N" },
{ PA7, (u32) SPI1, SPI1_MOSI, 5, "SPI1_MOSI" },
{ PA7, (u32) TIM14, TIM14_CH1, 9, "TIM14_CH1" },
{ PA7, (u32) ETH, ETH_MII_RX_DV, 11, "ETH_MII_RX_DV" },
{ PA7, (u32) ETH, ETH_RMII_CRS_DV, 11, "ETH_RMII_CRS_DV" },
{ PA7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PA7, (u32) ADC1, ADC1_IN7, 0, "ADC1_IN7" }, // *** ADC
{ PA7, (u32) ADC2, ADC2_IN7, 0, "ADC2_IN7" }, // *** ADC

{ PA8, (u32) RCC, MCO1, 0, "MCO1" },
{ PA8, (u32) TIM1, TIM1_CH1, 1, "TIM1_CH1" },
{ PA8, (u32) I2C3, I2C3_SCL, 4, "I2C3_SCL" },
{ PA8, (u32) USART1, USART1_CK, 7, "USART1_CK" },
//{ PA8, (u32) OTG1_FS, OTG_FS_SOF, 10, "OTG_FS_SOF" },
{ PA8, (u32) LCD, LCD_R6, 14, "LCD_R6" },
{ PA8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PA9, (u32) TIM1, TIM1_CH2, 1, "TIM1_CH2" },
{ PA9, (u32) I2C3, I2C3_SMBA, 4, "I2C3_SMBA" },
{ PA9, (u32) USART1, USART1_TX, 7, "USART1_TX" },
{ PA9, (u32) DCMI, DCMI_D0, 13, "DCMI_D0" },
{ PA9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PA10, (u32) TIM1, TIM1_CH3, 1, "TIM1_CH3" },
{ PA10, (u32) USART1, USART1_RX, 7, "USART1_RX" },
//{ PA10, (u32) OTG, OTG_FS_ID, 10, "OTG_FS_ID" },
{ PA10, (u32) DCMI, DCMI_D1, 13, "DCMI_D1" },
{ PA10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PA11, (u32) TIM1, TIM1_CH4, 1, "TIM1_CH4" },
{ PA11, (u32) USART1, USART1_CTS, 7, "USART1_CTS" },
{ PA11, (u32) CAN1, CAN1_RX, 9, "CAN1_RX" },
//{ PA11, (u32) OTG, OTG_FS_DM, 10, "OTG_FS_DM" },
{ PA11, (u32) LCD, LCD_R4, 14, "LCD_R4" },
{ PA11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PA12, (u32) TIM1, TIM1_ETR, 1, "TIM1_ETR" },
{ PA12, (u32) USART1, USART1_RTS, 7, "USART1_RTS" },
{ PA12, (u32) CAN1, CAN1_TX, 9, "CAN1_TX" },
//{ PA12, (u32) OTG, OTG_FS_DP, 10, "OTG_FS_DP" },
{ PA12, (u32) LCD, LCD_R5, 14, "LCD_R5" },
{ PA12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PA13, (u32) SYS, JTMS_SWDIO, 0, "JTMS_SWDIO" },
{ PA13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PA14, (u32) SYS, JTCK_SWCLK, 0, "JTCK_SWDCLK" },
{ PA14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PA15, (u32) SYS, JTDI, 0, "JTDI" },
{ PA15, (u32) TIM2, TIM2_CH1, 1, "TIM2_CH1" },
{ PA15, (u32) TIM2, TIM2_ETR, 1, "TIM2_ETR" },
{ PA15, (u32) SPI1, SPI1_NSS, 5, "SPI1_NSS" },
{ PA15, (u32) SPI3, SPI3_NSS, 6, "SPI3_NSS" },
{ PA15, (u32) I2S3ext, I2S3_WS, 6, "I2S3_WS" },
{ PA15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB0, (u32) TIM1, TIM1_CH2N, 1, "TIM1_CH2N" },
{ PB0, (u32) TIM3, TIM3_CH3, 2, "TIM2_CH3" },
{ PB0, (u32) TIM8, TIM8_CH2N, 3, "TIM8_CH2N" },
{ PB0, (u32) LCD, LCD_R3, 9, "LCD_R3" },
//{ PB0, (u32) , OTG_HS_ULPI_D1, 10, "OTG_HS_ULPI_D1" },
{ PB0, (u32) ETH, ETH_MII_RXD2, 11, "ETH_MII_RXD2" },
{ PB0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PB0, (u32) ADC1, ADC1_IN8, 0, "ADC1_IN8" }, // *** ADC
{ PB0, (u32) ADC2, ADC2_IN8, 0, "ADC2_IN8" }, // *** ADC


{ PB1, (u32) TIM1, TIM1_CH3N, 1, "TIM1_CH3N" },
{ PB1, (u32) TIM3, TIM3_CH4, 2, "TIM3_CH4" },
{ PB1, (u32) TIM8, TIM8_CH3N, 3, "TIM8_CH3N" },
{ PB1, (u32) LCD, LCD_R6, 9, "LCD_R6" },
//{ PB1, (u32) , , OTG_HS_ULPI_D2, 10, "OTG_HS_ULPI_D2" },
{ PB1, (u32) ETH, ETH_MII_RXD3, 11, "ETH_MII_RXD3" },
{ PB1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PB1, (u32) ADC1, ADC1_IN9, 0, "ADC1_IN9" }, // *** ADC
{ PB1, (u32) ADC2, ADC2_IN9, 0, "ADC2_IN9" }, // *** ADC


{ PB2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PB3, (u32) SYS, JTD0, 0, "JTDO" },
//{ PB3, (U32) SYS, TRACSWO, 0, "TRACSWO" },
{ PB3, (u32) TIM2, TIM2_CH2, 1, "TIM2_CH2" },
{ PB3, (u32) SPI1, SPI1_SCK, 5, "SPI1_SCK" },
{ PB3, (u32) I2S3ext, I2S3_CK, 5, "I2S3_CK" },
{ PB3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PB4, (u32) SYS, NJTRST, 0, "NJTRST" },
{ PB4, (u32) TIM3, TIM3_CH1, 2, "TIM3_CH1" },
{ PB4, (u32) SPI1, SPI1_MISO, 5, "SPI1_MISO" },
{ PB4, (u32) SPI3, SPI3_MISO, 6, "SPI3_MISO" },
{ PB4, (u32) I2S3ext, I2S3_EXT_SD, 7, "I2S3_EXT_SD" },
{ PB4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB5, (u32) TIM3, TIM3_CH2, 2, "TIM3_CH2" },
{ PB5, (u32) I2C1, I2C1_SMBA, 4, "I2C1_SMBA" },
{ PB5, (u32) SPI1, SPI1_MOSI, 5, "SPI1_MOSI" },
{ PB5, (u32) SPI3, SPI3_MOSI, 6, "SPI3_MOSI" },
{ PB5, (u32) I2S3ext, I2S3_SD, 6, "I2S3_SD" },
{ PB5, (u32) CAN2, CAN2_RX, 9, "CAN2_RX" },
//{ PB5, (u32) , OTG_HS_ULPI_D7, 10, "OTG_HS_ULPI_D7" },
{ PB5, (u32) ETH, ETH_PPS_OUT, 11, "ETH_PPS_OUT" },
//{ PB5, (u32) FMC, FMC_SDCKE1, 12, "FMC_SDCKE1" },
{ PB5, (u32) DCMI, DCMI_D10, 13, "DCMI_D10" },
{ PB5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB6, (u32) TIM4, TIM4_CH1, 2, "TIM4_CH1" },
{ PB6, (u32) I2C1, I2C1_SCL, 4, "I2C1_SCL" },
{ PB6, (u32) USART1, USART1_TX, 7, "USART1_TX" },
{ PB6, (u32) CAN2, CAN2_TX, 9, "CAN2_TX" },
//{ PB6, (u32) FMC, FMC_SDNE1, 12, "FMC_SDNE1" },
{ PB6, (u32) DCMI, DCMI_D5, 13, "DCMI_D5" },
{ PB6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB7, (u32) TIM4, TIM4_CH2, 2, "TIM4_CH2" },
{ PB7, (u32) I2C1, I2C1_SDA, 4, "I2C1_SDA" },
{ PB7, (u32) USART1, USART1_RX, 7, "USART1_RX" },
//{ PB7, (u32) FMC, FMC_NL, 12, "FMC_NL" },
{ PB7, (u32) DCMI, DCMI_VSYNC, 13, "DCMI_VSYNC" },
{ PB7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB8, (u32) TIM4, TIM4_CH3, 2, "TIM4_CH3" },
{ PB8, (u32) TIM10, TIM10_CH1, 3, "TIM10_CH1" },
{ PB8, (u32) I2C1, I2C1_SCL, 4, "I2C1_SCL" },
{ PB8, (u32) CAN1, CAN1_RX, 9, "CAN1_RX" },
{ PB8, (u32) ETH, ETH_MII_TXD3, 11, "ETH_MII_TXD3" },
{ PB8, (u32) SDIO, SDIO_D4, 12, "SDIO_D4" },
{ PB8, (u32) DCMI, DCMI_D6, 13, "DCMI_D6" },
{ PB8, (u32) LCD, LCD_B6, 14, "LCD_B6" },
{ PB8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB9, (u32) TIM4, TIM4_CH4, 2, "TIM4_CH4" },
{ PB9, (u32) TIM11, TIM11_CH1, 3, "TIM11_CH1" },
{ PB9, (u32) I2C1, I2C1_SDA, 4, "I2C1_SDA" },
{ PB9, (u32) SPI2, SPI2_NSS, 5, "SPI2_NSS" },
{ PB9, (u32) I2S2ext, I2S2_WS, 5, "I2S2_WS" },
{ PB9, (u32) CAN1, CAN1_TX, 9, "CAN1_TX" },
{ PB9, (u32) SDIO, SDIO_D5, 12, "SDIO_D5" },
//{ PB9, (u32) , , , "" },
{ PB9, (u32) DCMI, DCMI_D7, 13, "DCMI_D7" },
{ PB9, (u32) LCD, LCD_B7, 14, "LCD_B7" },
{ PB9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB10, (u32) TIM2, TIM2_CH3, 1, "TIM2_CH3" },
{ PB10, (u32) I2C2, I2C2_SCL, 4, "I2C2_SCL" },
{ PB10, (u32) SPI2, SPI2_SCK, 5, "SPI2_SCK" },
{ PB10, (u32) USART3, USART3_TX, 7, "USART3_TX" },
//{ PB10, (u32) , OTG_HS_ULPI_D3, 10, "OTG_ULPI_D3" },
{ PB10, (u32) ETH, ETH_MII_RX_ER, 11, "ETH_MII_RX_ER" },
{ PB10, (u32) LCD, LCD_G4, 14, "LCD_G4" },
{ PB10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB11, (u32) TIM2, TIM2_CH4, 1, "TIM2_CH4" },
{ PB11, (u32) I2C2, I2C2_SDA, 4, "I2C2_SDA" },
{ PB11, (u32) USART3, USART3_RX, 7, "USART3_RX" },
//{ PB11, (u32) , OTG_HS_ULPI_D4, 10, "OTG_HS_ULPI_D4" },
{ PB11, (u32) ETH, ETH_MII_TX_EN, 11, "ETH_MII_TX_EN" },
{ PB11, (u32) ETH, ETH_RMII_TX_EN, 11, "ETH_RMII_TX_EN" },
{ PB11, (u32) LCD, LCD_G5, 14, "LCD_G5" },
{ PB11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB12, (u32) TIM1, TIM1_BKIN, 1, "TIM1_BKIN" },
{ PB12, (u32) I2C2, I2C2_SMBA, 4, "I2C2_SMBA" },
{ PB12, (u32) SPI2, SPI2_NSS, 5, "SPI2_NSS" },
{ PB12, (u32) I2S2ext, I2S2_WS, 5, "I2S2_WS" },
{ PB12, (u32) USART3, USART3_CK, 7, "USART3_CK" },
{ PB12, (u32) CAN2, CAN2_RX, 9, "CAN2_RX" },
//{ PB12, (u32) , OTG_HS_ULPI_D6, 10, "OTG_HS_ULPI_D6" },
{ PB12, (u32) ETH, ETH_MII_TXD0, 11, "ETH_MII_TXD0" },
{ PB12, (u32) ETH, ETH_RMII_TXD0, 11, "ETH_RMII_TXD0" },
//{ PB12, (u32) , OTG_HS_ID, 12, "OTG_HS_ID" },
{ PB12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB13, (u32) TIM1, TIM1_CH1N, 1, "TIM1_CH1N" },
{ PB13, (u32) SPI2, SPI2_SCK, 5, "SPI2_SCK" },
{ PB13, (u32) USART3, USART3_CTS, 7, "USART3_CTS" },
{ PB13, (u32) CAN2, CAN2_TX, 9, "CAN2_TX" },
//{ PB13, (u32) , OTG_HS_ULPI_D6, 10, "OTG_HS_ULPI_D6" },
{ PB13, (u32) ETH, ETH_MII_TXD1, 11, "ETH_MII_TXD1" },
{ PB13, (u32) ETH, ETH_RMII_TXD1, 11, "ETH_RMII_TX_D1" },
{ PB13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB14, (u32) TIM1, TIM1_CH2N, 1, "TIM1_CH2N" },
{ PB14, (u32) TIM8, TIM8_CH2N, 3, "TIM8_CH2N" },
{ PB14, (u32) SPI2, SPI2_MISO, 5, "TIM2_MISO" },
{ PB14, (u32) I2S2ext, I2S2_EXT_SD, 6, "I2S2_EXT_SD" },
{ PB14, (u32) USART3, USART3_RTS, 7, "USART3_RTS" },
{ PB14, (u32) TIM12, TIM12_CH1, 9, "TIM12_CH1" },
//{ PB14, (u32) , OTG_HS_DM, 12, "OTG_HS_DM" },
{ PB14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PB15, (u32) RTC, RTC_REFIN, 0, "RTC_REFIN" },
{ PB15, (u32) TIM1, TIM1_CH3N, 1, "TIM1_CH3N" },
{ PB15, (u32) TIM8, TIM8_CH3N, 3, "TIM8_CH3N" },
{ PB15, (u32) SPI2, SPI2_MOSI, 5, "SPI2_MOSI" },
{ PB15, (u32) I2S2ext, I2S2_SD, 5, "I2S2_SD" },
{ PB15, (u32) TIM12, TIM12_CH2, 9, "TIM12_CH2" },
//{ PB15, (u32) , OTG_HS_DP, 12, "OTG_HS_DP" },
{ PB15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },


//{ PC0, (u32) , OTG_HS_ULPI_STP, 10, "OTG_HS_ULPI_STP" },
//{ PC0, (u32) FMC, FMC_SDN_WE, 12, "FMC_SDN_WE" },
{ PC0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PC0, (u32) ADC1, ADC1_IN10, 0, "ADC1_IN10" }, // *** ADC
{ PC0, (u32) ADC2, ADC2_IN10, 0, "ADC2_IN10" }, // *** ADC
{ PC0, (u32) ADC3, ADC3_IN10, 0, "ADC3_IN10" }, // *** ADC

{ PC1, (u32) ETH, ETH_MDC, 11, "ETH_MDC" },
{ PC1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PC1, (u32) ADC1, ADC1_IN11, 0, "ADC1_IN11" }, // *** ADC
{ PC1, (u32) ADC2, ADC2_IN11, 0, "ADC2_IN11" }, // *** ADC
{ PC1, (u32) ADC3, ADC3_IN11, 0, "ADC3_IN11" }, // *** ADC

{ PC2, (u32) SPI2, SPI2_MISO, 5, "SPI2_MISO" },
{ PC2, (u32) I2S2ext, I2S2_EXT_SD, 6, "I2S2_EXT_SD" },
//{ PC2, (u32) , OTG_HS_ULPI_DIR, 10, "OTG_HS_ULPI_DIR" },
{ PC2, (u32) ETH, ETH_MII_TXD2, 11, "ETH_MII_TXD2" },
//{ PC2, (u32) FMC, FMC_SDNE0, 12, "FMC_SDNE0" },
{ PC2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PC2, (u32) ADC1, ADC1_IN12, 0, "ADC1_IN12" }, // *** ADC
{ PC2, (u32) ADC2, ADC2_IN12, 0, "ADC2_IN12" }, // *** ADC
{ PC2, (u32) ADC3, ADC3_IN12, 0, "ADC3_IN12" }, // *** ADC

{ PC3, (u32) SPI2, SPI2_MOSI, 5, "SPI2_MOSI" },
{ PC3, (u32) I2S2ext, I2S2_SD, 5, "I2S2_SD" },
//{ PC3, (u32) , OTG_HS_ULPI_NXT, 10, "OTG_ULPI_NXT" },
{ PC3, (u32) ETH, ETH_MII_TX_CLK, 11, "ETH_MII_TX_CLK" },
//{ PC3, (u32) FMC, FMC_SDCKE0, 12, "FMC_SDCKE0" },
{ PC3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PC3, (u32) ADC1, ADC1_IN13, 0, "ADC1_IN13" }, // *** ADC
{ PC3, (u32) ADC2, ADC2_IN13, 0, "ADC2_IN13" }, // *** ADC
{ PC3, (u32) ADC3, ADC3_IN13, 0, "ADC3_IN13" }, // *** ADC

{ PC4, (u32) ETH, ETH_MII_RXD0, 11, "ETH_MII_RXD0" },
{ PC4, (u32) ETH, ETH_RMII_RXD0, 11, "ETH_RMII_RXD0" },
{ PC4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PC4, (u32) ADC1, ADC1_IN14, 0, "ADC1_IN14" }, // *** ADC
{ PC4, (u32) ADC2, ADC2_IN14, 0, "ADC2_IN14" }, // *** ADC

{ PC5, (u32) ETH, ETH_MII_RXD1, 11, "ETH_MII_RXD1" },
{ PC5, (u32) ETH, ETH_RMII_RXD1, 11, "ETH_RMII_RXD1" },
{ PC5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PC5, (u32) ADC1, ADC1_IN5, 0, "ADC1_IN5" }, // *** ADC
{ PC5, (u32) ADC2, ADC2_IN5, 0, "ADC2_IN5" }, // *** ADC

{ PC6, (u32) TIM3, TIM3_CH1, 2, "TIM3_CH1" },
{ PC6, (u32) TIM8, TIM8_CH1, 3, "TIM8_CH1" },
{ PC6, (u32) I2S2ext, I2S2_MCK, 5, "I2S2_MCK" },
{ PC6, (u32) USART6, USART6_TX, 8, "USART6_TX" },
{ PC6, (u32) SDIO, SDIO_D6, 13, "SDIO_D6" },
{ PC6, (u32) DCMI, DCMI_D0, 13, "DCMI_D0" },
{ PC6, (u32) LCD, LCD_HSYNC, 14, "LCD_HSYNC" },
{ PC6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC7, (u32) TIM3, TIM3_CH2, 2, "TIM3_CH2" },
{ PC7, (u32) TIM8, TIM8_CH2, 3, "TIM8_CH2" },
{ PC7, (u32) USART6, USART6_RX, 8, "USART6_RX" },
{ PC7, (u32) SDIO, SDIO_D7, 12, "SDIO_D7" },
{ PC7, (u32) DCMI, DCMI_D1, 13, "DCMI_D1" },
{ PC7, (u32) LCD, LCD_G6, 14, "LCD_G6" },
{ PC7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC8, (u32) TIM3, TIM3_CH3, 2, "TIM3_CH3" },
{ PC8, (u32) TIM8, TIM8_CH3, 3, "TIM8_CH3" },
{ PC8, (u32) USART6, USART6_CK, 8, "USART6_CK" },
{ PC8, (u32) SDIO, SDIO_D0, 12, "SDIO_D0" },
{ PC8, (u32) DCMI, DCMI_D2, 13, "DCMI_D2" },
{ PC8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC9, (u32) RCC, MCO2, 0, "MCO2" },
{ PC9, (u32) TIM3, TIM3_CH4, 2, "TIM3_CH4" },
{ PC9, (u32) TIM8, TIM8_CH4, 3, "TIM8_CH4" },
{ PC9, (u32) I2C3, I2C3_SDA, 4, "I2C3_SDA" },
//{ PC9, (u32) I2Sext, I2S_CKIN, 5, "I2S_CKIN" },
{ PC9, (u32) SDIO, SDIO_D1, 12, "SDIO_D1" },
{ PC9, (u32) DCMI, DCMI_D3, 13, "DCMI_D3" },
{ PC9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC10, (u32) SPI3, SPI3_SCK, 6, "SPI3_SCK" },
{ PC10, (u32) I2S3ext, I2S3_CK, 6, "I2S3_CK" },
{ PC10, (u32) USART3, USART3_TX, 7, "USART3_TX" },
{ PC10, (u32) UART4, UART4_TX, 8, "UART4_TX" },
{ PC10, (u32) SDIO, SDIO_D2, 12, "SDIO_D2" },
{ PC10, (u32) DCMI, DCMI_D8, 13, "DCMI_D8" },
{ PC10, (u32) LCD, LCD_R2, 14, "LCD_R2" },
{ PC10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC11, (u32) I2S3ext, I2S3_EXT_SD, 5, "I2S3_EXT_SD" },
{ PC11, (u32) SPI3, SPI3_MISO, 6, "SPI_MISO" },
{ PC11, (u32) USART3, USART3_RX, 7, "USART3_RX" },
{ PC11, (u32) UART4, UART4_RX, 8, "UART4_RX" },
{ PC11, (u32) SDIO, SDIO_D3, 12, "SDIO_D3" },
{ PC11, (u32) DCMI, DCMI_D4, 13, "DCMI_D4" },
{ PC11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC12, (u32) SPI3, SPI3_MOSI, 6, "SPI3_MOSI" },
{ PC12, (u32) I2S3ext, I2S3_SD, 6, "I2S3_SD" },
{ PC12, (u32) USART3, USART3_CK, 7, "USART3_CK" },
{ PC12, (u32) UART5, UART5_TX, 8, "UART5_TX" },
{ PC12, (u32) SDIO, SDIO_CK, 12, "SDIO_CK" },
{ PC12, (u32) DCMI, DCMI_D9, 13, "DCMI_D9" },
{ PC12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PC15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },


{ PD0, (u32) CAN1, CAN1_RX, 9, "CAN1_RX" },
//{ PD0, (u32) FMC, FMC_D2, 12, "FMC_D2" },
{ PD0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD1, (u32) CAN1, CAN1_TX, 9, "CAN1_TX" },
//{ PD1, (u32) FMC, FMC_D3, 12, "FMC_D3" },
{ PD1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD2, (u32) TIM3, TIM3_ETR, 2, "TIM3_ETR" },
{ PD2, (u32) UART5, UART5_RX, 8, "UART5_RX" },
{ PD2, (u32) SDIO, SDIO_CMD, 12, "SDIO_CMD" },
{ PD2, (u32) DCMI, DCMI_D11, 13, "DCMI_D11" },
{ PD2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD3, (u32) SPI2, SPI2_SCK, 5, "SPI2_SCK" },
{ PD3, (u32) I2S2ext, I2S2_CK, 5, "I2S2_CK" },
{ PD3, (u32) USART2, USART2_CTS, 7, "USART2_CTS" },
//{ PD3, (u32) FMC, FMC_CLK, 12, "FMC_CLK" },
{ PD3, (u32) DCMI, DCMI_D5, 13, "DCMI_D5" },
{ PD3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD4, (u32) USART2, USART2_RTS, 7, "USART2_RTS" },
//{ PD4, (u32) FMC, FMC_NOE, 12, "FMC_NOE" },
{ PD4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD5, (u32) USART2, USART2_TX, 7, "USART2_TX" },
//{ PD5, (u32) FMC, FMC_NWE, 12, "FMC_NWE" },
{ PD5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD6, (u32) SPI3, SPI3_MOSI, 5, "SPI3_MOSI" },
{ PD6, (u32) I2S3ext, I2S3_SD, 5, "I2S3_SD" },
//{ PD6, (u32) SAI1, SAI1_SD_A, 6, "SAI1_SD_A" },
{ PD6, (u32) USART2, USART2_RX, 7, "USART2_RX" },
//{ PD6, (u32) FMC, FMC_NWAIT, 12, "FMC_NWAIT" },
{ PD6, (u32) DCMI, DCMI_D10, 13, "DCMI_D10" },
{ PD6, (u32) LCD, LCD_B2, 14, "LCD_B2" },
{ PD6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD7, (u32) USART2, USART2_CK, 7, "USART2_CK" },
//{ PD7, (u32) FMC, FMC_NE1, 12, "FMC_NE1" },
//{ PD7, (u32) FMC, FMC_NCE2, 12, "FMC_NCE2" },
{ PD7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD8, (u32) USART3, USART3_TX, 7, "USART3_TX" },
//{ PD8, (u32) FMC, FMC_D13, 12, "FMC_D13" },
{ PD8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD9, (u32) USART3, USART3_RX, 7, "USART3_RX" },
//{ PD9, (u32) FMC, FMC_D14, 12, "FMC_D14" },
{ PD9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD10, (u32) USART3, USART3_CK, 7, "USART3_CK" },
//{ PD10, (u32) FMC, FMC_D14, 12, "FMC_D14" },
{ PD10, (u32) LCD, LCD_B3, 14, "LCD_B3" },
{ PD10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD11, (u32) USART3, USART3_CTS, 7, "USART3_CTS" },
//{ PD11, (u32) FMC, FMC_A16, 12, "FMC_A16" },
{ PD11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD12, (u32) TIM4, TIM4_CH1, 2, "TIM4_CH1" },
{ PD12, (u32) USART3, USART3_RTS, 7, "USART3_RTS" },
//{ PD12, (u32) FMC, FMC_A17, 12, "FMC_A17" },
{ PD12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD13, (u32) TIM4, TIM4_CH2, 2, "TIM4_CH2" },
//{ PD13, (u32) FMC, FMC_A18, 12, "FMC_A18" },
{ PD13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD14, (u32) TIM4, TIM4_CH3, 3, "TIM4_CH3" },
//{ PD14, (u32) FMC, FMC_D0, 12, "FMC_D0" },
{ PD14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PD15, (u32) TIM4, TIM4_CH4, 2, "TIM4_CH4" },
//{ PD15, (u32) FMC, FMC_D1, 12, "FMC_D1" },
{ PD15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE0, (u32) TIM4, TIM4_ETR, 2, "TIM4_ETR" },
//??{ PE0, (u32) UART8, UART8_RX, 8, "UART8_RX" },
//{ PE0, (u32) FMC, FMC_NBL0, 12, "FMC_NBL0" },
{ PE0, (u32) DCMI, DCMI_D2, 13, "DCMI_D2" },
{ PE0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//?{ PE1, (u32) UART8, UART8_TX, 8, "UART8_TX" },
//{ PE1, (u32) FMC, FMC_NBL1, 12, "FMC_NBL1" },
{ PE1, (u32) DCMI, DCMI_D3, 13, "DCMI_D3" },
{ PE1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PE2, (u32) SYS, TRACECLK, 0, "TRACECLK" },
{ PE2, (u32) SPI4, SPI4_SCK, 5, "SPI4_SCK" },
//{ PE2, (u32) SAI1, SAI1_MCLK_A, 6, "SAI1_MCLK_A" },
{ PE2, (u32) ETH, ETH_MII_TXD3, 11, "ETH_MII_TXD3" },
//{ PE2, (u32) FMC, FMC_A23, 12, "FMC_A23" },
{ PE2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PE3, (u32) SYS, TRACED0, 0, "TRACED0" },
//{ PE3, (u32) SAI1, SAI1_SD_B, 6, "SAI1_SD_B" },
//{ PE3, (u32) FMC, FMC_A19, 12, "FMC_A19" },
{ PE3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE4, (u32) SPI4, SPI4_NSS, 5, "SPI4_NSS" },
//{ PE4, (u32) SAI1, SAI1_FS_A, 6, "SAI1_FS_A" },
//{ PE4, (u32) FMC, FMC_A20, 12, "FMC_A20" },
{ PE4, (u32) DCMI, DCMI_D4, 13, "DCMI_D4" },
{ PE4, (u32) LCD, LCD_B0, 14, "LCD_B0" },
{ PE4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PE5, (u32) SYS, TRACED2, 0, "TRACED2" },
{ PE5, (u32) TIM9, TIM9_CH1, 3, "TIM9_CH1" },
{ PE5, (u32) SPI4, SPI4_MISO, 5, "SPI4_MISO" },
//{ PE5, (u32) SAI1, SAI1_SCK_A, 6, "SAI1_SCK_A" },
//{ PE5, (u32) FMC, FMC_A21, 12, "FMC_A21" },
{ PE5, (u32) DCMI, DCMI_D6, 13, "DCMI_D6" },
{ PE5, (u32) LCD, LCD_G0, 14, "LCD_G0" },
{ PE5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PE6, (u32) SYS, TRACED3, 0, "TRACED3" },
{ PE6, (u32) TIM9, TIM9_CH2, 3, "TIM9_CH2" },
{ PE6, (u32) SPI4, SPI4_MOSI, 5, "SPI4_MOSI" },
//{ PE6, (u32) SAI1, SAI1_SD_A, 6, "SAI1_SD_A" },
//{ PE6, (u32) FMC, FMC_A22, 12, "FMC_A22" },
{ PE6, (u32) DCMI, DCMI_D7, 13, "DCMI_D7" },
{ PE6, (u32) LCD, LCD_G1, 14, "LCD_G1" },
{ PE6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE7, (u32) TIM1, TIM1_ETR, 1, "TIM1_ETR" },
//{ PE7, (u32) UART7, UART7_RX, 8, "UART7_RX" },
//{ PE7, (u32) FMC, FMC_D4, 12, "FMC_D4" },
{ PE7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE8, (u32) TIM1, TIM1_CH1N, 1, "TIM1_CH1N" },
//{ PE8, (u32) UART7, UART7_TX, 8, "UART7_TX" },
//{ PE8, (u32) FMC, FMC_D5, 12, "FMC_D5" },
{ PE8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE9, (u32) TIM1, TIM1_CH1, 1, "TIM1_CH1" },
//{ PE9, (u32) FMC, FMC_D6, 12, "FMC_D6" },
{ PE9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE10, (u32) TIM1, TIM1_CH2N, 1, "TIM1_CH2N" },
//{ PE10, (u32) FMC, FMC_D7, 12, "FMC_D7" },
{ PE10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE11, (u32) TIM1, TIM1_CH2, 1, "TIM1_CH2" },
{ PE11, (u32) SPI4, SPI4_NSS, 5, "SPI4_NSS" },
//{ PE11, (u32) FMC, FMC_D8, 12, "FMC_D8" },
{ PE11, (u32) LCD, LCD_G3, 14, "LCD_G3" },
{ PE11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE12, (u32) TIM1, TIM1_CH3N, 1, "TIM1_CH3N" },
{ PE12, (u32) SPI4, SPI4_SCK, 5, "SPI4_SCK" },
//{ PE12, (u32) FMC, FMC_D9, 12, "FMC_D9" },
{ PE12, (u32) LCD, LCD_B4, 14, "LCD_B4" },
{ PE12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE13, (u32) TIM1, TIM1_CH3, 1, "TIM1_CH3" },
{ PE13, (u32) SPI4, SPI4_MISO, 5, "SPI4_MISO" },
//{ PE13, (u32) FMC, FMC_D10, 12, "FMC_D10" },
{ PE13, (u32) LCD, LCD_DE, 14, "LCD_DE" },
{ PE13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE14, (u32) TIM1, TIM1_CH4, 1, "TIM1_CH4" },
{ PE14, (u32) SPI4, SPI4_MOSI, 5, "SPI4_MOSI" },
//{ PE14, (u32) FMC, FMC_D11, 12, "FMC_D11" },
{ PE14, (u32) LCD, LCD_CLK, 14, "LCD_CLK" },
{ PE14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PE15, (u32) TIM1, TIM1_BKIN, 1, "TIM1_BKIN" },
//{ PE15, (u32) FMC, FMC_D12, 12, "FMC_D12" },
{ PE15, (u32) LCD, LCD_R7, 14, "LCD_R7" },
{ PE15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PF0, (u32) I2C2, I2C2_SDA, 4, "I2S2_SDA" },
//{ PF0, (u32) FMC, FMC_A0, 12, "FMC_A0" },
{ PF0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PF1, (u32) I2C2, I2C2_SCL, 4, "I2C2_SCL" },
//{ PF1, (u32) FMC, FMC_A1, 12, "FMC_A1" },
{ PF1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PF2, (u32) I2C2, I2C2_SMBA, 4, "I2C2_SMBA" },
//{ PF2, (u32) FMC, FMC_A2, 12, "FMC_A2" },
{ PF2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PF3, (u32) FMC, FMC_A3, 12, "FMC_A3" },
{ PF3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PF3, (u32) ADC3, ADC3_IN9, 0, "ADC3_IN9" }, // *** ADC

//{ PF4, (u32) FMC, FMC_A4, 12, "FMC_A4" },
{ PF4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PF4, (u32) ADC3, ADC3_IN14, 0, "ADC3_IN14" }, // *** ADC

//{ PF5, (u32) FMC, FMC_A5, 12, "FMC_A5" },
{ PF5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PF5, (u32) ADC3, ADC3_IN15, 0, "ADC3_IN15" }, // *** ADC

{ PF6, (u32) TIM10, TIM10_CH1, 3, "TIM10_CH1" },
{ PF6, (u32) SPI5, SPI5_NSS, 5, "SPI5_NSS" },
//{ PF6, (u32) SAI1, SAI1_SD_B, 6, "SAI1_SD_B" },
//?{ PF6, (u32) UART7, UART7_TX, 8, "UART7_TX" },
//{ PF6, (u32) FMC, FMC_NIORD, 12, "FMC_NIORD" },
{ PF6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PF6, (u32) ADC3, ADC3_IN4, 0, "ADC3_IN4" }, // *** ADC

{ PF7, (u32) TIM11, TIM11_CH1, 3, "TIM11_CH1" },
{ PF7, (u32) SPI5, SPI5_SCK, 5, "SPI5_SCK" },
//{ PF7, (u32) SAI1, SAI1_MCLK_B, 6, "SAI1_MCLK_B" },
//{ PF7, (u32) UART7, UART7_RX, 8, "UART7_RX" },
//{ PF7, (u32) FMC, FMC_NREG, 12, "FMC_NREG" },
{ PF7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PF7, (u32) ADC3, ADC3_IN5, 0, "ADC3_IN5" }, // *** ADC

{ PF8, (u32) SPI5, SPI5_MISO, 5, "SPI5_MISO" },
//{ PF8, (u32) SAI1, SAI1_SCK_B, 6, "SAI1_SCK_B" },
{ PF8, (u32) TIM13, TIM13_CH1, 9, "TIM13_CH1" },
//{ PF8, (u32) FMC, FMC_NIOWR, 12, "FMC_NIOWR" },
{ PF8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PF8, (u32) ADC3, ADC3_IN6, 0, "ADC3_IN6" }, // *** ADC

{ PF9, (u32) SPI5, SPI5_MOSI, 5, "SPI5_MOSI" },
//{ PF9, (u32) SAI1, SAI1_FS_B, 6, "SAI1_FS_B" },
{ PF9, (u32) TIM14, TIM14_CH1, 9, "TIM14_CH1" },
//{ PF9, (u32) FMC, FMC_CD, 12, "FMC_CD" },
{ PF9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PF9, (u32) ADC3, ADC3_IN7, 0, "ADC3_IN7" }, // *** ADC

//{ PF10, (u32) FMC, FMC_INTR, 12, "FMC_INTR" },
{ PF10, (u32) DCMI, DCMI_D11, 13, "DCMI_D11" },
{ PF10, (u32) LCD, LCD_DE, 14, "LCD_DE" },
{ PF10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },
{ PF10, (u32) ADC3, ADC3_IN8, 0, "ADC3_IN8" }, // *** ADC

{ PF11, (u32) SPI5, SPI5_MOSI, 5, "SPI5_MOSI" },
//{ PF11, (u32) FMC, FMC_SDNRAS, 12, "FMC_SDNRAS" },
{ PF11, (u32) DCMI, DCMI_D12, 13, "DCMI_D12" },
{ PF11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PF12, (u32) FMC, FMC_A6, 12, "FMC_A6" },
{ PF12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PF13, (u32) FMC, FMC_A7, 12, "FMC_A7" },
{ PF13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PF14, (u32) FMC, FMC_A8, 12, "FMC_A8" },
{ PF14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PF15, (u32) FMC, FMC_A9, 12, "FMC_A9" },
{ PF15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },


//{ PG0, (u32) FMC, FMC_A10, 12, "FMC_A10" },
{ PG0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PG1, (u32) FMC, FMC_A11, 12, "FMC_A11" },
{ PG1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PG2, (u32) FMC, FMC_A12, 12, "FMC_A12" },
{ PG2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PG3, (u32) FMC, FMC_A13, 12, "FMC_A13" },
{ PG3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PG4, (u32) FMC, FMC_A14, 12, "FMC_A14" },
//{ PG4, (u32) FMC, FMC_BA0, 12, "FMC_BA0" },
{ PG4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PG5, (u32) FMC, FMC_A15, 12, "FMC_A15" },
//{ PG5, (u32) FMC, FMC_BA1, 12, "FMC_BA1" },
{ PG5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PG6, (u32) FMC, FMC_INT2, 12, "FMC_INT2" },
{ PG6, (u32) DCMI, DCMI_D12, 13, "DCMI_D12" },
{ PG6, (u32) LCD, LCD_R7, 14, "LCD_R7" },
{ PG6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG7, (u32) USART6, USART6_CK, 8, "USART6_CK" },
//{ PG7, (u32) FMC, FMC_INT3, 12, "FMC_INT3" },
{ PG7, (u32) DCMI, DCMI_D13, 13, "DCMI_D13" },
{ PG7, (u32) LCD, LCD_CLK, 14, "LCD_CLK" },
{ PG7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG8, (u32) SPI6, SPI6_NSS, 5, "SPI6_NSS" },
{ PG8, (u32) USART6, USART6_RTS, 8, "USART6_RTS" },
{ PG8, (u32) ETH, ETH_PPS_OUT, 11, "ETH_PPS_OUT" },
//{ PG8, (u32) FMC, FMC_SDC_LK, 12, "FMC_SDC_LK" },
{ PG8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG9, (u32) USART6, USART6_RX, 8, "USART6_RX" },
//{ PG9, (u32) FMC, FMC_NE2, 12, "FMC_NE2" },
//{ PG9, (u32) FMC, FMC_NCE3, 12, "FMC_NCE3" },
{ PG9, (u32) DCMI, DCMI_VSYNC, 13, "DCMI_VSYNC" },
{ PG9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG10, (u32) LCD, LCD_G3, 9, "LCD_G3" },
//{ PG10, (u32) FMC, FMC_NCE4_1, 12, "FMC_NCE4_1" },
//{ PG10, (u32) FMC, FMC_NE3, 12, "FMC_NE3" },
{ PG10, (u32) DCMI, DCMI_D2, 13, "DCMI_D2" },
{ PG10, (u32) LCD, LCD_B2, 14, "LCD_B2" },
{ PG10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG11, (u32) ETH, ETH_MII_TX_EN, 11, "ETH_MII_TX_EN" },
{ PG11, (u32) ETH, ETH_RMII_TX_EN, 11, "ETH_RMII_TX_EN" },
//{ PG11, (u32) FMC, FMC_NCE4_2, 12, "FMC_NCE4_2" },
{ PG11, (u32) DCMI, DCMI_D3, 13, "DCMI_D3" },
{ PG11, (u32) LCD, LCD_B3, 14, "LCD_B3" },
{ PG11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG12, (u32) SPI6, SPI6_MISO, 5, "SPI6_MISO" },
{ PG12, (u32) USART6, USART6_RTS, 8, "USART6_RTS" },
{ PG12, (u32) LCD, LCD_B4, 9, "LCD_B4" },
//{ PG12, (u32) FMC, FMC_NE4, 12, "FMC_NE4" },
{ PG12, (u32) LCD, LCD_B1, 14, "LCD_B1" },
{ PG12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG13, (u32) SPI6, SPI6_SCK, 5, "SPI6_SCK" },
{ PG13, (u32) USART6, USART6_CTS, 8, "USART6_CTS" },
{ PG13, (u32) ETH, ETH_MII_TXD0, 11, "ETH_MII_TCD0" },
{ PG13, (u32) ETH, ETH_RMII_TXD0, 11, "ETH_RMII_TXD0" },
//{ PG13, (u32) FMC, FMC_A24, 12, "FMC_A24" },
{ PG13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG14, (u32) SPI6, SPI6_MOSI, 5, "SPI6_MOSI" },
{ PG14, (u32) USART6, USART6_TX, 8, "USART6_TX" },
{ PG14, (u32) ETH, ETH_MII_TXD1, 11, "ETH_MII_TXD1" },
{ PG14, (u32) ETH, ETH_RMII_TXD1, 11, "ETH_RMII_TXD1" },
//{ PG14, (u32) FMC, FMC_A25, 12, "FMC_A25" },
{ PG14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PG15, (u32) USART6, USART6_CTS, 8, "USART6_CTS" },
//{ PG15, (u32) FMC, FMC_SDNCAS, 12, "FMC_SDNCAS" },
{ PG15, (u32) DCMI, DCMI_D13, 13, "DCMI_D13" },
{ PG15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH2, (u32) ETH, ETH_MII_CRS, 11, "ETH_MII_CRS" },
//{ PH2, (u32) FMC, FMC_SDCKE0, 12, "FMC_SDCKE0" },
{ PH2, (u32) LCD, LCD_R0, 14, "LCD_R0" },
{ PH2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH3, (u32) ETH, ETH_MII_COL, 11, "ETH_MII_COL" },
//{ PH3, (u32) FMC, FMC_SDN_E0, 12, "FMC_SDN_E0" },
{ PH3, (u32) LCD, LCD_R1, 14, "LCD_R1" },
{ PH3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH4, (u32) I2C2, I2C2_SCL, 4, "I2C2_SCL" },
//{ PH4, (u32) , OTG_HS_ULPI_NXT, 10, "OTG_HS_ULPI_NXT" },
{ PH4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH5, (u32) I2C2, I2C2_SDA, 4, "I2C2_SDA" },
{ PH5, (u32) SPI5, SPI5_NSS, 5, "SPI5_NSS" },
//{ PH5, (u32) FMC, FMC_SDNWE, 12, "FMC_SDNWE" },
{ PH5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH6, (u32) I2C2, I2C2_SMBA, 4, "I2C2_SMBA" },
{ PH6, (u32) SPI5, SPI5_SCK, 5, "SPI5_SCK" },
{ PH6, (u32) TIM12, TIM12_CH1, 9, "TIM12_CH1" },
//{ PH6, (u32) FMC, FMC_SDNE1, 12, "FMC_SDNE1" },
{ PH6, (u32) DCMI, DCMI_D8, 13, "DCMI_D8" },
{ PH6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH7, (u32) I2C3, I2C3_SCL, 4, "I2C3_SCL" },
{ PH7, (u32) SPI5, SPI5_MISO, 5, "SPI5_MISO" },
{ PH7, (u32) ETH, ETH_MII_RXD3, 11, "ETH_MII_RXD3" },
//{ PH7, (u32) FMC, FMC_SDCKE1, 12, "FMC_SDCKE1" },
{ PH7, (u32) DCMI, DCMI_D9, 13, "DCMI_D9" },
{ PH7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH8, (u32) I2C3, I2C3_SDA, 4, "I2C3_SDA" },
//{ PH8, (u32) FMC, FMC_D16 , 12, "FMC_D16" },
{ PH8, (u32) DCMI, DCMI_HSYNC, 13, "DCMI_HSYNC" },
{ PH8, (u32) LCD, LCD_R2, 14, "LCD_R2" },
{ PH8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH9, (u32) I2S3ext, I2C3_SMBAA, 4, "I2C3_SMBA" },
{ PH9, (u32) TIM12, TIM12_CH2, 9, "TIM12_CH2" },
//{ PH9, (u32) FMC, FMC_D17, 12, "FMC_D17" },
{ PH9, (u32) DCMI, DCMI_D0, 13, "DCMI_D0" },
{ PH9, (u32) LCD, LCD_R3, 14, "LCD_R3" },
{ PH9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH10, (u32) TIM5, TIM5_CH1, 2, "TIM5_CH1" },
//{ PH10, (u32) FMC, FMC_D18, 12, "FMC_D18" },
{ PH10, (u32) DCMI, DCMI_D1, 13, "DCMI_D1" },
{ PH10, (u32) LCD, LCD_R4, 14, "LCD_R4" },
{ PH10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH11, (u32) TIM5, TIM5_CH2, 2, "TIM5_CH2" },
//{ PH11, (u32) FMC, FMC_D19, 12, "FMC_D19" },
{ PH11, (u32) DCMI, DCMI_D2, 13, "DCMI_D2" },
{ PH11, (u32) LCD, LCD_R5, 14, "LCD_R5" },
{ PH11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH12, (u32) TIM5, TIM5_CH3, 2, "TIM5_CH3" },
//{ PH12, (u32) FMC, FMC_D20, 12, "FMC_D20" },
{ PH12, (u32) DCMI, DCMI_D3, 13, "DCMI_D3" },
{ PH12, (u32) LCD, LCD_R6, 14, "LCD_R6" },
{ PH12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH13, (u32) TIM8, TIM8_CH1N, 3, "TIM8_CH1N" },
{ PH13, (u32) CAN1, CAN1_TX, 9, "CAN1_TX" },
//{ PH13, (u32) FMC, FMC_D21, 12, "FMC_D21" },
{ PH13, (u32) LCD, LCD_G2, 14, "LCD_G2" },
{ PH13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH14, (u32) TIM8, TIM8_CH2N, 3, "TIM8_CH2N" },
//{ PH14, (u32) FMC, FMC_D22, 12, "FMC_D22" },
{ PH14, (u32) DCMI, DCMI_D4, 13, "DCMI_D4" },
{ PH14, (u32) LCD, LCD_G3, 14, "LCD_G3" },
{ PH14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PH15, (u32) TIM8, TIM8_CH3N, 3, "TIM8_CH3N" },
//{ PH15, (u32) FMC, FMC_D23, 12, "FMC_D23" },
{ PH15, (u32) DCMI, DCMI_D11, 13, "DCMI_D11" },
{ PH15, (u32) LCD, LCD_G4, 14, "LCD_G4" },
{ PH15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI0, (u32) TIM5, TIM5_CH4, 2, "TIM5_CH4" },
{ PI0, (u32) SPI2, SPI2_NSS, 5, "SPI2_NSS" },
{ PI0, (u32) I2S2ext, I2S2_WS, 5, "I2S2_WS" },
//{ PI0, (u32) FMC, FMC_D24, 12, "FMC_D24" },
{ PI0, (u32) DCMI, DCMI_D13, 13, "DCMI_D13" },
{ PI0, (u32) LCD, LCD_G5, 14, "LCD_G5" },
{ PI0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI1, (u32) SPI2, SPI2_SCK, 5, "SPI2_SCK" },
{ PI1, (u32) I2S2ext, I2S2_CK, 5, "I2S2_CK" },
//{ PI1, (u32) FMC, FMC_D25, 12, "FMC_D25" },
{ PI1, (u32) DCMI, DCMI_D8, 13, "DCMI_D8" },
{ PI1, (u32) LCD, LCD_G6, 14, "LCD_G6" },
{ PI1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI2, (u32) TIM8, TIM8_CH4, 3, "TIM8_CH4" },
{ PI2, (u32) SPI2, SPI2_MISO, 5, "SPI2_MISO" },
{ PI2, (u32) I2S2ext, I2S2_EXT_SD, 6, "I2S2_EXT_SD" },
//{ PI2, (u32) FMC, FMC_D26, 12, "FMC_D26" },
{ PI2, (u32) DCMI, DCMI_D9, 13, "DCMI_D9" },
{ PI2, (u32) LCD, LCD_G7, 14, "LCD_G7" },
{ PI2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI3, (u32) TIM8, TIM8_ETR, 3, "TIM8_ETR" },
{ PI3, (u32) SPI2, SPI2_MOSI, 5, "SPI2_MOSI" },
{ PI3, (u32) I2S2ext, I2S2_SD, 5, "I2S2_SD" },
//{ PI3, (u32) FMC, FMC_D27, 12, "FMC_D27" },
{ PI3, (u32) DCMI, DCMI_D10, 13, "DCMI_D10" },
{ PI3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI4, (u32) TIM8, TIM8_BKIN, 3, "TIM8_BKIN" },
//{ PI4, (u32) FMC, FMC_NBL2, 12, "FMC_NBL2" },
{ PI4, (u32) DCMI, DCMI_D5, 13, "DCMI_D5" },
{ PI4, (u32) LCD, LCD_B4, 14, "LCD_B4" },
{ PI4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI5, (u32) TIM8, TIM8_CH1, 3, "TIM8_CH1" },
//{ PI5, (u32) FMC, FMC_NBL3, 12, "FMC_NBL3" },
{ PI5, (u32) DCMI, DCMI_VSYNC, 13, "DCMI_VSYNC" },
{ PI5, (u32) LCD, LCD_B5, 14, "LCD_B5" },
{ PI5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI6, (u32) TIM8, TIM8_CH2, 3, "TIM8_CH2" },
//{ PI6, (u32) FMC, FMC_D28, 12, "FMC_D28" },
{ PI6, (u32) DCMI, DCMI_D6, 13, "DCMI_D6" },
{ PI6, (u32) LCD, LCD_B6, 14, "LCD_B6" },
{ PI6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI7, (u32) TIM8, TIM8_CH3, 3, "TIM8_CH3" },
//{ PI7, (u32) FMC, FMC_D29, 12, "FMC_D29" },
{ PI7, (u32) DCMI, DCMI_D7, 13, "DCMI_D7" },
{ PI7, (u32) LCD, LCD_B7, 14, "LCD_B7" },
{ PI7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI9, (u32) CAN1, CAN1_RX, 9, "CAN1_RX" },
//{ PI9, (u32) FMC, FMC_D30, 11, "FMC_D30" },
{ PI9, (u32) LCD, LCD_VSYNC, 14, "LCD_VSYNC" },
{ PI9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI10, (u32) ETH, ETH_MII_RX_ER, 11, "ETH_MII_RX_ER" },
//{ PI10, (u32) FMC, FMC_D31, 12, "FMC_D31" },
{ PI10, (u32) LCD, LCD_HSYNC, 14, "LCD_HSYNC" },
{ PI10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

//{ PI11, (u32) , OTG_HS_ULPI_DIR, 10, "OTG_HS_ULPI_DIR" },
{ PI11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI12, (u32) LCD, LCD_HSYNC, 14, "LCD_HSYNC" },
{ PI12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI13, (u32) LCD, LCD_VSYNC, 14, "LCD_VSYNC" },
{ PI13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI14, (u32) LCD, LCD_CLK, 14, "LCD_CLK" },
{ PI14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PI15, (u32) LCD, LCD_R0, 14, "LCD_R0" },
{ PI15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ0, (u32) LCD, LCD_R1, 14, "LCD_R1" },
{ PJ0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ1, (u32) LCD, LCD_R2, 14, "LCD_R2" },
{ PJ1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ2, (u32) LCD, LCD_R3, 14, "LCD_R3" },
{ PJ2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ3, (u32) LCD, LCD_R4, 14, "LCD_R4" },
{ PJ3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ4, (u32) LCD, LCD_R5, 14, "LCD_R5" },
{ PJ4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ5, (u32) LCD, LCD_R6, 14, "LCD_R6" },
{ PJ5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ6, (u32) LCD, LCD_R7, 14, "LCD_R7" },
{ PJ6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ7, (u32) LCD, LCD_G0, 14, "LCD_G0" },
{ PJ7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ8, (u32) LCD, LCD_G1, 14, "LCD_G1" },
{ PJ8, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ9, (u32) LCD, LCD_G2, 14, "LCD_G2" },
{ PJ9, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ10, (u32) LCD, LCD_G3, 14, "LCD_G3" },
{ PJ10, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ11, (u32) LCD, LCD_G4, 14, "LCD_G4" },
{ PJ11, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ12, (u32) LCD, LCD_B0, 14, "LCD_B0" },
{ PJ12, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ13, (u32) LCD, LCD_B1, 14, "LCD_B1" },
{ PJ13, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ14, (u32) LCD, LCD_B2, 14, "LCD_B2" },
{ PJ14, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PJ15, (u32) LCD, LCD_B3, 14, "LCD_B3" },
{ PJ15, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },


{ PK0, (u32) LCD, LCD_G5, 14, "LCD_G5" },
{ PK0, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PK1, (u32) LCD, LCD_G6, 14, "LCD_G6" },
{ PK1, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PK2, (u32) LCD, LCD_G7, 14, "LCD_G7" },
{ PK2, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PK3, (u32) LCD, LCD_B4, 14, "LCD_B4" },
{ PK3, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PK4, (u32) LCD, LCD_B5, 14, "LCD_B5" },
{ PK4, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PK5, (u32) LCD, LCD_B6, 14, "LCD_B6" },
{ PK5, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PK6, (u32) LCD, LCD_B7, 14, "LCD_B7" },
{ PK6, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

{ PK7, (u32) LCD, LCD_DE, 14, "LCD_DE" },
{ PK7, (u32) SYS, EVENTOUT, 15, "EVENTOUT" },

};

//=============================
// const PinAlternateDescription_t PAD[]
// search functions from this const global structure (depends on dice and package)
/*
typedef struct {
  
  u8 PinName;
  u32 PPP_Base;
  SignalName_t SignalName;
  u8 AF;
  char* SignalNameString;
  
} PinAlternateDescription_t;
*/

// give the pin info
u32 GetPinAF(PinNameDef PinName, u32 PPP_Adr) {
  
  u32 i;
  u32 result;
  
  for(i=0;i<countof(PAD);i++) {
    
    if(PAD[i].PinName==PinName)
      if(PAD[i].PPP_Base==PPP_Adr) {
        // we found the pin!
        result = PAD[i].AF;
        return result;
      };
    
  };
  
  while(1); // nothing found...
}


PinAlternateDescription_t* GetSignalDescription(PinNameDef PinName, u32 PPP_Adr) {

  u32 i;
  PinAlternateDescription_t* result;
  
  for(i=0;i<countof(PAD);i++) {
    
    if(PAD[i].PinName==PinName)
      if(PAD[i].PPP_Base==PPP_Adr) {
        // we found the pin!
        result = (PinAlternateDescription_t*) &PAD[i];
        return result;
      };
    
  };
  
  while(1); // nothing found...
}

//=========================== Functions for searching solutions ====================
// The only globals we can use are the constant ones.
// When "x" is found, any possible number will be used to get a match (increasing index number)
// It is recursive as long as an x exist.
// it returns -1 if nothing is found. Recursive 2 levels usually

u32 DoLabelMatch(char* l, char*s) {
  
  for(u8 n=0;n<32;n++) { // max 32 char to compare

      if((l[n]==0)&&(s[n]==0))
        return TRUE; // everything same till end char
      
      if(l[n]!=s[n])
        return FALSE; // mismatch, failed.
  }
  
  // too long size, failed
  return FALSE;
}

//==============================================================
// This function assumes the signal name "s" is unique
s32 ScanSignalDescriptions(u32 StartIndex, char* s) {

  for(u16 n=StartIndex;n<countof(PAD);n++)
    if(DoLabelMatch(PAD[n].SignalNameString,s))
      return n;
    
  return -1;
}

//===============================================================
// Now we need to find out all the possible solutions for this label (and try all of them)


// Wild Character management
const char* Numbers[] = {"0","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15" };


//=============================================================================
// Resource allocation, conflict detection

// First, we need to check when a GPIO is being used
static u8 BookedPin[MAX_PACKAGE_PIN];

void BookPin(u32 PinName) {
  
  if(PinName>MAX_PACKAGE_PIN)
    while(1); // not possible
  
//  if(BookedPin[PinName]!=0)
//    while(1); // this pin is already used
  if(BookedPin[PinName]<0xFF)
    BookedPin[PinName]++;// = 1; // booked
}

u32 IsPinLocked(u32 PinName) {

  if(PinName>MAX_PACKAGE_PIN)
    while(1); // not possible
  
  return (BookedPin[PinName>1]);
}

void FreePin(u32 PinName) { // not implemented yet

  if(PinName>MAX_PACKAGE_PIN)
    while(1); // not possible

  BookedPin[PinName] = 0; // booked
}


//===================================================
// Sequencer related function
u32 sq_PinSetLowJob(u32 u) {
  
  u32* p = (u32*) u;
  IO_Pin_t* Pin = (IO_Pin_t*)p[0];
  
  IO_PinSetLow(Pin);
  return 0; // done, immediate
}

u32 sq_PinSetHighJob(u32 u) {

  u32* p = (u32*) u;
  IO_Pin_t* Pin = (IO_Pin_t*)p[0];

  IO_PinSetHigh(Pin);
  return 0; // done, immediate
}

//==================================================
// Helper function to show the different category of pins to configure similarly
u32 ConfigurePinAsAnalog(IO_Pin_t* P) {
  
  IO_PinClockEnable(P);
  IO_PinSetInput(P);
  IO_PinSetLow(P);
  IO_PinSetSpeedMHz(P,1);
  IO_PinEnablePullUpDown(P, DISABLE, DISABLE);
  IO_PinEnableHighDrive(P, DISABLE);
  IO_PinSetAnalog(P);
  return 0;
}

u32 ConfigurePinAsInputTrigger(IO_Pin_t* P) {

  IO_PinClockEnable(P);
  IO_PinSetInput(P);
  IO_PinSetLow(P);
  IO_PinSetSpeedMHz(P, 1);
  IO_PinEnablePullUpDown(P, ENABLE, DISABLE);
  IO_PinEnableHighDrive(P, DISABLE);
  IO_PinSetInput(P);
  return 0;
}

u32 ConfigurePinAsPushPullOutput(IO_Pin_t* P) {
  
  IO_PinClockEnable(P);
  IO_PinSetSpeedMHz(P, 1);
  IO_PinSetLow(P);  
  IO_PinSetOutput(P);  
  IO_PinEnablePullUpDown(P, DISABLE, DISABLE);
  IO_PinEnableHighDrive(P, ENABLE);  
  return 0;
}

u32 ConfigurePinAsPushPullOutputPD(IO_Pin_t* P) {
  
  ConfigurePinAsPushPullOutput(P);
  IO_PinEnablePullUpDown(P, DISABLE, ENABLE);
  return 0;
}

u32 ConfigurePinAsPushPullOutputPU(IO_Pin_t* P) {
  
  ConfigurePinAsPushPullOutput(P);
  IO_PinEnablePullUpDown(P, ENABLE, DISABLE);
  return 0;
}

u32 ConfigurePinAsOpenDrainPU(IO_Pin_t* P) {

  IO_PinClockEnable(P);
  IO_PinSetSpeedMHz(P,1);
  IO_PinSetHigh(P);    
  IO_PinSetOutput(P);  
  IO_PinEnablePullUpDown(P, ENABLE, DISABLE);
  IO_PinEnableHighDrive(P, DISABLE);
  return 0;
}


//=====================================
// Interpreter
typedef struct {
  const PinNameDef Name;
  const char* sName;
} PinNameAndString_t;

const PinNameAndString_t hmPinNameAndString[] = {
  { PA0, "PA0" },  
  { PA1, "PA1" },
  { PA2, "PA2" },
  { PA3, "PA3" },
  { PA4, "PA4" },
  { PA5, "PA5" },
  { PA6, "PA6" },
  { PA7, "PA7" },
  { PA8, "PA8" },
  { PA9, "PA9" },
  { PA10, "PA10" },
  { PA11, "PA11" },
  { PA12, "PA12" },
  { PA13, "PA13" },
  { PA14, "PA14" },
  { PA15, "PA15" },
  //==============
  { PB0, "PB0" },  
  { PB1, "PB1" },
  { PB2, "PB2" },
  { PB3, "PB3" },
  { PB4, "PB4" },
  { PB5, "PB5" },
  { PB6, "PB6" },
  { PB7, "PB7" },
  { PB8, "PB8" },
  { PB9, "PB9" },
  { PB10, "PB10" },
  { PB11, "PB11" },
  { PB12, "PB12" },
  { PB13, "PB13" },
  { PB14, "PB14" },
  { PB15, "PB15" },
  //==============
  { PC0, "PC0" },  
  { PC1, "PC1" },
  { PC2, "PC2" },
  { PC3, "PC3" },
  { PC4, "PC4" },
  { PC5, "PC5" },
  { PC6, "PC6" },
  { PC7, "PC7" },
  { PC8, "PC8" },
  { PC9, "PC9" },
  { PC10, "PC10" },
  { PC11, "PC11" },
  { PC12, "PC12" },
  { PC13, "PC13" },
  { PC14, "PC14" },
  { PC15, "PC15" },
  //==============
  { PD0, "PD0" },  
  { PD1, "PD1" },
  { PD2, "PD2" },
  { PD3, "PD3" },
  { PD4, "PD4" },
  { PD5, "PD5" },
  { PD6, "PD6" },
  { PD7, "PD7" },
  { PD8, "PD8" },
  { PD9, "PD9" },
  { PD10, "PD10" },
  { PD11, "PD11" },
  { PD12, "PD12" },
  { PD13, "PD13" },
  { PD14, "PD14" },
  { PD15, "PD15" },
  //==============
  { PE0, "PE0" },  
  { PE1, "PE1" },
  { PE2, "PE2" },
  { PE3, "PE3" },
  { PE4, "PE4" },
  { PE5, "PE5" },
  { PE6, "PE6" },
  { PE7, "PE7" },
  { PE8, "PE8" },
  { PE9, "PE9" },
  { PE10, "PE10" },
  { PE11, "PE11" },
  { PE12, "PE12" },
  { PE13, "PE13" },
  { PE14, "PE14" },
  { PE15, "PE15" },
  //==============
  { PF0, "PF0" },  
  { PF1, "PF1" },
  { PF2, "PF2" },
  { PF3, "PF3" },
  { PF4, "PF4" },
  { PF5, "PF5" },
  { PF6, "PF6" },
  { PF7, "PF7" },
  { PF8, "PF8" },
  { PF9, "PF9" },
  { PF10, "PF10" },
  { PF11, "PF11" },
  { PF12, "PF12" },
  { PF13, "PF13" },
  { PF14, "PF14" },
  { PF15, "PF15" },
  //==============
  { PG0, "PG0" },  
  { PG1, "PG1" },
  { PG2, "PG2" },
  { PG3, "PG3" },
  { PG4, "PG4" },
  { PG5, "PG5" },
  { PG6, "PG6" },
  { PG7, "PG7" },
  { PG8, "PG8" },
  { PG9, "PG9" },
  { PG10, "PG10" },
  { PG11, "PG11" },
  { PG12, "PG12" },
  { PG13, "PG13" },
  { PG14, "PG14" },
  { PG15, "PG15" },
  //==============
  { PH0, "PH0" },  
  { PH1, "PH1" },
  { PH2, "PH2" },
  { PH3, "PH3" },
  { PH4, "PH4" },
  { PH5, "PH5" },
  { PH6, "PH6" },
  { PH7, "PH7" },
  { PH8, "PH8" },
  { PH9, "PH9" },
  { PH10, "PH10" },
  { PH11, "PH11" },
  { PH12, "PH12" },
  { PH13, "PH13" },
  { PH14, "PH14" },
  { PH15, "PH15" },
  //==============
  { PI0, "PI0" },  
  { PI1, "PI1" },
  { PI2, "PI2" },
  { PI3, "PI3" },
  { PI4, "PI4" },
  { PI5, "PI5" },
  { PI6, "PI6" },
  { PI7, "PI7" },
  { PI8, "PI8" },
  { PI9, "PI9" },
  { PI10, "PI10" },
  { PI11, "PI11" },
  { PI12, "PI12" },
  { PI13, "PI13" },
  { PI14, "PI14" },
  { PI15, "PI15" },
  //==============
  { PJ0, "PJ0" },  
  { PJ1, "PJ1" },
  { PJ2, "PJ2" },
  { PJ3, "PJ3" },
  { PJ4, "PJ4" },
  { PJ5, "PJ5" },
  { PJ6, "PJ6" },
  { PJ7, "PJ7" },
  { PJ8, "PJ8" },
  { PJ9, "PJ9" },
  { PJ10, "PJ10" },
  { PJ11, "PJ11" },
  { PJ12, "PJ12" },
  { PJ13, "PJ13" },
  { PJ14, "PJ14" },
  { PJ15, "PJ15" },
  //==============
  { PK0, "PK0" },  
  { PK1, "PK1" },
  { PK2, "PK2" },
  { PK3, "PK3" },
  { PK4, "PK4" },
  { PK5, "PK5" },
  { PK6, "PK6" },
  { PK7, "PK7" },
  //==============
  
};

// With above table we can convert from pinname to string (human-machine hm)
const char* hmPinNameToString(PinNameDef Name) {
  
  for(u16 n=0;n<countof(hmPinNameAndString);n++)
    if(hmPinNameAndString[n].Name == Name)
      return hmPinNameAndString[n].sName;
    
  return 0; // not found
}

// on the other way around, it's about string matching... to check if helper function exists
