
// Should the hook change the interrupt level back to original?
// Will the stack survive?
// Should the interrupt level change after the callback is done to avoid the stack to be too deep?
// if there is no hook, it means main loop + flag is going to be used?

// Right now, we assume the I2C is done from main loop, not under interrupt handler.
// If need reactivity, then set a flag which will trigger action for I2C to proceed.
// Much later, we can try to have Interupt handler and queue for pending job from any source with callback (and its interrupt level)

// already, with this scheme, the I2C master can be duplicated on as many pins as possible, which is not bad already.
// low power, clock speed tuning are possible from this base.
#include "sebEngine.h"
//~~~~~~~~~~~~~~~~~~~~~ global variables to use differently~~~~~~~~~~~

u32 Receive (u32 u, u32 DoAck);
u32 Transmit (u32 u, u8 bValue);
u32 GenerateStart (u32 u, u8 SlaveAdr);
u32 ErrorRecovery (u32 u);
u32 GenerateStop (u32 u);

//u32 I2C_MasterIO_EXTI_IRQHandler(u32 u); // the event handler function for this I2Cslave, passing the context to it
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
u32 TimerCountdownWait(u32 u);
u32 NopsWait(u32 u);
//--------- this will become later global resources --------

void NewI2C_MasterIO_SDA_SCL(I2C_MasterIO_t* M, IO_Pin_t* SDA, IO_Pin_t* SCL) {

  if((SDA==0)||(SCL==0)) while(1); // define the 2 pins please.
  if(M==0) while(1); // define a RAM structure for the I2C Master!
  
  M->SDA = SDA;
  M->SCL = SCL;
}

//
void SetI2C_MasterIO_Timings(I2C_MasterIO_t* M, u32 MinBps, u32 MaxBps) { 

  u32 HalfClockPeriod_Hz;
  u32 HalfClockPeriod_us;
  
  if(M->Clocks==0) while(1); // you must link this to the clock tree BEFOREhands
  
  if(MinBps>400000) while(1); // max 400kbps
  if(MinBps==0) MinBps = 50000;  // if not defined, 50kHz minimum
  M->Bps.Min = MinBps; // 400khz
  
  HalfClockPeriod_Hz = MinBps*2; // Timers runs at 1MHz max overflow speed. 500kHz = 2us
  
  if(M->Timer) {
    
    M->WaitParam = 1000000 / ( M->Timer->OverflowPeriod_us * HalfClockPeriod_Hz);
    if(M->WaitParam) {
      M->fnWaitMethod = TimerCountdownWait; // here we should take care of the timings, and choose the best scheme based on CPU MHz and bps of bus...    
      return; // found a tick period compatible with this Basic Timer
    };
    // otherwise, the strategy will be NOPs.
  }

  // Delay will use S/W NOPs because the incoming sys frequency is too low or the bit rate too high
  //!!! In IAR, 96MHz core STM32F437, No optimisation: WaitParam = 2. With max optimisation for speed: WaitParam = 20 (400kHz)
  // Later, we will use the help of the BT with precise time to tune it dynamically.... more high tech and requires some time to tune.
  M->fnWaitMethod = NopsWait;
  
  // Here 2 options:
  // Case 1: The Value is zero = update only min/max for future computation
  // Case 2: The Value is non zero = tune the cell for this frequency
  if(M->Clocks->OutCoreClk_Hz.Value) { // clock is known, compute local timings and prescalers for it
    HalfClockPeriod_us = (M->Clocks->OutCoreClk_Hz.Value)/(MinBps*12); // Here the feedclock is the core speed for IO emulated we assume 1 cycle per instruction which is not realistic.
    // it should not be zero or the delays won't scale for the communication
    M->WaitParam = HalfClockPeriod_us;
  }else{ // clock is not defined, narrow down min and max if necessary
    // estimate the minimum Core frequency for this emulated bitbanging IO scheme
    MakeItNoLessThan(M->Clocks->OutCoreClk_Hz.Min, MinBps * 12 * 20);
  }
}

void SetI2C_MasterIO_Format(I2C_MasterIO_t* M) {
  
}

//=============================================
void ConfigureI2C_MasterIO(I2C_MasterIO_t* M) { // configure the HW registers

  // configure SDA pin
  ConfigurePinAsOpenDrainPU(M->SDA);
  // configure SCL pin
  ConfigurePinAsOpenDrainPU(M->SCL);
}

void EnableI2C_MasterIO(I2C_MasterIO_t* M) {
  
}
//==============================================
//=============== These are the optional I2C spy mode to see what is happening on a bus.
// the slave function is then disabled

static u32 TimerCountdownWait(u32 u) {
  
  I2C_MasterIO_t* M = (I2C_MasterIO_t*) u;
  ArmTimerCountdown(M->Timer, M->Cn, M->WaitParam * M->ctWaitMethod);
  while(M->Timer->CountDownDone[M->Cn]==0) ;
  return 0;
}

