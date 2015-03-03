#include "sebEngine.h"

// TODO: Rename I2C_Slave by SIO for the local variables.

// It is a state machine, we need to define all bits we need
// SDAEdge->SDALevel SCLEdge->SCLLevel
// [SDAEdge] [SDASymbol] [SCLEdge] [SCLSymbol]
const I2C_Symbols I2C_EventToSymbol[] = {
  I2C_Error, // 00 00 => SDA=0, SCL=0 Error
  I2C_Error, // 00 01 => SDA=0, SCL=1 Error
  I2C_SDA0SCLFall, // 00 10 => SDA=0, SCL->0 : No action
  I2C_SDA0SCLRise, // 00 11 => SDA=0, SCL->1 : "0" data bit sampled
  I2C_Void, // 01 00 => SDA->1, SCL=0 : No action
  I2C_Error, // 01 01 => SDA=1, SCL=1 Error
  I2C_SDA1SCLFall, // 01 10 => SDA=1, SCL->0 : No action
  I2C_SDA1SCLRise, // 01 11 => SDA=1, SCL->1 : "1" data bit sampled
  I2C_Void, // 10 00 => SDA->0, SCL=0 : No action
  I2C_Start, // 10 01 => SDA->0, SCL=0 : START
  I2C_Error, // 10 10 => SDA->0, SCL->0 : Error (too slow or glitch)
  I2C_Error, // 10 11 => SDA->0, SCL->1 ; Considered error (too slow)
  I2C_Void, // 11 00 => SDA->1, SCL=0 : No action
  I2C_Stop, // 11 01 => SDA->1, SCL=1 :STOP bit detected
  I2C_Error, // 11 10 => SDA->1, SCL->0 : Error (too slow or glitch)
  I2C_Error// 11 11 => SDA->1, SCL->1 : Error (too slow or glitch)
};

//~~~~~~~~~~~~~~~~~~~~~ global variables to use differently~~~~~~~~~~~

u32 I2C_SlaveIO_EXTI_IRQHandler(u32 u); // the event handler function for this I2Cslave, passing the context to it

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
u32 I2C_SlaveIO_STMA_Run(u32 u);
extern u32 I2C_SlaveIO_RAM24C02_States2fn[32]; // this contains the state machine (state to function look up table).

//--------- this will become later global resources --------

