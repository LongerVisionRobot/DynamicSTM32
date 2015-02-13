
#include "SebEngine.h"
#include "NHD_C0216CZ_LCD16x2.h"


#define LCD_WAKE_UP                          0x30    //DATA interface 8BIT, instruction set=0
#define LCD_CLEAR_DISPLAY                    0x01    //clear display
#define LCD_RETURN_HOME   									 0x02		//return cursor home
#define LCD_ENTRY_MODE    									 0x06		//set entry mode
#define LCD_DISPLAY_ON_CUR_OFF    					 0x0C		//display on, cursor off
#define LCD_DISPLAY_ON_CUR_ON     					 0x0E		//display on, cursor on
#define LCD_FUNC_SET_DATA8_LINE2_DHEIGHT_IS  0x3D		//data 8 bit, 2 line, double height font, instruction set=1
#define LCD_FUNC_SET_DATA4_LINE2_DHEIGHT_IS  0x2D		//data 4 bit, 2 line, double height font, instruction set=1
#define LCD_FUNC_SET_DATA8_LINE1_DHEIGHT_IS  0x35		//data 8 bit, 1 line, double height font, instruction set=1
#define LCD_FUNC_SET_DATA4_LINE1_DHEIGHT_IS  0x25		//data 4 bit, 1 line, double height font, instruction set=1
#define LCD_FUNC_SET_DATA8_LINE2_NDHEIGHT_IS 0x39		//data 8 bit, 2 line, double height font, instruction set=1
#define LCD_INT_OSC 												 0x14 		//internal osc frequency
#define LCD_POWER_CONTROL 									 0x56  	//power control
#define LCD_FOLLOWER_CONTROL 								 0x6D  	//follower control
#define LCD_CONTRAST_SET_low 								 0x73    //contrast SET + DATA CONTRAST (LAST 4 DIGIT) //70
#define LCD_CONTRAST_SET_high 							 0x7F    //contrast SET + DATA CONTRAST (LAST 4 DIGIT) //70

u32 NewNHD_LCD16x2(NHD_LCD16x2* L) {
  
  // Initialize SPI Master GPIO
  NewSPI_MasterIO_RX_TX(L->MIO);
  
  NHD_FillRAM_With(L, '-');//NHD_ClearRAM(NHD_LCD16x2* L)
  
  IO_PinClockEnable(L->RS);
  IO_PinSetHigh(L->RS);
  IO_PinSetOutput(L->RS);  
  IO_PinEnablePullUpDown(L->RS, ENABLE, DISABLE);
  IO_PinEnableHighDrive(L->RS, ENABLE);

  IO_PinClockEnable(L->NRST);
  IO_PinSetHigh(L->NRST);
  IO_PinSetOutput(L->NRST);  
  IO_PinEnablePullUpDown(L->NRST, ENABLE, DISABLE);
  IO_PinEnableHighDrive(L->NRST, ENABLE);  
  
  L->Print.fnPutChar = (u32) NHD_LCD_putchar;
  L->Print.ctPutChar = (u32) L;
  return 0;
}

u32 SetNHD_LCD16x2_Timings( NHD_LCD16x2* L) {

  IO_PinSetSpeedMHz(L->RS, 1);
  IO_PinSetSpeedMHz(L->NRST, 1);
  
  L->FullFrameRefreshRate = 0; // hard update
  L->FrameCountdown = 0;
  
  L->BTn = 0;
  L->MIO->BTn = 1;
  SetSPI_MasterIO_Timings(L->MIO, 100000, SPI_CPOL_Low, SPI_CPHA_1Edge, SPI_FirstBit_MSB, GetMCUClockTree() ); // 1200000, SPI_CPOL_Low, SPI_CPHA_1Edge, SPI_FirstBit_MSB
  return 0;
}

u32 sq_LCD_StartJob(u32 u); // SPI_MasterHW* and bitmask for NSS to go low
u32 sq_LCD_StopJob(u32 u); // same as start, bitmask which NSS will go high.
u32 sq_LCD_MoveJob(u32 u); // SPI_MasterHW* and RX_Adr, TX_Adr, sizeof (4 parameters needed)
u32 sq_LCD_Command(u32 u);
u32 sq_LCD_Data(u32 u);


// The low level commands we are going to replace...
// SEND A BYTE (8 bit push)

#define LCD_NHD16x2SendData(a,b) LCD_NHD16x2SendByte((a), (b), 0)
#define LCD_NHD16x2SendCommand(a,b) LCD_NHD16x2SendByte((a), (b), 1)