static u32 NopsWait(u32 u) {
  I2C_MasterIO_t* M = (I2C_MasterIO_t*) u;
  u32 n = M->ctWaitMethod * M->WaitParam;
  while(n--) asm("nop\n");
  return 0;
}

static u32 WaitHere(u32 u, u32 delay) {
  I2C_MasterIO_t* M = (I2C_MasterIO_t*) u;
  M->ctWaitMethod = delay;
  if(M->fnWaitMethod) M->fnWaitMethod(u);
  return 0;
}
 


// This function can be called upon any I2C bus error condition.
static u32 ErrorRecovery (u32 u) 
{
  // blindly generate 9 stop bits to flush any stuck situation
  // non-invasive, no side effects.
  GenerateStop(u);
  GenerateStop(u);
  GenerateStop(u);
  GenerateStop(u);
  GenerateStop(u);
  GenerateStop(u);
  GenerateStop(u);
  GenerateStop(u);	// flush the bus if it is stuck
  return u;
}

static u32 GenerateStart (u32 u, u8 SlaveAdr) 
{
  I2C_MasterIO_t* M = (I2C_MasterIO_t*) u;
  M->SlaveAdr = SlaveAdr;
  IO_PinSetHigh(M->SDA);//dir_I2C_SDA_IN;	// to check if I2C is idle... or stuck
  WaitHere(u,1);
  if(IO_PinGet(M->SDA)==0)	// Samsung decoder has I2C compatibility problems, it does not detect NACK in Read Mode...
          ErrorRecovery(u);

  // Seb this is bugged, it's not a start bit if SCL is low...  it's too short.
  IO_PinSetHigh(M->SCL);//bit_I2C_SCL_HIGH;
  WaitHere(u,1);					

  // Fixed violation on Start hold time
  IO_PinSetLow(M->SDA);//bit_I2C_SDA_LOW;
  WaitHere(u,1);

  IO_PinSetLow(M->SCL);//bit_I2C_SCL_LOW;
  WaitHere(u,1);

  return Transmit (u,SlaveAdr);				// Send the slave address
}


static u32 Transmit(u32 u, u8 bValue) 
{
  I2C_MasterIO_t* M = (I2C_MasterIO_t*) u;
  u8 loop;

  for (loop = 0; loop < 8; loop++) 
  {
      if (bValue & 0x80) {
        IO_PinSetHigh(M->SDA);//bit_I2C_SDA_HIGH;
      }else{ 
        IO_PinSetLow(M->SDA);//bit_I2C_SDA_LOW;
      }

      WaitHere(u,1);// Sept 17
//		dir_I2C_SDA_OUT;				// make sure SDA is configured as output (once DR initialised)

      IO_PinSetHigh(M->SCL);//bit_I2C_SCL_HIGH;
      WaitHere(u,1);//1028
      bValue <<= 1;
      IO_PinSetLow(M->SCL);//bit_I2C_SCL_LOW;
      WaitHere(u,1);
  }

  // Acknowledge Write
  // bit_I2C_SCL = HIGH;
  // ack is READ to check if Slave is responding

  IO_PinSetHigh(M->SDA);//dir_I2C_SDA_IN;
  WaitHere(u,1);
  IO_PinSetHigh(M->SCL);//bit_I2C_SCL_HIGH;	// SCL = 1
  WaitHere(u,1);					

  // Here we could sense NACK and manage error info to calling function
  // for debug to find ACK bit as long scl pulse...
  // Error = bit_I2C_SDA; // 1 = Error, 0 = Ok
  IO_PinSetHigh(M->SCL);//bit_I2C_SCL_HIGH;	// SCL = 1
  WaitHere(u,2);//	NOP;					
  M->AckFail |= IO_PinGet(M->SDA);	// Acknowledge bit

  IO_PinSetLow(M->SCL);//bit_I2C_SCL_LOW;	// SCL = 0

  WaitHere(u,1);// Sept 17

//-	SetSDAOutput();//dir_I2C_SDA_OUT;

//-	WaitHere(u,1);//	NOP;	add sept 17

  return M->AckFail;
}


