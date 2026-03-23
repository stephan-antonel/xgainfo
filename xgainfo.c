#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <conio.h>
#include <string.h>

#define VERSION "1.1"

#define IOREG_OPMODE	0x00
#define IOREG_APCTL	0x01
#define IOREG_INTEN	0x04
#define IOREG_APBANK 0x08
#define IOREG_INDEX	0x0A
#define IOREG_DATA	0x0B

#define APERTURE_64K	0xA000  //Should this be (unsigned char* far)0xA0000000L?

#define SHOW_POS	 	1
#define SHOW_IO	 	1 << 1
#define SHOW_INDEX 	1 << 2
#define SHOW_MEM	 	1 << 3
#define SHOW_MONITOR 1 << 4

FILE *fp = stdout;

typedef unsigned char uint8_t;
typedef unsigned int	 uint16_t;
typedef unsigned long int uint32_t;

typedef unsigned char bool_t;

uint16_t temp;	//Holding register for register value
char str[9];		//Temp string
uint8_t show = 0;	//Flags for which data to show
uint8_t inst = 0; //Which instance to show
bool_t pageBreaks;//True if -p specified

//We need Int 15h to read the POS registers but
//non-MCA systems usually don't have it, so check first.
//Returns 1 if Int 15h exists, 0 if not.
bool_t CheckInterrupt(void)
{
	//Why not just use the C functions?
	//I'm trying to learn x86 assembly, don't judge me.
	asm mov al, 0x15;
	asm mov ah, 0x35;
	asm int 0x21;      //Get Int 15h address from IVT
	asm mov ax, es;
	asm or ax, bx;
	asm jz notfound; 	 //If it's 0, there's no interrupt there.
	return 1;

	notfound:
	return 0;
}

//Enable a MCA for setup (i.e, pull SETUP# low) to
//allow access to POS registers. This is where we use Int 15h.
void SetupSlot(uint8_t slot, bool_t enabled)
{
	if(slot > 7)
		return;

	asm mov bh, 0x00;
	asm mov bl, slot;    //Pass slot # in bx
	asm mov ax, 0xC401;  //0xC401 = enable
	asm mov dh, 0x00;
	asm mov dl, enabled;
	asm or  dl, 0x00;
	asm jnz go;
	asm inc ax;          //0xC402 = disable
	asm jmp go;
	go:						//Go!
	asm int 0x15;			//Do the thing
}

//The planar enables setup a different way,
//so we have a different function for it.
void SetupPlanar(bool_t enable)
{
	asm mov ah, 0x00;
	asm mov al, enable;
	asm or ax, 0x00;
	asm jz disable;
	asm cli;
	asm mov dx, 0x94;
	asm in ax, dx;			//Get value from IO port 94h
	asm mov temp, ax;		//Save it for later
	asm and ax, 0xDF;		//Clear bit 5
	asm or ax, 0x80;		//Set bit 7
	asm out dx, ax;		//Write the modified value back
	asm sti;
	return;
	disable:
	asm cli;
	asm mov ax, temp;		//Restore the original value
	asm mov dx, 0x94;
	asm out dx, ax;		//Write it to port 94h
	asm sti;
}


//Some PS/2s have onboard XGA on the planar
//Returns the MCA card ID if XGA found, 0 if not.
uint16_t CheckPlanar(void)
{
	uint16_t posid = 0;

	SetupPlanar(1);
	asm cli;
	asm mov dx, 0x100;//Read the MCA card ID
	asm in ax, dx;
	asm cmp ah, 0x8F;	//Is the high byte 8Fh?
	asm jne notfound; //If not, not an XGA
	asm cmp al, 0xDB; //Is it an O.G. XGA?
	asm je found;
	asm cmp al, 0xDA; //Or a XGA-NI (XGA2)
	asm je found;

	notfound:
	asm sti;
	SetupPlanar(0);
	return 0;

	found:
	asm sti;
	asm mov posid, ax;
	SetupPlanar(0);
	return posid;
}