u32 LCD_NHD16x2SendByte(u32 u, u8 byte, u8 IsCommand)
{
  // refrain from direct hardcoding for now
  // because we are going to push Jobs in a stack (including memories) later and the data will be preserved in it.
  // then, if we use a HW SPI, the jobs will accumulate and be processed in background without blocking later on.
  NHD_LCD16x2* L = (NHD_LCD16x2*) u;
  
  L->Byte = byte;// to have a physical address
  
  L->Jobs[0] = (OneJobType) { sq_SPI_MIO_StartJob, {(u32)L->MIO, 1} };
  if(IsCommand) { // Command
    L->Jobs[1] = (OneJobType) { sq_PinSetLowJob, {(u32) L->RS } };
  }else{          // Data
    L->Jobs[1] = (OneJobType) { sq_PinSetHighJob, {(u32) L->RS } };
  };
  L->Jobs[2] = (OneJobType) { sq_SPI_MIO_MoveJob,  {(u32)L->MIO, (u32)&L->Byte, 0, 1} };
  L->Jobs[3] = (OneJobType) { sq_SPI_MIO_StopJob, {(u32)L->MIO, 1 }};

  AddToSA(L->SA, (u32) &L->Jobs[0]);
  AddToSA(L->SA, (u32) &L->Jobs[1]);
  AddToSA(L->SA, (u32) &L->Jobs[2]);
  AddToSA(L->SA, (u32) &L->Jobs[3]);
    
  StartJobToDoInForeground((u32)L->SA); // for now... will become background possibly later
  while(L->SA->FlagEmptied==0);
  NOPs(1);
  
  return 0;
}

u32 NHD_LCD16x2_UpdateDisplay(NHD_LCD16x2* L) {
  
  if(L->FrameCountdown)
    L->FrameCountdown--;
  
  if(L->FrameCountdown==0) {
    L->FrameCountdown = L->FullFrameRefreshRate; // being zero or not...
    
    NHD_LCD16x2_FullUpdateDisplay(L);
    return 0;
  }
  
  // smart update
  if(L->FrameCountdown==0)
    L->FrameCountdown=L->FullFrameRefreshRate;
  
  NHD_LCD16x2_SmartUpdateDisplay(L);
  return 0;
}

u32 NHD_LCD16x2_FullUpdateDisplay(NHD_LCD16x2* L) {
  
  u8 x,y,c;
  for(y=0;y<LCD_COG_NB_ROW_Y;y++)
    for(x=0;x<LCD_COG_NB_COL_X;x++) {
      LCD_NHD16x2SendCommand((u32)L, 0x80 + y*40 + x);
      c = L->PreviousRAM[y][x] = L->RAM[y][x];
      LCD_NHD16x2SendData((u32)L, c);
    }
  return 0;
}

u32 NHD_LCD16x2_SmartUpdateDisplay(NHD_LCD16x2* L) {
  
  u8 x,y, c;
  for(y=0;y<LCD_COG_NB_ROW_Y;y++)
    for(x=0;x<LCD_COG_NB_COL_X;x++) {
      if(L->PreviousRAM[y][x] == L->RAM[y][x])
        continue; // skip if previous value is same
      
      c = L->PreviousRAM[y][x] = L->RAM[y][x];
      LCD_NHD16x2SendCommand((u32)L, 0x80 + y*40 + x);
      LCD_NHD16x2SendData((u32)L, c);
    }
  return 0;
}


u32 NHD_LCD16x2_Init(NHD_LCD16x2* L)
{
  Wait_ms(100);

	LCD_NHD16x2SendCommand((u32)L, LCD_WAKE_UP); 								//wake up
	NOPs(200);
	LCD_NHD16x2SendCommand((u32)L, LCD_WAKE_UP);							 	//wake up
	LCD_NHD16x2SendCommand((u32)L, LCD_WAKE_UP);							 	//wake up
	
	LCD_NHD16x2SendCommand((u32)L, LCD_FUNC_SET_DATA8_LINE2_NDHEIGHT_IS);
	LCD_NHD16x2SendCommand((u32)L, LCD_INT_OSC); 								//internal osc frequency
	LCD_NHD16x2SendCommand((u32)L, LCD_POWER_CONTROL); 					//power control
	LCD_NHD16x2SendCommand((u32)L, LCD_FOLLOWER_CONTROL); 			//follower control
	LCD_NHD16x2SendCommand((u32)L, LCD_CONTRAST_SET_low); 					//contrast
	LCD_NHD16x2SendCommand((u32)L, LCD_DISPLAY_ON_CUR_OFF); 		//display on
	LCD_NHD16x2SendCommand((u32)L, LCD_ENTRY_MODE);						 	//entry mode
//	LCD_cog_Clear(); 					//clear
	NOPs(300);
	
	// test to clear the screen manually
//	LCD_cog_ClearScreen_RAM();
//        Wait_ms(1);
//	LCD_cog_Printf(__DATE__);
//        Wait_ms(1);
//	LCD_cog_RAM_GOTO_X_Y(0,1);
//        Wait_ms(1);
//	LCD_cog_Printf(__TIME__);
//        Wait_ms(1);
//	LCD_cog_RAM_to_Display();
//        while(1)
	NHD_LCD16x2_UpdateDisplay(L);
// on after reset		if(Is_EEP_Detected())
//			Set_LED_2(0);

//	LCD_cog_ShowTemporaryMessage_1(600, "FROG %4X\nST Singapore", MCU_SerialID);
//Test_my_lcd();      
        return 0;
}

//===============-------> RAM manipulation functions