static u32 Receive (u32 u, u32 DoAck) 
{ 
  I2C_MasterIO_t* M = (I2C_MasterIO_t*) u;
  u8 bValue, loop;

  bValue = 0;
  IO_PinSetHigh(M->SDA);//dir_I2C_SDA_IN; // make SDA as input before reading pin level

  for (loop = 0; loop < 8; loop ++) 
  {
      WaitHere(u,1);// NOP; NOP;	// 1 us delay

      IO_PinSetHigh(M->SCL);//bit_I2C_SCL_HIGH;	// SCL = 1
      WaitHere(u,2);					
      bValue <<= 1;

      if(IO_PinGet(M->SDA)) bValue++;
      IO_PinSetLow(M->SCL);//bit_I2C_SCL_LOW;	// SCL = 0
      WaitHere(u,1);//1028
  }

// Manage the ackknowledge bit
  if(DoAck) {
    IO_PinSetLow(M->SDA);//bit_I2C_SDA_LOW;
  }else{
    IO_PinSetHigh(M->SDA);//bit_I2C_SDA_HIGH;
  }

//  SetSDAOutput();//dir_I2C_SDA_OUT; // make sure SDA is configured as output (once DR initialised)
  WaitHere(u,1);	// enlarge the pulse to see it on the scope
  IO_PinSetHigh(M->SCL);//bit_I2C_SCL_HIGH;	// SCL = 1
  WaitHere(u,1);
  IO_PinSetLow(M->SCL);//bit_I2C_SCL_LOW;	// SCL = 0

  WaitHere(u,1);//	NOP;	add sept 17

  if(!DoAck)	// If NACK, STOP will automatically follows (I2C spec)
    GenerateStop(u);

  return bValue;
}


static u32 GenerateStop (u32 u) {
  
  I2C_MasterIO_t* M = (I2C_MasterIO_t*) u;
  IO_PinSetLow(M->SCL);//bit_I2C_SCL_LOW;
  WaitHere(u,1);
  IO_PinSetLow(M->SDA);//bit_I2C_SDA_LOW;
  WaitHere(u,1);							// Extra to make sure delay is ok
  
  IO_PinSetHigh(M->SCL);//bit_I2C_SCL_HIGH;
  WaitHere(u,1);

  IO_PinSetHigh(M->SDA);//bit_I2C_SDA_HIGH;
//	WaitHere(u,1);
  return 0;
}


//================================---------------------------> 8>< <----------------===========================]
// Here we try to have a job scheduler which more or less will look like a sequence of I2C communication happening...
// This way we can prepare some job that could be done in background (when background I2C Master becomes possible with a BYTE HW cell

static u32 I2C_MIO_Start(u32 u, u32 SlaveAdr) {

  I2C_MasterIO_t* M = (I2C_MasterIO_t*) u;

  M->AckFail = GenerateStart (u,SlaveAdr);				// Send the slave address
  return 0; // no interrupt setup
}

// This is NEVER NEEDED unless error recovery is asked
static u32 I2C_MIO_Stop(u32 u, u32 BitMask) { // This is used only in transmit mode (Adr.b0=0)

//  I2C_MasterIO_t* S = (I2C_MasterIO_t*) u;
  GenerateStop(u);
  WaitHere(u,1);
  return 0; // no call back, next job right away
}


static u32 I2C_MIO_Move(u32 u, u32 Param1, u32 Param2, u32 Param3) { // Param1: Block adr, Param2: Block size byte, Param3: Ack when read or not
  
  I2C_MasterIO_t* S = (I2C_MasterIO_t*) u;
  u8* pu8 = (u8*) Param1;
  u16 bCount = (u16) Param2;
  u8 MoreComing = Param3;
  
  // if the TX adr is null, no bytes to transmit, disable MOSI, and transmit dummy things instead
  if(pu8==0) while(1); // not supported with no buffer pointer
//  if(bCount==0) while(1); // not supported

  if(S->SlaveAdr & 1) { // receiver mode

    if((bCount==0)||(S->AckFail)) {
      Receive(u,0);
    }else
    {
      do{ // at least read one byte if ACK failed on slave address, to generate the STOP bit at right opportunity. If 0 bytes requested, will still read one of them
          *pu8++ = Receive (u, MoreComing | bCount); // a STOP bit will be generated if no more packets and last byteCount :-)
        } while(bCount--);
    }

  }else{ // transmitter mode

    while((S->AckFail==0)&& (bCount--)) { // for all bytes... if ack fail, skip the transmit part
       S->AckFail |= Transmit (u, *pu8++);
    };

    // let's take care of the stop bit if last packet here for compactness
    if(MoreComing==FALSE)
      GenerateStop(u);
  };
  
  return 0; // no interrupt call back setup here
}


//==============================================================
// SEQUENCER COMPATIBLE FUNCTION START

// This is the single instruction process
// These functions can be used in sequencers
// from 3 param down to 1
u32 sq_I2C_MIO_StartJob(u32 u) {
  u32* p = (u32*) u;
  return I2C_MIO_Start(p[0], p[1]);//Param1);//1 parameter
}

// from 3 param down to 1
u32 sq_I2C_MIO_StopJob(u32 u) {
  u32* p = (u32*) u;
  return I2C_MIO_Stop(p[0], p[1]);//Param1);
}

u32 sq_I2C_MIO_MoveJob(u32 u) {
  u32* p = (u32*) u;
  return I2C_MIO_Move(p[0], p[1], p[2], p[3]);
}

u32 sq_I2C_MIO_DMA_Interrupt(u32 u) {
//  u32* p = (u32*) u;
//  DMA_Interrupt(p[0], (FunctionalState)p[1]);
  return 0;
}
 