void NewI2C_SlaveIO(I2C_SlaveIO* S, u8* SlaveAddresses, u8 SlaveAddressesCountof, u8* pMemory, u32 MemoryCountof) {

  u32 pinSDA = S->SDA->Name;
  u32 pinSCL = S->SCL->Name;
// we have to initialize the state machine first
  S->STMA.State = 0;
  S->STMA.fnStates = I2C_SlaveIO_RAM24C02_States2fn;
  S->STMA.ct = (u32)S;
  I2C_SlaveIO_STMA_Run((u32)&(S->STMA));// run from state 0 (this ones does not use any I2C_Symbol as input)
  S->Addresses = SlaveAddresses;
  S->AddressesCountof = SlaveAddressesCountof;
  S->SubAddressInitial = S->SubAddress = 0;
  S->MemoryRead = 0;
  S->MemoryWritten = 0;
  S->Memory = pMemory;
  S->MemoryCountof = MemoryCountof;
  S->Bitmask = 0;
  S->Data = 0;
  S->BusBusy = 0; // should be not available until a start or a stop is seen on the bus

  S->TalkEnded = 0;

  // configure the GPIOs
  // configure SDA pin
  IO_PinClockEnable(S->SDA);
  IO_PinSetHigh(S->SDA);
  IO_PinSetOutput(S->SDA);
  //IO_PinSetLow(S->SDA);
  IO_PinSetSpeedMHz(S->SDA, 1);
  IO_PinEnablePullUpDown(S->SDA, ENABLE, DISABLE);
  IO_PinEnableHighDrive(S->SDA, DISABLE);

  // configure SCL pin
  IO_PinClockEnable(S->SCL);
  IO_PinSetInput(S->SCL);
  IO_PinSetLow(S->SCL);
  IO_PinSetSpeedMHz(S->SCL, 1);
  IO_PinEnablePullUpDown(S->SCL, ENABLE, DISABLE);
  IO_PinEnableHighDrive(S->SCL, DISABLE);

  // let's setup the edge configuration first
  EXTI_SetEdgesEnable(pinSDA, ENABLE, ENABLE); // GPIO, channel and edge configuration
  EXTI_SetEdgesEnable(pinSCL, ENABLE, ENABLE); // GPIO, channel and edge configuration

  // The I2C_Slave has its own hooks to setup first
  S->fnSlaveScheme = I2C_SlaveIO_STMA_Run; // run the state machine instead of a hardcoded stuff
  
  // debug code optional hook here
  // for now we hook the corresponding IRQ to the IRQ handler for EXTI channels
  // putting a non zero hook is needed to activate the EXTI interrupt enable going to NVIC channel
  HookEXTIn(pinSDA & 0xF, (u32) I2C_SlaveIO_EXTI_IRQHandler , (u32)S); // this will move to EXTI cell later, it will activate the EXTI Interrupt enable, not the NVIC (later)
  EXTI_Interrupt(pinSDA &0xF, ENABLE);
  HookEXTIn(pinSCL & 0xF, (u32) (EXTI_PinShareSameNVIC(pinSDA,pinSCL)?(u32_fn_u32):(I2C_SlaveIO_EXTI_IRQHandler)), (u32)S); // shares the same IRQ handler, so don't call the same handler twice!
  EXTI_Interrupt(pinSCL & 0xF, ENABLE);
// not needed if they share the same IRQ  fnEXTI_n_Hook_To(pinSCL & 0xF, (u32) I2C_SlaveIO_EXTI_IRQHandler, (u32)&gI2C_Slave); // this will move to EXTI cell later

}

//TODO: Create a Timings here

//#define SetSDAOutput()          IO_PinSetHigh(S->SDA)
//#define SetSDAInput()           IO_PinSetLow{S->SDA)

//==============================================

u32 I2C_SlaveIO_SpyProcess(u32);

// The code should be in RAM to change easily the behaviour, but the linker will be lost...
static u32 I2C_SlaveIO_EXTI_IRQHandler(u32 u) {

  I2C_SlaveIO* S = (I2C_SlaveIO*) u;

  u8 EventMask; // local variables generates less code than global
  // Implementation is made to enable low power scheme: The max bus speed depends on the core frequency which could be stopped
  // it is not a pin, it is the pin mask... shit.

  EventMask = 0;
  if(IO_PinGetPR(S->SDA)) {
     EventMask |= 0x8;
     IO_PinClearPR(S->SDA);
  };
  if(IO_PinGet(S->SDA))
     EventMask |= 0x4;
  if(IO_PinGetPR(S->SCL)) {
     EventMask |= 0x2;
     IO_PinClearPR(S->SCL);     
  }
  if(IO_PinGet(S->SCL))
     EventMask |= 0x1;
  S->EventMask = EventMask; // for debug  

  S->I2C_Symbol = I2C_EventToSymbol[EventMask];
  if(S->fnSlaveScheme) I2C_SlaveIO_STMA_Run((u32)(&S->STMA));// DIRECT Here we only use state machine which we can disable by zeroing the fnSlaveScheme, we pass the state machine as parameter
  if(S->fnSpyScheme)I2C_SlaveIO_SpyProcess(u); // DIRECT we pass the I2C_Slave pointer here for the spy

  return 0;
}