//Check if there's an XGA in a certain slot
//Returns the MCA card ID if the slot has a XGA card in it
//Returns 0 otherwise
uint16_t CheckSlot(uint8_t slot)
{
	uint16_t posid = 0;

	if(slot > 7)
		return 0;

	SetupSlot(slot, 1);	//Enable POS registers on card (#SETUP)
	asm mov dx, 0x100;
	asm in ax, dx;			//Get the adapter ID
	asm cmp ah, 0x8F;		//Is the high byte 8Fh?
	asm jne notfound;		//If not, not an XGA
	asm cmp al, 0xDB;		//Check the low byte. DBh = XGA
	asm je found;
	asm cmp al, 0xDA;		//DAh = XGA2
	asm je found;

	notfound:
	SetupSlot(slot, 0);	//Disable the POS registers when done
	return 0;

	found:
	asm mov posid, ax;
	SetupSlot(slot, 0);
	return posid;
}

//Extended printf. Allows features like output to file, page breaks
void XPrintf(const char *format, ...)
{
	static uint8_t lines = 0;
	va_list args;

	va_start(args, format);
	vfprintf(fp, format, args);
	va_end(args);

	if(++lines > 23 && pageBreaks)
	{
		printf("Press a key to continue..\n");
		getch();
		lines = 0;
	}

}

//Read one of the XGA's IO registers
uint8_t XgaReadIO(uint16_t base, uint8_t reg)
{
	uint8_t value;

	asm mov dx, base;
	asm add dl, reg;
	asm in ax, dx;
	asm mov value, al;
	return value;
}

//Write to one of the XGA's IO registers
void XgaWriteIO(uint16_t base, uint8_t reg, uint8_t value)
{
	asm mov dx, base;
	asm add dl, reg;
	asm mov al, value;
	asm out dx, al;
}

//Shows a binary representation of the byte passed in
char *BinByte(uint8_t b)
{
	int i;
	//static char str[9];

	for(i = 0; i < 8; i++)
	{
		str[7 - i] = (b & 0x01) ? '1' : '0';
		b >>= 1;
	}

	str[8] = 0; //Just in case
	return str;
}

//Returns the size of XGA VRAM in kb
//Requires enabling XGA extended mode
uint16_t XgaMemSize(uint16_t ioRegBase)
{
	uint16_t memSize = 0;

	asm mov al, 0x00; 	  //Disable XGA interrupts
	asm mov dx, ioRegBase;
	asm add dx, IOREG_INTEN;
	asm out dx, al;
	asm mov al, 0x04;		  //Enable XGA extended mode
	asm mov dx, ioRegBase;
	asm out dx, al;
	asm xor ah, ah;		  //Blank the screen
	asm mov al, 0x64;
	asm add dx, IOREG_INDEX;
	asm out dx, ax;
	asm mov al, 0x01;		 //Enable the 64k aperture
	asm mov dx, ioRegBase;
	asm add dx, IOREG_APCTL;
	asm out dx, al;
	asm mov ax, APERTURE_64K;	//Load the aperture address into es
	asm mov es, ax;
	asm mov ax, 0x03;			//Search pages 3 - 8 to tally up the RAM
	asm mov cx, 0x08;
	asm push cx;

	pageloop:
	asm mov dx, ioRegBase;
	asm add dx, IOREG_APBANK;
	asm out dx, al;
	asm add al, 0x04;
	asm mov bh, byte ptr es:[0xFFFE];
	asm mov byte ptr es:[0xFFFE], 0xA5;
	asm mov byte ptr es:[0x00], 0x00;
	asm cmp byte ptr es:[0xFFFE], 0xA5;
	asm jne endcount;
	asm mov byte ptr es:[0xFFFE], 0xA5;	//No idea why we have to do this twice...
	asm mov byte ptr es:[0x00], 0x00;
	asm cmp byte ptr es:[0xFFFE], 0xA5;
	asm jne endcount;
	asm mov byte ptr es:[0xFFFE], bh;
	asm loop pageloop;

	endcount:
	asm pop ax;
	asm sub ax, cx;
	asm mov memSize, ax;

	asm mov dx, ioRegBase;	//Disable XGA extended mode
	asm mov al, 0x00;
	asm out dx, al;

	return (memSize + 4) * 64;
}