u32 NHD_FillRAM_With(NHD_LCD16x2* L, u8 c) { // clears the ram, to update the screen, use UpdateDisplay()

  u8 x,y;
  for(y=0;y<LCD_COG_NB_ROW_Y;y++)
    for(x=0;x<LCD_COG_NB_COL_X;x++)
      L->RAM[y][x] = c;
  
  return 0;
}

u32 NHD_ClearRAM(NHD_LCD16x2* L) {
  
  return NHD_FillRAM_With(L, ' ');
}

u32  NHD_LCD_GOTO_X_Y(NHD_LCD16x2* L, u8 x, u8 y)
{
  if(x>=LCD_COG_NB_COL_X)
          return 0;
  if(y>=LCD_COG_NB_ROW_Y)
          return 0; // should this cause error, display update, or nothing?

  L->Cursor_X = x;
  L->Cursor_Y = y;
  return 0;
}

u32 NHD_LCD_GO_Home(NHD_LCD16x2* L) {

  L->Cursor_X = 0;
  L->Cursor_Y = 0;
  
  return 0;
}


u32 NHD_LCD_GOTO_NextLine(NHD_LCD16x2* L)
{
  if(L->Cursor_Y<=LCD_COG_NB_ROW_Y)
    L->Cursor_Y++;
  NHD_LCD_GOTO_X_Y(L, 0,L->Cursor_Y);
  return 0;
}

u32 NHD_LCD_GOTO_HorizontalCenter(NHD_LCD16x2* L)
{
  NHD_LCD_GOTO_X_Y(L, LCD_COG_NB_COL_X/2-1,L->Cursor_Y);
  return 0;
}


// SEND A DATA BYTE TO THE LCD
u32 NHD_LCD_putchar(NHD_LCD16x2* L, u32 c) {
  
  // range check
  if(L->Cursor_X >= LCD_COG_NB_COL_X)
    return 0;
  if(L->Cursor_Y >= LCD_COG_NB_ROW_Y)
    return 0;
          
  // here we can intercept special characters like code 00 to code 0F and have a meaning for it
  if(c==0x0A) { // next line
    NHD_LCD_GOTO_NextLine(L);
    return 0;
  }
  
  if(c==0x09) { // tab
    NHD_LCD_GOTO_HorizontalCenter(L);
    return 0;
  };
  
  // displayable characters
  L->RAM[L->Cursor_Y][L->Cursor_X] = c;
  if(L->Cursor_X<=LCD_COG_NB_COL_X)
    L->Cursor_X++;
  return 0;
}

#ifdef ADD_EXAMPLES_TO_PROJECT

static NHD_LCD16x2 LCD;
static SPI_MasterIO LCD_SPI;
static IO_PinTypeDef NRST;
static IO_PinTypeDef RS;
static IO_PinTypeDef CS;
static IO_PinTypeDef SI;
static IO_PinTypeDef SCL;
static BasicTimer BT;

static u32 JobList[100];
static StuffsArtery myLCDSequence;

u32 NHD_LCD16x2Test(void) {
  
  u32 i;
  
  MCUInitClocks();
  
  // Declare all pins first
  IO_PinInit(&NRST, PE2);
  IO_PinInit(&RS, PE3);
  IO_PinInit(&CS, PE4);
  IO_PinInit(&SCL, PE5);
  IO_PinInit(&SI, PE6);
  
  // Use the pins for the SPI
  LCD_SPI.MISO = &SI;
  LCD_SPI.MOSI = &SI;
  LCD_SPI.SCK = &SCL;
  LCD_SPI.NSSs[0] = &CS;
  
  // Use the SPI for the LCD
  LCD.MIO = &LCD_SPI;
  
  // Use the pins for the LCD
  LCD.NRST = &NRST;
  LCD.RS = &RS;
  
  // Declare the BT
  NewBasicTimer_us(&BT, TIM6, 100, GetMCUClockTree());// 100us tick period
  NVIC_BasicTimersEnable(ENABLE);
  
  // Use BT for LCD and SPI (2 channels used)
  LCD.BT = &BT;
  LCD_SPI.BT = &BT;
  
  
  // Declare the Sequence (Job FIFO List)
  StuffsArtery* SA = &myLCDSequence; // program
  NewSA(SA, (u32)&JobList[0], countof(JobList));    
  
  // Use Sequence for LCD
  LCD.SA = SA;
  
  // Initialize the SPI (done by LCD)
  
  // Initialize the BT (Done by Declaring it)
  
  // Initialize the LCD
  NewNHD_LCD16x2(&LCD);
  SetNHD_LCD16x2_Timings(&LCD);
  
  // that's it! Now we need to control the LCD, by initializing/reset and send a message with delays on it.
  NHD_LCD16x2_Init(&LCD);
  
  LCD.FullFrameRefreshRate = 2;
  LCD.FrameCountdown = 0;

  i = 10000;
  while(1) {
    NHD_ClearRAM(&LCD);
    NHD_LCD_GO_Home(&LCD);
    SebPrintf(&LCD.Print, "Hello\tworld!\n%d %4X", (u32)i, (u32)i);
    NHD_LCD16x2_UpdateDisplay(&LCD);
    i++;
    
    
  }

  
  return 0;
}


#endif