u32 I2C_SlaveIO_STMA_Run(u32 u) {

  STateMAchine_t* STMA = (STateMAchine_t*) u;// type it
  u32 result;

    if(STMA->fnStates[STMA->State]) result = ((u32(*)(u32))STMA->fnStates[STMA->State])(STMA->ct); // call either an empty function or a hooked one with desired context predefined (if context missing... error)
    else while(1);// error, the state machine is going nowhere

    STMA->State = result;

  return u;
}

//=============== These are the optional I2C spy mode to see what is happening on a bus.
// the slave function is then disabled

u32 SpyI2C_SlaveIO(I2C_SlaveIO* u) {

  u->fnSpyScheme = I2C_SlaveIO_SpyProcess;
  u->I2C_BitCounter = 0;
  return 0;
}

u32 UnSpyI2C_SlaveIO(I2C_SlaveIO* u) {

  u->fnSpyScheme = 0;
  return 0;
}

const u8 HexToAscii[] = {  '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };

static u32 I2C_SlaveIO_SpyProcess(u32 u) {

  I2C_SlaveIO* S = (I2C_SlaveIO*) u;
  I2C_Symbols I2C_Symbol = S->I2C_Symbol;
  
  if(S->BV==0) while(1);// you forgot to point to a BV global structure... pointer is null.

  switch(I2C_Symbol) {
  case I2C_Start:    // SDA goes low while SCL remains high
    AddToBV(S->BV, 'S');//    I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = 'S';
    S->I2C_BitCounter = 0;
    S->I2C_Nibble = 0;
    return 0;
  case I2C_Stop:     // SDA goes high while SCL remains high
    AddToBV(S->BV, 'P'); 
    AddToBV(S->BV, 0x0A);    // add a LF to format line by line//    I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = 'P';
    return 0;
  case I2C_SDA1SCLRise:  // SCL went high and SDA is high
    S->I2C_Nibble|=1;
    S->I2C_BitCounter++;
    if(S->I2C_BitCounter==4)
      AddToBV(S->BV, HexToAscii[S->I2C_Nibble & 0xF]);      //I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = HexToAscii[I2C_Slave->I2C_Nibble & 0xF];
    if(S->I2C_BitCounter==8)
      AddToBV(S->BV, HexToAscii[S->I2C_Nibble & 0xF]);      //I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = HexToAscii[I2C_Slave->I2C_Nibble & 0xF];
    if(S->I2C_BitCounter==9) {
      AddToBV(S->BV, 'n');//      I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = (I2C_Symbol==I2C_BitLow)?'a':'n';
      S->I2C_BitCounter = 0;
    };
    S->I2C_Nibble<<=1;
    return 0;
  case I2C_SDA0SCLRise:   // SCL went high and SDA is low
    S->I2C_BitCounter++;
    if(S->I2C_BitCounter==4)
      AddToBV(S->BV, HexToAscii[S->I2C_Nibble & 0xF]);      //I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = HexToAscii[I2C_Slave->I2C_Nibble & 0xF];
    if(S->I2C_BitCounter==8)
      AddToBV(S->BV, HexToAscii[S->I2C_Nibble & 0xF]);      //I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = HexToAscii[I2C_Slave->I2C_Nibble & 0xF];
    if(S->I2C_BitCounter==9) {
      AddToBV(S->BV, 'a');//      I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = (I2C_Symbol==I2C_BitLow)?'a':'n';
      S->I2C_BitCounter = 0;
    };
    S->I2C_Nibble<<=1;
    return 0;
  case I2C_Error:     // wrong transition due to noise on bus, too fast bus or too slow MCU
    AddToBV(S->BV, '?');    //I2C_Slave->I2C_String[I2C_Slave->I2C_String_Index++] = '?';
    S->I2C_BitCounter = 0;
    S->I2C_Nibble = 0;
    return 0;
  };

  // SCL Falling edge does no recording, we are in receiving mode always
  return 0;
}

//==================== debug final scheme with generic state machine
// these are all the functions used by the state machine
u32 I2C_SlaveIO_sm_MakeSlaveIdle(u32 u);
u32 I2C_SlaveIO_sm_StartBitReceived(u32 u);
u32 I2C_SlaveIO_sm_WaitForStartBit(u32 u);
u32 I2C_SlaveIO_sm_Receive8BitsAdr(u32 u);


u32 I2C_SlaveIO_sm_Receiving8bitSubAdr(u32 u);


u32 I2C_SlaveIO_sm_Receiving8bitData(u32 u);


u32 I2C_SlaveIO_sm_Transmit8BitData(u32 u);

// this is the function pointer array from state value
u32 I2C_SlaveIO_RAM24C02_States2fn[32] = { // RAM for speed
/*  0 */  (u32)I2C_SlaveIO_sm_MakeSlaveIdle,
/*  1 */  (u32)I2C_SlaveIO_sm_WaitForStartBit,
/*  2 */  (u32)I2C_SlaveIO_sm_Receive8BitsAdr,
/*  3 */ 0,
/*  4 */ 0,
/*  5 */  (u32)I2C_SlaveIO_sm_Receiving8bitSubAdr,
/*  6 */ 0,
/*  7 */ 0,
/*  8 */  (u32)I2C_SlaveIO_sm_Receiving8bitData,
/*  9 */ 0,
/* 10 */ 0,
/* 11 */  (u32)I2C_SlaveIO_sm_Transmit8BitData,
/* 12 */ 0, // tbd hackable
/* 13 */ 0, // tbd hackable
/* 14 */ 0, // tbd hackable
/* 15 */ 0,  // tbd hackable
/* 16 */ 0, // tbd hackable
/* 17 */ 0, // tbd hackable
/* 18 */ 0, // tbd hackable
/* 19 */ 0, // tbd hackable
/* 20 */ 0,  // tbd hackable
/* 21 */ 0, // tbd hackable
/* 22 */ 0, // tbd hackable
/* 23 */ 0, // tbd hackable
/* 24 */ 0, // tbd hackable
/* 25 */ 0,  // tbd hackable
/* 26 */ 0, // tbd hackable
/* 27 */ 0, // tbd hackable
/* 28 */ 0, // tbd hackable
/* 29 */ 0, // tbd hackable
/* 30 */ 0, // tbd hackable
/* 31 */ 0  // tbd hackable
};
// size is 32x4=128 bytes

enum {
  S0_MakeSlaveIdle, // 0
  S1_WaitForStartBit, // 1
  S2_Receive8BitsAdr, // 2
  S3_Ack8BitsAdr, // 3
  S4_BeginReceiving8bitSubAdr,  // 4
  S5_Receiving8bitSubAdr, // 5
  S6_AckReceived8bit, // 6
  S7_BeginReceiving8bitData, // 7
  S8_Receiving8bitData, // 8
  S9_WaitForEndOfAckBit, // 9
  S10_WaitForTransmitPhase, // 10
  S11_Transmit8BitData, // 11
  S12_Spare12______________, // 12
  S13_Spare13______________, // 13
  S14_Spare14______________, // 14
  S15_Spare15______________, // 15
  S16_Spare16______________, // 16
  S17_Spare17______________, // 17
  S18_Spare18______________, // 18
  S19_Spare19______________, // 19
  S20_Spare20______________, // 20
  S21_Spare21______________, // 21
  S22_Spare22______________, // 22
  S23_Spare23______________, // 23
  S24_Spare24______________, // 24
  S25_Spare25______________, // 25
  S26_Spare26______________, // 26
  S27_Spare27______________, // 27
  S28_Spare28______________, // 28
  S29_Spare29______________, // 29
  S30_Spare30______________, // 30
  S31_Spare31______________, // 31
  StatesCount, // 16
}; // these are the states



static u32 I2C_SlaveIO_sm_MakeSlaveIdle(u32 u) { // State S0 and state machine reset state
  I2C_SlaveIO* S = (I2C_SlaveIO*) u;
  I2C_Symbols I2C_Symbol = S->I2C_Symbol; // made local var as it is read only in this function  
  // SDA = input
  //  SetSDALow(); // will be low if turns output for ack bit... and stays input otherwise
  IO_PinSetHigh(S->SDA);//SetSDAInput();
  // SCL = input //never an output! no clock stretching!  SetSCLInput();
  S->TalkEnded = 1; // assume communication stopped
  
  // hooks processing
  if(I2C_Symbol==I2C_Stop)
    if(S->fnBusFree) {(u32(*)(u32))S->fnBusFree(u);}else{S->FlagBusFree = 1;};

// here we assume that even if the communication is aborted, if some bytes went through the bus, then we consider calling the hooks anyway.
// if we want only after a valid stop bit, then group it on stop bit.  
  if(S->MemoryRead) // this flag is internal
    if(S->fnTransmitted) { 
      (u32(*)(u32))S->fnTransmitted(u);
      S->MemoryRead = 0;
    }else{S->FlagTransmitted = 1;}; // the flag is cleared because there was a hook, otherwise, poll and clear it by polling

  if(S->MemoryWritten) // this flag is internal
    if(S->fnReceived) {
      (u32(*)(u32))S->fnReceived(u);
      S->MemoryWritten = 0;
    }else{S->FlagReceived = 1;}; // the flag is cleared because there was a hook, otherwise, poll and clear it by polling
  
  return S1_WaitForStartBit;
}


static u32 I2C_SlaveIO_sm_StartBitReceived(u32 u) {
  I2C_SlaveIO* S = (I2C_SlaveIO*) u;
  S->BusBusy = 1;
  S->Bitmask = 0x100;
  S->Data = 0x00;
  return S2_Receive8BitsAdr;// next should be SCL going low 
}


// State S1
static u32 I2C_SlaveIO_sm_WaitForStartBit(u32 u) {
  u32 thisSx = S1_WaitForStartBit;
  I2C_SlaveIO* S = (I2C_SlaveIO*) u;
  I2C_Symbols I2C_Symbol = S->I2C_Symbol; // made local var as it is read only in this function
  switch((u8)I2C_Symbol) {
      case I2C_Start:  // start bit: communication starting right now
                         I2C_SlaveIO_sm_MakeSlaveIdle(u);
                         return I2C_SlaveIO_sm_StartBitReceived(u);
      case I2C_Stop:     S->BusBusy = 0;return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Error:    return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Timeout:  return I2C_SlaveIO_sm_MakeSlaveIdle(u);
  };

  return thisSx;
}


// State S2
static u32 I2C_SlaveIO_sm_Receive8BitsAdr(u32 u) {
  u32 thisSx = S2_Receive8BitsAdr;
  I2C_SlaveIO* S = (I2C_SlaveIO*) u;
  I2C_Symbols I2C_Symbol = S->I2C_Symbol; // made local var as it is read only in this function
  u8 i;
  switch(I2C_Symbol) {
      case I2C_Start:  // start bit: communication starting right now
                         I2C_SlaveIO_sm_MakeSlaveIdle(u);
                         return I2C_SlaveIO_sm_StartBitReceived(u);
      case I2C_Stop:     S->BusBusy = 0;return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Error:    return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Timeout:  return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      
      // the first clock edge we expect in receive mode is rising edge, the last clock edge we expect is ack SCL rising edge for transmit, and ack falling edge for receive
      // the bitmask is shifted right on SCL falling edge
      case I2C_SDA1SCLRise:
        S->Data |= S->Bitmask;   
      case I2C_SDA0SCLRise:// S->Data |= 0;
        if(S->Bitmask!=1) return thisSx; // wait for all 8 address bit to be available
        if(S->AddressesCountof==0) return thisSx; // continue: no address to compare with
        // all data bit received, we can check if we continue...
        for(i=0;i<S->AddressesCountof;i++)
          if((0xFE & S->Data)==S->Addresses[i]) {// we could save which slave address was used
            S->DecodedAddress = S->Data;
            return thisSx;
          }
        // no address match, back to idle
        return I2C_SlaveIO_sm_MakeSlaveIdle(u); // no address match, release lines (NACK) and wait for start bit
                        
      // we shift bitmask on SCL falling edge. no use of the level
      case I2C_SDA0SCLFall:
      case I2C_SDA1SCLFall:
        if(S->Bitmask==1) 
          IO_PinSetLow(S->SDA);//SetSDAOutput(); // ackknowledge
        
        if(S->Bitmask==0) {// transmit or receive next?
          S->Bitmask = 0x80; // byte to send next
          if(S->Data & 1) { // transmit mode now
            S->Data = S->Memory[S->SubAddress];
            // prepare SDA level for the first MSB bit, and we check if the data
            if(S->Data & S->Bitmask) { /*SetSDAInput()*/IO_PinSetHigh(S->SDA); } else { IO_PinSetLow(S->SDA);/*SetSDAOutput();*/ }; // output low
            return S11_Transmit8BitData;
          };
          // continue receive data, next is the subaddress (next event is SCL rise edge as this one
          IO_PinSetHigh(S->SDA);//SetSDAInput();
          S->Data = 0x00;          
          return S5_Receiving8bitSubAdr; // same thing again with different action
        }
        S->Bitmask >>= 1;
        return thisSx;
  };

  return thisSx;
}


// State S5
static u32 I2C_SlaveIO_sm_Receiving8bitSubAdr(u32 u) {
  u32 thisSx = S5_Receiving8bitSubAdr;
  I2C_SlaveIO* S = (I2C_SlaveIO*) u;
  I2C_Symbols I2C_Symbol = S->I2C_Symbol; // made local var as it is read only in this function
  
//===--->
  switch(I2C_Symbol) {
      case I2C_Start:  // start bit: communication starting right now
                         I2C_SlaveIO_sm_MakeSlaveIdle(u);
                         return I2C_SlaveIO_sm_StartBitReceived(u);
      case I2C_Stop:     S->BusBusy = 0;return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Error:    return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Timeout:  return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      
      // the first clock edge we expect in receive mode is rising edge, the last clock edge we expect is ack SCL rising edge for transmit, and ack falling edge for receive
      // the bitmask is shifted right on SCL falling edge
      case I2C_SDA1SCLRise:
        S->Data |= S->Bitmask;  
      case I2C_SDA0SCLRise:// S->Data |= 0;
        if(S->Bitmask!=1) return thisSx; // wait for all 8 subadr bit to be available
        S->SubAddress = S->Data;
        S->SubAddress %= S->MemoryCountof; // address wraps around the memory range
        S->SubAddressInitial = S->SubAddress;
        return thisSx;
                        
      // we shift bitmask on SCL falling edge. no use of the level
      case I2C_SDA0SCLFall:
      case I2C_SDA1SCLFall:
        if(S->Bitmask==1) 
          IO_PinSetLow(S->SDA);//SetSDAOutput(); // ackknowledge
        
        if(S->Bitmask==0) {// get ready to receive data bytes now
          S->Bitmask = 0x80; // byte to send next
          // continue receive data
          IO_PinSetHigh(S->SDA);//SetSDAInput();
          S->Data = 0x00;          
          return S8_Receiving8bitData; // same thing again with different action
        }
        S->Bitmask >>= 1;
        return thisSx;
  };

  return thisSx;
}
  
//===--->  

// State S8
static u32 I2C_SlaveIO_sm_Receiving8bitData(u32 u) {
  
  u32 thisSx = S8_Receiving8bitData;
  I2C_SlaveIO* S = (I2C_SlaveIO*) u;
  I2C_Symbols I2C_Symbol = S->I2C_Symbol; // made local var as it is read only in this function
  
  switch(I2C_Symbol) {
      case I2C_Start:  // start bit: communication starting right now
                         I2C_SlaveIO_sm_MakeSlaveIdle(u);
                         return I2C_SlaveIO_sm_StartBitReceived(u);
      case I2C_Stop:     S->BusBusy = 0;return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Error:    return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Timeout:  return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      
      // the first clock edge we expect in receive mode is rising edge, the last clock edge we expect is ack SCL rising edge for transmit, and ack falling edge for receive
      // the bitmask is shifted right on SCL falling edge
      case I2C_SDA1SCLRise:
        S->Data |= S->Bitmask;  
      case I2C_SDA0SCLRise:// S->Data |= 0;
        if(S->Bitmask!=1) return thisSx; // wait for all 8 subadr bit to be available
        // we received one byte
        S->Memory[S->SubAddress++] = S->Data;
        if(S->SubAddress>=S->MemoryCountof) // address wraps around the memory range
          S->SubAddress = 0; // wraps around
        S->MemoryWritten = 1; // some bytes modified by master!
        return thisSx;

      // we shift bitmask on SCL falling edge. no use of the level
      case I2C_SDA0SCLFall:
      case I2C_SDA1SCLFall:
        if(S->Bitmask==1) 
          IO_PinSetLow(S->SDA);//SetSDAOutput(); // ackknowledge
        
        if(S->Bitmask==0) {// get ready to receive data bytes now
          S->Bitmask = 0x80; // byte to send next
          // continue receive data
          IO_PinSetHigh(S->SDA);//SetSDAInput();
          S->Data = 0x00;          
          return thisSx; // same thing again with different action
        }
        S->Bitmask >>= 1;
        return thisSx;
  };

  return thisSx;
}


//====== SLAVE TRANSMIT MODE (master is reading data from slave)


// State S11
static u32 I2C_SlaveIO_sm_Transmit8BitData(u32 u) {
  
  
  u32 thisSx = S11_Transmit8BitData;
  I2C_SlaveIO* S = (I2C_SlaveIO*) u;
  I2C_Symbols I2C_Symbol = S->I2C_Symbol; // made local var as it is read only in this function
  
  switch(I2C_Symbol) {
      case I2C_Start:  // start bit: communication starting right now
                         I2C_SlaveIO_sm_MakeSlaveIdle(u);
                         return I2C_SlaveIO_sm_StartBitReceived(u);
      case I2C_Stop:     S->BusBusy = 0;return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Error:    return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      case I2C_Timeout:  return I2C_SlaveIO_sm_MakeSlaveIdle(u);
      
      // the first clock edge we expect in receive mode is rising edge, the last clock edge we expect is ack SCL rising edge for transmit, and ack falling edge for receive
      // the bitmask is shifted right on SCL falling edge
      case I2C_SDA1SCLRise:
        if((S->Data & S->Bitmask)==0) // we were outputing 1 and we read 0! bail out
          return I2C_SlaveIO_sm_MakeSlaveIdle(u);
        return thisSx; // continue
      case I2C_SDA0SCLRise:
        return thisSx;// for data bit
      // we shift bitmask on SCL falling edge. no use of the level
      case I2C_SDA0SCLFall:
      case I2C_SDA1SCLFall:
        // output the next data bit
        if(S->Bitmask==1) {
          IO_PinSetHigh(S->SDA);//SetSDAInput(); // release for master to ack or not
          S->Bitmask = 0; // finished
          return thisSx;
        };
        
        if(S->Bitmask==0) {// get ready to receive data bytes now
          S->Bitmask = 0x80; // byte to send next
          S->SubAddress++;
          if(S->SubAddress>=S->MemoryCountof) // address wraps around the memory range
            S->SubAddress = 0; // wraps around
          S->Data = S->Memory[S->SubAddress];
          if(S->Data & S->Bitmask) {IO_PinSetHigh(S->SDA);/*SetSDAInput();*/}else{IO_PinSetLow(S->SDA);/*SetSDAOutput();*/};
          S->MemoryRead = 1; // the 8 data bit were sent proper
          return thisSx; // same thing again with different action
        };
        
        S->Bitmask >>= 1;
        if(S->Data & S->Bitmask) {IO_PinSetHigh(S->SDA);/*SetSDAInput();*/}else{IO_PinSetLow(S->SDA);/*SetSDAOutput();*/};
        return thisSx;
  };

  return thisSx;
}

//================= At the end we have the test function to check functionality
#ifdef ADD_EXAMPLES_TO_PROJECT // example takes RAM in global variables.. disabled by default
static u8 I2C_SlaveAdresses[] = { 0x40, 0xB0, 0xC0, 0xD0 }; // ram is faster
static u8 I2C_SlaveMemory[256];
static u8 RxBuf[10];

// to make the slave not responding, we need to change to add a "slave busy" flag which latches when bus is idle.
static I2C_SlaveIO gI2C_Slave; // can be multiple of them, as array (we pass by pointer)
static IO_Pin_t I2C_SlaveIO_SDA; // for fast pin access using bitbanding area (we will use a fake HW registers which will check if all pins are not colliding)
static IO_Pin_t I2C_SlaveIO_SCL; // for fast pin access using bitbanding area
// we have to cleanup the Architect function for the fPin



void TestI2C_SlaveIO(void) {

  gI2C_Slave.SDA = IO_PinInit(&I2C_SlaveIO_SDA, PH10 ); // Initialize some quick pointers
  gI2C_Slave.SCL = IO_PinInit(&I2C_SlaveIO_SCL, PH12 ); // Initialize some quick pointers
  
  NewI2C_SlaveIO(&gI2C_Slave, (u8*)I2C_SlaveAdresses, countof(I2C_SlaveAdresses), I2C_SlaveMemory, countof(I2C_SlaveMemory));
  SpyI2C_SlaveIO(&gI2C_Slave); // this is to go to spy code as well
//  u32 (*fnBusFree)(u32); // the I2C bus is free (stop bit occured), this can be hooked to run some action (like turn off this slave, do something, turn it back on)
//  u32 (*fnReceived)(u32); // user data has been received (end of slave receive phase). This does not include any sub address
//  u32 (*fnTransmitted)(u32); // data was transmitted out (end of slave transmitter phase), can be hooked to trigger higher level action

  gI2C_Slave.fnBusFree = u32_fn_u32;
  gI2C_Slave.fnReceived = u32_fn_u32;
  gI2C_Slave.fnTransmitted = u32_fn_u32;
  NVIC_EXTIsEnable(ENABLE);
  //===============

  while(1) {

    // here we will use the I2C single master to generate events...
#if 1 // basic slave receive test
    // can put a breakpoint here
    I2CIO_Start(0x40);  // if return FALSE, chip is not present or bus stuck
    I2CIO_Transmit(0x01);
    I2CIO_Transmit(0x12);
    I2CIO_Transmit(0x23);
    I2CIO_Transmit(0x34);
    I2CIO_Stop();
#endif
#if 1
    // can put a breakpoint here
    I2C_SlaveMemory[0] = 0x55;
    I2C_SlaveMemory[1] = 0x66;
    I2C_SlaveMemory[2] = 0x77;
    I2C_SlaveMemory[3] = 0x88;
    I2C_SlaveMemory[4] = 0x99;

    I2CIO_Start(0x40);  // if return FALSE, chip is not present or bus stuck
    I2CIO_Transmit(0x01);
    I2CIO_Start(0x41);
    RxBuf[0] = I2CIO_Receive(1);
    RxBuf[1] = I2CIO_Receive(1);
    RxBuf[2] = I2CIO_Receive(0);
#endif
  }

};
#endif // I2C Slave IO example