void XgaMonitorInfo(void)
{
	uint8_t dispCtl;
	uint8_t monitorId;

	asm mov dx, IOREG_INDEX;
	asm mov ax, 0x0052;
	asm out dx, ax;
	asm inc dx;
	asm in al, dx;
	asm mov dispCtl, al;
	asm and al, 0x0F;
	asm mov monitorId, al;

	XPrintf("\nMonitor Info\n");
	XPrintf("============\n");
	XPrintf("Monitor ID:           %02Xh %s\n", monitorId, BinByte(monitorId));

	switch(monitorId)
	{
		case 0x0F: XPrintf("No monitor detected. \n"); break;
		case 0x0E: XPrintf("Monitor type:         IBM 8512/8513\n"); break;
		case 0x0D: XPrintf("Monitor type:         IBM 8503\n"); break;
		case 0x0C: XPrintf("Monitor type:         IBM 8515\n"); break;
		case 0x0B: XPrintf("Monitor type:         IBM 8514\n"); break;
		case 0x09: XPrintf("Monitor type:         IBM 8507/8604\n"); break;
		default:   XPrintf("Monitor type:         Unidentified\n"); break;
	}

	XPrintf("\n");
}

//Does what it says: shows information about the XGA card
void XgaInfo(uint8_t slot)
{
	int i;
	uint8_t pos[6];
	uint8_t instance;
	uint8_t arb;
	uint8_t reg;
	uint8_t mem;
	uint16_t cardId;
	uint16_t ioRegBase;
	uint32_t memRegBase;
	uint32_t romBase;
	uint32_t aperture[3];

	if(slot > 8)
		return;	//Invalid slot #

	if(slot == 0)
		SetupPlanar(1);
	else
		SetupSlot(slot, 1);

	//I did it in C because I just can't wrap my head
	//around memory addressing in assembly
	for(i = 0; i < 6; i++)
		pos[i] = inp(0x100 + i);

	cardId = (pos[1] << 8) | pos[0];
	instance = (pos[2] & 0x0F) >> 1;
	ioRegBase = 0x2100 | (instance << 4);
	romBase = (pos[2] & 0xF0) >> 4;
	romBase *= 0x2000;
	romBase += 0xC0000;
	memRegBase = romBase + 0x1C00 + (instance * 128);
	arb = pos[3] >> 3;
	i = XgaReadIO(ioRegBase, 1) & 0x03;
	aperture[1] = (pos[5] & 0x0F);
	aperture[1] <<= 20;
	aperture[2] = (pos[4] & 0x0E) << 8;
	aperture[2] |= instance << 6;
	aperture[2] <<= 16;

	switch(i)
	{
		case 1:  aperture[0] = 0xA0000; break;
		case 2:  aperture[0] = 0xB0000; break;
		default: aperture[0] = 0; break;
	}

	if(slot == 0)
		SetupPlanar(0);
	else
		SetupSlot(slot, 0);

	XPrintf("Type:                 %s (%04Xh)\n", (cardId == 0x8FDA) ? "XGA2" : "XGA", cardId);
	XPrintf("Instance #:           %d (card enable %s)\n", instance, (pos[2] & 0x01) ? "on" : "off");
	XPrintf("ROM Base:             %lXh\n", romBase);
	XPrintf("IO Register Base:     %04Xh\n", ioRegBase);
	XPrintf("Memory Register Base: %lXh\n", memRegBase);
	XPrintf("Arbitration level:    %d (fairness %s)\n", arb, (pos[2] & 0x04) ? "on" : "off");
	XPrintf("64K aperture:         %lXh %s\n", aperture[0], (aperture[0] == 0) ? "(disabled)" : "");
	XPrintf("1MB aperture:         %lXh %s\n", aperture[1], (aperture[1] == 0) ? "(disabled)" : "");
	XPrintf("4MB aperture:         %lXh %s\n" , aperture[2], (aperture[2] == 0) ? "(disabled)" : "");

	if(show & SHOW_MEM)
		XPrintf("Memory:               %dKB\n", XgaMemSize(ioRegBase));

	if(show & SHOW_MONITOR)
		XgaMonitorInfo();

	if(show & SHOW_POS)
	{
		XPrintf("\n");

		for(i = 0; i < 6; i++)
			XPrintf("POS %02Xh:   %02Xh   %s\n", 0x100 + i, pos[i], BinByte(pos[i]));
	}

	if(show & SHOW_IO)
	{
		XPrintf("\n");

		for(i = 0; i < 0x0F; i++)
		{
			reg = XgaReadIO(ioRegBase, i);
			XPrintf("IO  %02Xh:  %02Xh   %s\n", ioRegBase + i, reg, BinByte(reg));
		}
	}

	if(show & SHOW_INDEX)
	{
		XPrintf("\n");

		for(i = 0; i <= 0x70; i++)
		{
			XgaWriteIO(ioRegBase, 0x0A, i);
			reg = XgaReadIO(ioRegBase, 0x0B);
			XPrintf("IDX %02Xh:    %02Xh   %s\n", i, reg, BinByte(reg));
		}
	}

	XPrintf("\n");
}

