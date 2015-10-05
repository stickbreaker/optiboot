/* optiBoot modification for self re-programming 

Chuck Todd <ctodd@cableone.net> 04OCT2015

Origional Source From majekw/supermaster https://github.com/majekw/optiboot/tree/supermaster

Intel Hex format description from Nick Gammon http://www.gammon.com.au/uploader


These changes are to support self re-programing using and external SPI Flash for 
program Storage.  The storage is a modified Intel Hex file, Converted from Human readable
to pure binary, except the ':' is retained as a line start maker.

A WinBond W25Q64F 8MB SPI Flash is connected thru Level shifters to the SPI bus and D53 for CS, 
*/

/*** From optiboot.h *****************/
#ifdef DOSELF
// 'typedef' (in following line) and 'const' (few lines below) are a way to define external function at some arbitrary address
typedef void (*do_self_t)(uint32_t address);

typedef uint32_t optiboot_addr_t;
const do_self_t do_self = (do_self_t)((FLASHEND-1023+2)>>1);  // might have to change the BootBlock size to 2k
#endif

/************** From optiboot.c ******************/

/* everything that needs to run VERY early */
void pre_main(void) {
  // Allow convenient way of calling do_spm function - jump table,
  //   so entry to this function will always be here, indepedent of compilation,
  //   features etc
  asm volatile (
    "	rjmp	1f\n"
    "	rjmp	do_spm\n"
#ifdef DOSELF
    " rjmp  do_self\n"
#endif
    "1:\n"
  );
}

/*********** Added end of to optiboot.c *******************/

#if defined(DOSELF)
// would a static prefix help/be needed?
uint8_t do_SPI(uint8_t in){ //quick and dirty SPI.transfer()  
SPDR = in;
while(!(SPSR & 1<<SPIF));
return SPDR;
}

// would a static prefix help/be needed?
void do_RL(uint32 * adr, uint8_t * ch){
 /* Read from SPI Flash W25QF64
 *  adr points to ':' of converted Intel Hex file line, the flash is loaded with Binary values
 *  ch becomes complete line, including checksum, checksum is not counted in L
 * : L AA T d*L cksm
 * ':'  - Line start sync, discarded
 * L    - length of data section (bytes)
 * AA   - Two byte address 
 * d*L  - 'L' bytes of data
 * cksum- checksum of all bytes (recalculated, not HID version)
 */
 
PORTB = PORTB &0xFE; // CS low  my SPI Flash is on D53, MEGA2560
do_SPI(0x03); // read command
do_SPI((uint8_t)(*adr>>16)&0xff); // A23..16
do_SPI((uint8_t)(*adr>>8)&0xFF);  // A15..8
do_SPI((uint8_t)(*adr)&0xff);       // A7..0
do_SPI(0); // ':' skip, should I Verify?
uint8_t j=0;  // index for buffer
ch[j++]=do_SPI(0); // length of current data field in line 
uint8_t i = ch[j-1]+4; // len byte + (2)addr + (1)type +(1)cksm
while(i>0){
  ch[j++] = do_SPI(0);
  i--;
  }
*adr = *adr + j + 2; // ':' + len, update address for source in SPI Flash
PORTB= PORTB | 0x01; // CS HIGH, Done with current line
}
// what does the static prefix do?  I just copied it from do_spm()
static void do_self(uint32_t adr){
/* cs pin for SPI slave, Is already Output and High.
 * SPI must be inited correctly prior to call. 
 * adr is image location inside the SPI Flash.
 * image must be a converted format of an Intel hex file.
 * interrupts off
 * watchdog off
 *
*/
uint32_t objAdr = 0,extendA=0, oldPage=0,mask=~(SPM_PAGESIZE-1);
uint8_t * ch[40]; // should actually never need more than 21 bytes 1+2+1+16+1
bool dirty=false; // flush indicator
RAMPZ=0; // shouldn't actually need this, but maybe?
uint8_t x; // index variable to walk thru line buffer
uint16_t w; // word to fill Flash buffer
do{
  do_RL(&adr,&ch); // read from SPI Flash, adr is updated byte call  
  switch(ch[3]){ // Intel record type
    case 0: // data record  L A A T (d*L) cksum
      objAdr = ch[2];                   // parse off address offset
      objAdr = (objAdr << 8) | ch[1];     // can address ever be ODD?
      x=4;                            // index were data starts
      while(ch[0]>0){
        if(oldPage!=((objAdr+extendA)&mask)){// write the page
          if(dirty){ // this first write address was not in page 0
            do_spm((uint16_t)(oldPage&0xffff),__BOOT_PAGE_ERASE,0);
            do_spm((uint16_t)(oldPage&0xffff),__BOOT_PAGE_WRITE,0);
            }
          oldPage = (objAdr+entendA)&mask;
          RAMPZ =(oldPage>>16)&0xff; // set Extended address for Fill operations, and next erase/write
          }
        w = ch[x++];   // fill next word
        w = w | (ch[x++]<<8);
        ch[0] -= 2;  // count down for each byte processed,  CAN LENGTH ever be ODD?
        do_spm((uint16_t)(objAdr&0xFFFF),_BOOT_PAGE_FILL,w);
        objAdr += 2;
        dirty=true;  // for Page_write
        }
      break;
    case 1: // end of file
      if(dirty){
        do_spm(oldPage&0xffff),__BOOT_PAGE_ERASE,0);
        do_spm(oldPage&0xffff),__BOOT_PAGE_WRITE,0);
       }
      appStart(1); // simulate Power-on Reset, do I need to worry about SP and other H/W init Status?
      break;
    case 2: // extended segment address L A A T d d cksum
      extendA = ch[4]<<12;
      break;
    default :;
    }// switch
  }while(1);

}
#endif