//Search the command line for a specific parameter (argument)
//Returns the index into the list if it is found, -1 otherwise
int CheckParam(int argc, char **argv, const char *check)
{
	int i;

	for(i = 0; i < argc; i++)
	{
		if(strcmp(argv[i], check) == 0)
			return i;
	}

	return -1;
}

//Used by the -s argument. Sets the corresponding flag if found.
void CheckShow(char *str, char opt, uint8_t flag)
{
	char *ptr = str;

	while(*ptr != 0)
	{
		if(*ptr++ == opt)
		{
			show |= flag;
			return;
		}
	}
}

//main is main
int main(int argc, char **argv)
{
	int i, pos;

	printf("XGAINFO by Stephan Antonel - Version %s 2026\n", VERSION);
	printf("Run \"XGAINFO -h\" for usage\n\n");

	if(CheckParam(argc, argv, "-h") >= 0)
	{
		printf("Argument         Description\n");
		printf("--------         -----------\n");
		printf("-h               Show this help message\n");
		printf("-f <file>        Output to a file instead of stdout\n");
		printf("-a               Show everything (supersedes -s)\n");
		printf("-p               Pause every 25 lines\n");
		printf("-s <params>      Show various other information\n");
		printf("                 p = show POS registers\n");
		printf("                 i = show IO registers\n");
		printf("                 x = show indexed registers\n\n");
		printf("                 m = show memory size\n");
		printf("                 d = show monitor information\n");

		return 0;
	}

	if((pos = CheckParam(argc, argv, "-f")) >= 0)
	{
		if(pos + 1 == argc)
		{
			printf("No file name specified.\n");
			return 1;
		}

		if((fp = fopen(argv[pos + 1], "w")) == NULL)
		{
			printf("Unable to open file %s for writing. \n");
			return 1;
		}
	}

	pageBreaks = (CheckParam(argc, argv, "-p") >= 0);

	if(CheckParam(argc, argv, "-a") >= 0)
		show = 0xFF;

	if((pos = CheckParam(argc, argv, "-s")) >= 0)
	{
		if(++pos < argc)
		{
			CheckShow(argv[pos], 'p', SHOW_POS);
			CheckShow(argv[pos], 'i', SHOW_IO);
			CheckShow(argv[pos], 'x', SHOW_INDEX);
			CheckShow(argv[pos], 'm', SHOW_MEM);
			CheckShow(argv[pos], 'd', SHOW_MONITOR);
		}
	}

	if(!CheckInterrupt())
	{
		printf("Error: Not an MCA system \ Int 15h not available.\n");
		return 1;
	}

	if(CheckPlanar())
	{
		XPrintf("XGA found on planar\n");
		XPrintf("----------------------------------------------------\n");
		XgaInfo(0);
	}

	for(i = 1; i < 8; i++)
	{
		if(CheckSlot(i))
		{
			XPrintf("XGA found in slot %d\n", i);
			XPrintf("--------------------------------------------------\n");
			XgaInfo(i);
		}
	}

	if(fp != stdout)
		fclose(fp);

	return 0;
}