#include "HardwareProfile.h"

#if defined(GFX_USE_DISPLAY_CONTROLLER_MI043_LQ043)

#include "Compiler.h"
#include "TimeDelay.h"
#include "Graphics/DisplayDriver.h"
#include "Graphics/Primitive.h"
#include "Graphics/MI043_LQ043.h"

// Color
GFX_COLOR   _color;
#ifdef USE_TRANSPARENT_COLOR
GFX_COLOR   _colorTransparent;
SHORT       _colorTransparentEnable;
#endif
// Clipping region control
SHORT _clipRgn;
// Clipping region borders
SHORT _clipLeft;
SHORT _clipTop;
SHORT _clipRight;
SHORT _clipBottom;

DWORD _address;

#define RED8(color16)   (BYTE) ((color16 & 0xF800) >> 11)
#define GREEN8(color16) (BYTE) ((color16 & 0x07E0) >> 5)
#define BLUE8(color16)  (BYTE) ((color16 & 0x001F))

/////////////////////// LOCAL FUNCTIONS PROTOTYPES ////////////////////////////
void SetReg(WORD index, BYTE value);
BYTE GetReg(WORD index);

void PutImage1BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch);
void PutImage4BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch);
void PutImage8BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch);
void PutImage16BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch);

void PutImage1BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch);
void PutImage4BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch);
void PutImage8BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch);
void PutImage16BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch);
#define PMPWaitBusy()  while(PMMODEbits.BUSY);

void WriteData(WORD value){

    if(((DWORD_VAL)_address).w[1]&0x0001)
        A17_LAT_BIT = 1;
    else
    	A17_LAT_BIT = 0;                                  // set address[17]

    PMADDR = ((DWORD_VAL)_address).w[0];              // set address[16:1]
	_address++;
    
    PMDIN = value; 
    PMPWaitBusy();
}

void  SetReg(WORD index, BYTE value){
	
	RS_LAT_BIT = 0;        // set RS line to low for register space access
    CS_LAT_BIT = 0;        // enable SSD1926 

    A17_LAT_BIT = 0;            // set address[17]
    PMADDR = index>>1;          // set address[16:1]
    A0_LAT_BIT = index&0x0001;  // set address[0]
	
	if(A0_LAT_BIT)
	{
		WE1_LAT_BIT = 0;
		PMDIN = value<<8;
	}
	else
	{
		WE1_LAT_BIT = 1;
		PMDIN = value;
	}
    PMPWaitBusy();            // wait for the transmission end
    
    CS_LAT_BIT = 1;        // disable SSD1926
    RS_LAT_BIT = 1;
	WE1_LAT_BIT = 0;	// prepare to write data WORD to memory
	A0_LAT_BIT = 0;
}

BYTE  GetReg(WORD index){
	BYTE value;

    RS_LAT_BIT = 0;        // set RS line to low for register space access
    CS_LAT_BIT = 0;        // enable SSD1926 

    A17_LAT_BIT = 0;            // set address[17]    
    PMADDR = index>>1;          // set address[16:1]
    A0_LAT_BIT = index&0x0001;  // set address[0]
	if(A0_LAT_BIT)
	{
		WE1_LAT_BIT = 0;
		value = PMDIN>>8;        // start transmission, read dummy value
	}
	else
	{
		WE1_LAT_BIT = 1;
		value = PMDIN;        // start transmission, read dummy value
	}

    PMPWaitBusy();         // wait for the transmission end

    CS_LAT_BIT = 1;        // disable SSD1926
    RS_LAT_BIT = 1;

    PMCONbits.PMPEN  = 0;  // suspend PMP
    if(A0_LAT_BIT)
	{
		value = PMDIN>>8;        // start transmission, read dummy value
	}
	else
	{
		value = PMDIN;        // start transmission, read dummy value
	}
    PMCONbits.PMPEN  = 1;  // resume PMP
	
	WE1_LAT_BIT = 0;
	A0_LAT_BIT = 0;
    
    return value;
}

void ResetDevice(void){

    RST_LAT_BIT = 0;       // hold in reset by default
    RST_TRIS_BIT = 0;      // enable RESET line

    A0_TRIS_BIT = 0;       // enable A0  line for byte access
    A17_TRIS_BIT = 0;      // enable A17 line

    RS_TRIS_BIT = 0;       // enable RS line 

    CS_LAT_BIT = 1;        // SSD1926 is not selected by default
    CS_TRIS_BIT = 0;       // enable 1926 CS line    

	WE1_TRIS_BIT = 0;
    
    // PMP setup 
    PMMODE = 0; PMAEN = 0; PMCON = 0;
    PMMODEbits.MODE   = 2;  // Intel 80 master interface
    PMMODEbits.WAITB  = 2;	// 1
    PMMODEbits.WAITM  = 9;	// 4
    PMMODEbits.WAITE  = 2;	// 1
	PMMODEbits.INCM   = 0;

    PMMODEbits.MODE16 = 1;  // 16 bit mode
		
	PMAENbits.PTEN0 = 1;   // enable low address latch
    PMAENbits.PTEN1 = 1;   // enable high address latch

    PMCONbits.ADRMUX = 3;   // address is multiplexed on data bus
    PMCONbits.CSF    = 0;
    PMCONbits.ALP    = 1;  // set address latch control polarity 
    PMCONbits.PTRDEN = 1;  // enable RD line
    PMCONbits.PTWREN = 1;  // enable WR line
    PMCONbits.PMPEN  = 1;  // enable PMP

    DelayMs(40);
    RST_LAT_BIT = 1;       // release from reset
    DelayMs(400);

/////////////////////////////////////////////////////////////////////
// PLL SETUP
// Crystal frequency x M / N = 80 MHz 
// for 4 MHz crystal:
/////////////////////////////////////////////////////////////////////
    SetReg(REG_PLL_CONFIG_0, 0x0a); // set N = 10
    SetReg(REG_PLL_CONFIG_1, 0xc8); // set M = 200
    SetReg(REG_PLL_CONFIG_2, 0xae); // must be programmed to 0xAE   
    SetReg(REG_PLL_CONFIG_0, 0x8a); // enable PLL

    SetReg(REG_MEMCLK_CONFIG, 0x00); // set MCLK = 0 (80 MHz)

    SetReg(REG_PCLK_FREQ_RATIO_0, 0xcb);
    SetReg(REG_PCLK_FREQ_RATIO_1, 0xcc);
    SetReg(REG_PCLK_FREQ_RATIO_2, 0x01);

	// setup panel type
	SetReg(REG_PANEL_TYPE, 0x71);			// color LCD panel is selected, TFT
	SetReg(REG_MOD_RATE, 0x00);
	SetReg(REG_HORIZ_TOTAL_0, 65);			// HT = (65*8)+4+1 = 525
	SetReg(REG_HORIZ_TOTAL_1, 4);
	SetReg(REG_HDP, 59);					// HDP = (59+1)*8 = 480
	SetReg(REG_HDP_START_POS0, 0x7);
	SetReg(REG_HDP_START_POS1, 0);
	SetReg(REG_VERT_TOTAL0, 29);			// VT = (1*256)+29+1 = 286
	SetReg(REG_VERT_TOTAL1, 1);
	SetReg(REG_VDP0, 15);					// VDP = (1*256)+15+1 = 272
	SetReg(REG_VDP1, 1);
	SetReg(REG_VDP_START_POS0, 0x03);
	SetReg(REG_VDP_START_POS1, 0x00);
	// Panel Configuration Registers
	SetReg(REG_HSYNC_PULSE_WIDTH, 0x04);
	SetReg(REG_LLINE_PULSE_START_SUBPIXEL_POS, 0x00);
	SetReg(REG_HSYNC_PULSE_START_POS0, 0x00);
	SetReg(REG_HSYNC_PULSE_START_POS1, 0x00);
	SetReg(REG_VSYNC_PULSE_WIDTH, 0x00);
	SetReg(REG_VSYNC_PULSE_START_POS0, 0x00);
	SetReg(REG_VSYNC_PULSE_START_POS1, 0x00);
	// Display mode register
	SetReg(REG_DISPLAY_MODE, 0x84);
	// Main window register
	SetReg(REG_MAIN_WIN_DISP_START_ADDR0, 0x00);
	SetReg(REG_MAIN_WIN_DISP_START_ADDR1, 0x00);
	SetReg(REG_MAIN_WIN_DISP_START_ADDR2, 0x00);
	SetReg(REG_MAIN_WIN_ADDR_OFFSET0, 240);	// MWLO = HDP/(32/bpp) = 480/(32/16) = 240
	SetReg(REG_MAIN_WIN_ADDR_OFFSET1, 0x00);
	SetReg(REG_RGB_SETTING, 0xc0);				// Using RGB for Floating and Main window
	//
	SetReg(REG_POWER_SAVE_CONFIG, 0x00);				// Turn off power save mode
	// // 2D Engine Registers
	SetReg(REG_FLOAT_WIN_DISP_START_ADDR0, 0x00);
	SetReg(REG_FLOAT_WIN_DISP_START_ADDR1, 0x00);
	SetReg(REG_FLOAT_WIN_DISP_START_ADDR2, 0x00);
	SetReg(REG_FLOAT_WIN_ADDR_OFFSET0, 120);
	SetReg(REG_FLOAT_WIN_ADDR_OFFSET1, 0x00);
	SetReg(REG_FLOAT_WIN_X_START_POS0, 0x00);
	SetReg(REG_FLOAT_WIN_X_START_POS1, 0x00);
	SetReg(REG_FLOAT_WIN_Y_START_POS0, 0x00);
	SetReg(REG_FLOAT_WIN_Y_START_POS1, 0x00);
	SetReg(REG_FLOAT_WIN_X_END_POS0, 0x77);
	SetReg(REG_FLOAT_WIN_X_END_POS1, 0x00);
	SetReg(REG_FLOAT_WIN_Y_END_POS0, 0x0f);
	SetReg(REG_FLOAT_WIN_Y_END_POS1, 0x01);
	SetReg(REG_SPECIAL_EFFECTS, 0x00);
		
	SetReg(REG_GPIO_CONFIG0, 0x1f);				// Set GPIO to output
	SetReg(REG_GPIO_CONFIG1, 0x00);
	SetReg(REG_GPIO_STATUS_CONTROL1, 0x80);		// Set LPOWER pin to High for turn on LCD
		
	DelayMs(500);
	
	while (!(GetReg(REG_POWER_SAVE_CONFIG) & 0x80));	// waiting for SSD1926 vertical sync signal exists
		
    SetReg(REG_DISPLAY_MODE,0x04);  // 16 BPP, enable RAM content to screen
	SetReg(REG_GPIO_STATUS_CONTROL0, 0x01);			// turn on backlight
}

/*********************************************************************
* Function: IsDeviceBusy()
*
* Overview: Returns non-zero if LCD controller is busy 
*           (previous drawing operation is not completed).
*
* PreCondition: none
*
* Input: none
*
* Output: Busy status.
*
* Side Effects: none
*
********************************************************************/
WORD IsDeviceBusy(void)
{  
    return ((GetReg(REG_2D_220) & 0x01) == 0);
}

/*********************************************************************
* Function: SetClipRgn(left, top, right, bottom)
*
* Overview: Sets clipping region.
*
* PreCondition: none
*
* Input: left - Defines the left clipping region border.
*		 top - Defines the top clipping region border.
*		 right - Defines the right clipping region border.
*	     bottom - Defines the bottom clipping region border.
*
* Output: none
*
* Side Effects: none
*
********************************************************************/
void SetClipRgn(SHORT left, SHORT top, SHORT right, SHORT bottom)
{
    _clipLeft=left;
    _clipTop=top;
    _clipRight=right;
    _clipBottom=bottom;

}

/*********************************************************************
* Function: SetClip(control)
*
* Overview: Enables/disables clipping.
*
* PreCondition: none
*
* Input: control - Enables or disables the clipping.
*			- 0: Disable clipping
*			- 1: Enable clipping
*
* Output: none
*
* Side Effects: none
*
********************************************************************/
void SetClip(BYTE control)
{
    _clipRgn=control;
}

/*********************************************************************
* Function: void PutPixel(SHORT x, SHORT y)
*
* PreCondition: none
*
* Input: x,y - pixel coordinates
*
* Output: none
*
* Side Effects: none
*
* Overview: puts pixel
*
* Note: none
*
********************************************************************/
void PutPixel(SHORT x, SHORT y){
DWORD address;

    if(_clipRgn){
        if(x<_clipLeft)
            return;
        if(x>_clipRight)
            return;
        if(y<_clipTop)
            return;
        if(y>_clipBottom)
            return;
    }
	
    address = (DWORD)LINE_MEM_PITCH*y + x;
	
    SetAddress(address);
    CS_LAT_BIT = 0;
    WriteData(_color);
    CS_LAT_BIT = 1;
}

/*********************************************************************
* Function: WORD GetPixel(SHORT x, SHORT y)
*
* PreCondition: none
*
* Input: x,y - pixel coordinates 
*
* Output: pixel color
*
* Side Effects: none
*
* Overview: returns pixel color at x,y position
*
* Note: none
*
********************************************************************/
WORD GetPixel(SHORT x, SHORT y){
DWORD address;
WORD  value;

    address = (long)LINE_MEM_PITCH*x + y;
    SetAddress(address);

    CS_LAT_BIT = 0;        // enable SSD1926

    A17_LAT_BIT = 0;                                  // set address[17]    
    if(((DWORD_VAL)_address).w[1]&0x0001)
        A17_LAT_BIT = 1;
    PMADDR = ((DWORD_VAL)_address).w[0];              // set address[16:1]
    A0_LAT_BIT = 1;                                   // set address[0]
    ((WORD_VAL)value).v[0] = PMDIN;  // start transmission, read dummy value
    PMPWaitBusy();         // wait for the transmission end
    A0_LAT_BIT = 0;                                   // set address[0]
    ((WORD_VAL)value).v[0] = PMDIN;  // start transmission, read low byte           

    PMPWaitBusy();         // wait for the transmission end
    CS_LAT_BIT = 1;        // disable SSD1926

    PMCONbits.PMPEN  = 0;  // suspend PMP

    ((WORD_VAL)value).v[0] = PMDIN;  // read high byte           

    PMCONbits.PMPEN  = 1;  // resume PMP

    return value;
}

#ifdef USE_DRV_LINE
/*********************************************************************
* Function: WORD Line2D(SHORT x1, SHORT y1, SHORT x2, SHORT y2)
*
* PreCondition: none
*
* Input: x1,y1 - starting coordinates, x2,y2 - ending coordinates
*
* Output: For NON-Blocking configuration:
*         - Returns 0 when device is busy and the shape is not yet completely drawn.
*         - Returns 1 when the shape is completely drawn.
*         For Blocking configuration:
*         - Always return 1.
*
* Side Effects: none
*
* Overview: draws solid line
*
* Note: none
*
********************************************************************/
static WORD Line2D(SHORT x1, SHORT y1, SHORT x2, SHORT y2)
{
#ifndef USE_NONBLOCKING_CONFIG
    while(IsDeviceBusy() != 0);

    /* Ready */
#else
    if(IsDeviceBusy() != 0)
        return (0);
#endif

    /* Line Boundaries */
    SetReg(REG_2D_1e4, x1 & 0xFF);
    SetReg(REG_2D_1e5, (x1 >> 8) & 0xFF);
    SetReg(REG_2D_1e8, y1 & 0xFF);
    SetReg(REG_2D_1e9, (y1 >> 8) & 0xFF);
    SetReg(REG_2D_1ec, x2 & 0xFF);
    SetReg(REG_2D_1ed, (x2 >> 8) & 0xFF);
    SetReg(REG_2D_1f0, y2 & 0xFF);
    SetReg(REG_2D_1f1, (y2 >> 8) & 0xFF);

    /* Source & Destination Window Start Addresses */
    SetReg(REG_2D_1d4, 0);
    SetReg(REG_2D_1d5, 0);
    SetReg(REG_2D_1d6, 0);
    SetReg(REG_2D_1f4, 0);
    SetReg(REG_2D_1f5, 0);
    SetReg(REG_2D_1f6, 0);

    /* Display width */
    SetReg(REG_2D_1f8, (GetMaxX() + 1) & 0xFF);
    SetReg(REG_2D_1f9, ((GetMaxX() + 1) >> 8) & 0xFF);

    /* Display 2d width */
    SetReg(REG_2D_1d8, (GetMaxX() + 1) & 0xFF);
    SetReg(REG_2D_1d9, ((GetMaxX() + 1) >> 8) & 0xFF);

    /* Set Color */
    SetReg(REG_2D_1fe, BLUE8(_color));
    SetReg(REG_2D_1fd, RED8(_color));
    SetReg(REG_2D_1fc, GREEN8(_color));

    /* 16bpp */
    SetReg(REG_2D_1dd, 0x00);

    /* Line command */
    SetReg(REG_2D_1d1, 0x01);

    /* Draw2d command */
    SetReg(REG_2D_1d2, 0x01);

#ifndef USE_NONBLOCKING_CONFIG
    while(IsDeviceBusy() != 0);

    /* Ready */
#endif
    return (1);
}

/*********************************************************************
* Function: WORD Line(SHORT x1, SHORT y1, SHORT x2, SHORT y2)
*
* PreCondition: none
*
* Input: x1,y1 - starting coordinates, x2,y2 - ending coordinates
*
* Output: For NON-Blocking configuration:
*         - Returns 0 when device is busy and the shape is not yet completely drawn.
*         - Returns 1 when the shape is completely drawn.
*         For Blocking configuration:
*         - Always return 1.
*
* Side Effects: none
*
* Overview: draws line
*
* Note: none
*
********************************************************************/
WORD Line(SHORT x1, SHORT y1, SHORT x2, SHORT y2)
{
#ifdef USE_PALETTE
    #error "In SSD1926 2D-Acceleration is not supported in Palette mode. Use Line function of Primitive layer"
#endif

    SHORT   deltaX, deltaY;
    SHORT   error, stepErrorLT, stepErrorGE;
    SHORT   stepX, stepY;
    SHORT   steep;
    SHORT   temp;
    SHORT   style, type;

    stepX = 0;
    deltaX = x2 - x1;
    if(deltaX < 0)
    {
        deltaX = -deltaX;
        --stepX;
    }
    else
    {
        ++stepX;
    }

    stepY = 0;
    deltaY = y2 - y1;
    if(deltaY < 0)
    {
        deltaY = -deltaY;
        --stepY;
    }
    else
    {
        ++stepY;
    }

    steep = 0;
    if(deltaX < deltaY)
    {
        ++steep;
    }

#ifndef USE_NONBLOCKING_CONFIG
    while(IsDeviceBusy() != 0);

    /* Ready */
#else
    if(IsDeviceBusy() != 0)
        return (0);
#endif
    if(_lineType == 0)
    {
        if(!Line2D(x1, y1, x2, y2))
            return (0);
        if(_lineThickness)
        {
            if(steep)
            {
                while(!Line2D(x1 + 1, y1, x2 + 1, y2));
                while(!Line2D(x1 - 1, y1, x2 - 1, y2));
            }
            else
            {
                while(!Line2D(x1, y1 + 1, x2, y2 + 1));
                while(!Line2D(x1, y1 - 1, x2, y2 - 1));
            }
        }

        return (1);
    }

    // Move cursor
    MoveTo(x2, y2);

    if(x1 == x2)
    {
        if(y1 > y2)
        {
            temp = y1;
            y1 = y2;
            y2 = temp;
        }

        style = 0;
        type = 1;
        for(temp = y1; temp < y2 + 1; temp++)
        {
            if((++style) == _lineType)
            {
                type ^= 1;
                style = 0;
            }

            if(type)
            {
                PutPixel(x1, temp);
                if(_lineThickness)
                {
                    PutPixel(x1 + 1, temp);
                    PutPixel(x1 - 1, temp);
                }
            }
        }

        return (1);
    }

    if(y1 == y2)
    {
        if(x1 > x2)
        {
            temp = x1;
            x1 = x2;
            x2 = temp;
        }

        style = 0;
        type = 1;
        for(temp = x1; temp < x2 + 1; temp++)
        {
            if((++style) == _lineType)
            {
                type ^= 1;
                style = 0;
            }

            if(type)
            {
                PutPixel(temp, y1);
                if(_lineThickness)
                {
                    PutPixel(temp, y1 + 1);
                    PutPixel(temp, y1 - 1);
                }
            }
        }

        return (1);
    }

    if(deltaX < deltaY)
    {
        temp = deltaX;
        deltaX = deltaY;
        deltaY = temp;
        temp = x1;
        x1 = y1;
        y1 = temp;
        temp = stepX;
        stepX = stepY;
        stepY = temp;
        PutPixel(y1, x1);
    }
    else
    {
        PutPixel(x1, y1);
    }

    // If the current error greater or equal zero
    stepErrorGE = deltaX << 1;

    // If the current error less than zero
    stepErrorLT = deltaY << 1;

    // Error for the first pixel
    error = stepErrorLT - deltaX;

    style = 0;
    type = 1;

    while(--deltaX >= 0)
    {
        if(error >= 0)
        {
            y1 += stepY;
            error -= stepErrorGE;
        }

        x1 += stepX;
        error += stepErrorLT;

        if((++style) == _lineType)
        {
            type ^= 1;
            style = 0;
        }

        if(type)
        {
            if(steep)
            {
                PutPixel(y1, x1);
                if(_lineThickness)
                {
                    PutPixel(y1 + 1, x1);
                    PutPixel(y1 - 1, x1);
                }
            }
            else
            {
                PutPixel(x1, y1);
                if(_lineThickness)
                {
                    PutPixel(x1, y1 + 1);
                    PutPixel(x1, y1 - 1);
                }
            }
        }
    }   // end of while

    return (1);
}

#endif

#ifdef USE_DRV_BAR
/*********************************************************************
* Function: void Bar(SHORT left, SHORT top, SHORT right, SHORT bottom)
*
* PreCondition: none
*
* Input: left,top - top left corner coordinates,
*        right,bottom - bottom right corner coordinates
*
* Output: none
*
* Side Effects: none
*
* Overview: draws rectangle filled with current color
*
* Note: none
*
********************************************************************/
WORD Bar(SHORT left, SHORT top, SHORT right, SHORT bottom){
	DWORD   address;
    SHORT   width, height;

#ifndef USE_NONBLOCKING_CONFIG
    while(IsDeviceBusy() != 0);

    /* Ready */
#else
    if(IsDeviceBusy() != 0)
        return (0);
#endif
    if(left > right)
    {
        return (1); /* Don't draw but return 1 */
    }

    if(top > bottom)
    {
        return (1); /* Don't draw but return 1 */
    }

    if(_clipRgn)
    {
        if(left < _clipLeft)
            left = _clipLeft;
        if(right > _clipRight)
            right = _clipRight;
        if(top < _clipTop)
            top = _clipTop;
        if(bottom > _clipBottom)
            bottom = _clipBottom;
    }

    width = right - left + 1;
    height = bottom - top + 1;

    address = top * (GetMaxX() + (DWORD) 1) + left;

    PutPixel(left, top);

    /* Source, Destination & Brush Window Start Addresses */
    SetReg(REG_2D_1d4, address & 0xFF);
    SetReg(REG_2D_1d5, (address >> 8) & 0xFF);
    SetReg(REG_2D_1d6, (address >> 16) & 0xFF);
    SetReg(REG_2D_1f4, address & 0xFF);
    SetReg(REG_2D_1f5, (address >> 8) & 0xFF);
    SetReg(REG_2D_1f6, (address >> 16) & 0xFF);
    SetReg(REG_2D_204, address & 0xFF);
    SetReg(REG_2D_205, (address >> 8) & 0xFF);
    SetReg(REG_2D_206, (address >> 16) & 0xFF);

    /* Source & Destination Window Width */
    SetReg(REG_2D_1ec, width & 0xFF);
    SetReg(REG_2D_1ed, (width >> 8) & 0xFF);
    SetReg(REG_2D_1e4, width & 0xFF);
    SetReg(REG_2D_1e5, (width >> 8) & 0xFF);

    /* Source & Destination Window Height */
    SetReg(REG_2D_1f0, height & 0xFF);
    SetReg(REG_2D_1f1, (height >> 8) & 0xFF);
    SetReg(REG_2D_1e8, height & 0xFF);
    SetReg(REG_2D_1e9, (height >> 8) & 0xFF);

    /* Brush width */
    SetReg(REG_2D_214, 1);
    SetReg(REG_2D_215, 0);

    /* Brush height */
    SetReg(REG_2D_218, 1);
    SetReg(REG_2D_219, 0);

    /* Display width */
    SetReg(REG_2D_1f8, (GetMaxX() + 1) & 0xFF);
    SetReg(REG_2D_1f9, ((GetMaxX() + 1) >> 8) & 0xFF);

    /* Display 2d width */
    SetReg(REG_2D_1d8, (GetMaxX() + 1) & 0xFF);
    SetReg(REG_2D_1d9, ((GetMaxX() + 1) >> 8) & 0xFF);

    /* ROP3 Command */
    SetReg(REG_2D_1fc, 0xF0);

    /* 16bpp */
    SetReg(REG_2D_1dd, 0x00);

    /* ROP command */
    SetReg(REG_2D_1d1, 0x09);

    /* Draw2d command */
    SetReg(REG_2D_1d2, 0x01);

#ifndef USE_NONBLOCKING_CONFIG
    while(IsDeviceBusy() != 0);

    /* Ready */
#endif
    return (1);
}
#endif

#ifdef USE_DRV_CIRCLE
/***************************************************************************
* Function: WORD Circle(SHORT x, SHORT y, SHORT radius)
*
* Overview: This macro draws a circle with the given center and radius.
*
* Input: x - Center x position. 
*		 y - Center y position.
*		 radius - the radius of the circle.
*
* Output: For NON-Blocking configuration:
*         - Returns 0 when device is busy and the shape is not yet completely drawn.
*         - Returns 1 when the shape is completely drawn.
*         For Blocking configuration:
*         - Always return 1.
*
* Side Effects: none
*
********************************************************************/
WORD Circle(SHORT x, SHORT y, SHORT radius)
{
        #define Rx      (WORD) radius
        #define Ry      (WORD) radius
        #define Angle1  (WORD) 0
        #define Angle2  (WORD) 360

        #ifndef USE_NONBLOCKING_CONFIG
    while(IsDeviceBusy() != 0);

    /* Ready */
        #else
    if(IsDeviceBusy() != 0)
        return (0);
        #endif

    /* Ellipse Parameters & Y-Limit */
    SetReg(REG_2D_1d4, x & 0xFF);
    SetReg(REG_2D_1d5, ((((GetMaxY() + (WORD) 1) << 1) & 0xFE) + ((x >> 8) & 0x01)));
    SetReg(REG_2D_1d6, ((GetMaxY() + (WORD) 1) >> 7) & 0x03);

    SetReg(REG_2D_1e4, y & 0xFF);
    SetReg(REG_2D_1e5, (y >> 8) & 0xFF);

    SetReg(REG_2D_1e8, Rx & 0xFF);
    SetReg(REG_2D_1e9, (Rx >> 8) & 0xFF);

    SetReg(REG_2D_1d8, Ry & 0xFF);
    SetReg(REG_2D_1d9, (Ry >> 8) & 0xFF);

    SetReg(REG_2D_1ec, Angle1 & 0xFF);
    SetReg(REG_2D_1ed, (Angle1 >> 8) & 0xFF);

    SetReg(REG_2D_1f0, Angle2 & 0xFF);
    SetReg(REG_2D_1f1, (Angle2 >> 8) & 0xFF);

    /* Destination Window Start Addresses */
    SetReg(REG_2D_1f4, 0);
    SetReg(REG_2D_1f5, 0);
    SetReg(REG_2D_1f6, 0);

    /* Destination Window Width */
    SetReg(REG_2D_1f8, (GetMaxX() + 1) & 0xFF);
    SetReg(REG_2D_1f9, ((GetMaxX() + 1) >> 8) & 0xFF);

    /* Set Color */
    SetReg(REG_2D_1fe, RED8(_color));
    SetReg(REG_2D_1fd, GREEN8(_color));
    SetReg(REG_2D_1fc, BLUE8(_color));

    /* 16bpp */
    SetReg(REG_2D_1dd, 0x00);

    /* Ellipse command */
    SetReg(REG_2D_1d1, 0x03);

    /* Draw2d command */
    SetReg(REG_2D_1d2, 0x01);

        #ifndef USE_NONBLOCKING_CONFIG
    while(IsDeviceBusy() != 0);

    /* Ready */
        #endif
    return (1);
}
#endif

#ifdef USE_DRV_CLEARDEVICE
/*********************************************************************
* Function: void ClearDevice(void)
*
* PreCondition: none
*
* Input: none
*
* Output: none
*
* Side Effects: none
*
* Overview: clears screen with current color 
*
* Note: none
*
********************************************************************/
void ClearDevice(void){
        #ifdef USE_PALETTE

    DWORD   counter;

    CS_LAT_BIT = 0;
    SetAddress(0);
    SetAddress(0);
    for(counter = 0; counter < (DWORD) (GetMaxX() + 1) * (GetMaxY() + 1); counter++)
    {
        WriteData(_color);
    }

    CS_LAT_BIT = 1;

        #else
    //while(GetReg(REG_2D_220) == 0);
    while(IsDeviceBusy() != 0);
    while(!Bar(0, 0, GetMaxX(), GetMaxY()));
    //while(GetReg(REG_2D_220) == 0);
    while(IsDeviceBusy() != 0);

    /* Ready */
        #endif
}
#endif

/*********************************************************************
* Function: void PutImage(SHORT left, SHORT top, void* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner,
*        bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs image starting from left,top coordinates
*
* Note: image must be located in flash
*
********************************************************************/
/*
WORD PutImage(SHORT left, SHORT top, void* bitmap, BYTE stretch){
FLASH_BYTE* flashAddress;
BYTE colorDepth;
WORD colorTemp;

    // Save current color
    colorTemp = _color;

    switch(*((SHORT*)bitmap))
    {
#ifdef USE_BITMAP_FLASH
        case FLASH:
            // Image address
            flashAddress = ((BITMAP_FLASH*)bitmap)->address;
            // Read color depth
            colorDepth = *(flashAddress+1);
            // Draw picture
            switch(colorDepth){
                case 1: PutImage1BPP(left, top, flashAddress, stretch); break;
                case 4: PutImage4BPP(left, top, flashAddress, stretch); break;
                case 8: PutImage8BPP(left, top, flashAddress, stretch); break;
                case 16: PutImage16BPP(left, top, flashAddress, stretch); break;
            }
            break;
#endif
#ifdef USE_BITMAP_EXTERNAL
        case EXTERNAL:
            // Get color depth
            ExternalMemoryCallback(bitmap, 1, 1, &colorDepth);
            // Draw picture
            switch(colorDepth){
                case 1: PutImage1BPPExt(left, top, bitmap, stretch); break;
                case 4: PutImage4BPPExt(left, top, bitmap, stretch); break;
                case 8: PutImage8BPPExt(left, top, bitmap, stretch); break;
                case 16: PutImage16BPPExt(left, top, bitmap, stretch); break;
                default:
                    break;
            }
            break;
#endif
        default:
            break;
    }

    // Restore current color
    _color = colorTemp;
    
    return 1;
}
*/
#ifdef USE_BITMAP_FLASH
/*********************************************************************
* Function: void PutImage1BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner,
*        bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs monochrome image starting from left,top coordinates
*
* Note: image must be located in flash
*
********************************************************************/
void PutImage1BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch){
register DWORD address;
register FLASH_BYTE* flashAddress;
register FLASH_BYTE* tempFlashAddress;
BYTE temp;
WORD sizeX, sizeY;
WORD x,y;
BYTE stretchX,stretchY;
WORD pallete[2];
BYTE mask;

    // Move pointer to size information
    flashAddress = bitmap + 2;

    // Set start address
    address = (long)LINE_MEM_PITCH*top+ left;

    // Read image size
    sizeY = *((FLASH_WORD*)flashAddress);
    flashAddress += 2;
    sizeX = *((FLASH_WORD*)flashAddress);
    flashAddress += 2;
    pallete[0] = *((FLASH_WORD*)flashAddress);
    flashAddress += 2;
    pallete[1] = *((FLASH_WORD*)flashAddress);
    flashAddress += 2;

    CS_LAT_BIT = 0;
    for(y=0; y<sizeY; y++){
        tempFlashAddress = flashAddress;
        for(stretchY = 0; stretchY<stretch; stretchY++){
            flashAddress = tempFlashAddress;
            SetAddress(address);
            mask = 0;
            for(x=0; x<sizeX; x++){

                // Read 8 pixels from flash
                if(mask == 0){
                    temp = *flashAddress;
                    flashAddress++;
                    mask = 0x80;
                }
                
                // Set color
                if(mask&temp){
                    SetColor(pallete[1]);
                }else{
                    SetColor(pallete[0]);
                }

                // Write pixel to screen
                for(stretchX=0; stretchX<stretch; stretchX++){
                    WriteData(_color);
                }

                // Shift to the next pixel
                mask >>= 1;
           }
           address += LINE_MEM_PITCH; 
        }
    }
    CS_LAT_BIT = 1;
}

/*********************************************************************
* Function: void PutImage4BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner, bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs 16 color image starting from left,top coordinates
*
* Note: image must be located in flash
*
********************************************************************/
void PutImage4BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch){
register DWORD address;
register FLASH_BYTE* flashAddress;
register FLASH_BYTE* tempFlashAddress;
WORD sizeX, sizeY;
register WORD x,y;
BYTE temp;
register BYTE stretchX,stretchY;
WORD pallete[16];
WORD counter;

    // Move pointer to size information
    flashAddress = bitmap + 2;

    // Set start address
    address = (long)LINE_MEM_PITCH*top+ left;

    // Read image size
    sizeY = *((FLASH_WORD*)flashAddress);
    flashAddress += 2;
    sizeX = *((FLASH_WORD*)flashAddress);
    flashAddress += 2;

    // Read pallete
    for(counter=0;counter<16;counter++){
        pallete[counter] = *((FLASH_WORD*)flashAddress);
        flashAddress += 2;
    }

    CS_LAT_BIT = 0;     
    for(y=0; y<sizeY; y++){
        tempFlashAddress = flashAddress;
        for(stretchY = 0; stretchY<stretch; stretchY++){
            flashAddress = tempFlashAddress;
            SetAddress(address);
            for(x=0; x<sizeX; x++){
                // Read 2 pixels from flash
                if(x&0x0001){
                    // second pixel in byte
                    SetColor(pallete[temp>>4]);
                }else{
                    temp = *flashAddress;
                    flashAddress++;
                    // first pixel in byte
                    SetColor(pallete[temp&0x0f]);
                }

                // Write pixel to screen       
                for(stretchX=0; stretchX<stretch; stretchX++){
                    WriteData(_color);
                }

                // Shift to the next pixel
                //temp >>= 4;
            }
            address += LINE_MEM_PITCH;
        }
    }
    CS_LAT_BIT = 1;
}

/*********************************************************************
* Function: void PutImage8BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner, bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs 256 color image starting from left,top coordinates
*
* Note: image must be located in flash
*
********************************************************************/
void PutImage8BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch){
register DWORD address;
register FLASH_BYTE* flashAddress;
register FLASH_BYTE* tempFlashAddress;
WORD sizeX, sizeY;
WORD x,y;
BYTE temp;
BYTE stretchX, stretchY;
WORD pallete[256];
WORD counter;

    // Move pointer to size information
    flashAddress = bitmap + 2;

    // Set start address
    address = (long)LINE_MEM_PITCH*top+ left;

    // Read image size
    sizeY = *((FLASH_WORD*)flashAddress);
    flashAddress += 2;
    sizeX = *((FLASH_WORD*)flashAddress);
    flashAddress += 2;

    // Read pallete
    for(counter=0;counter<256;counter++){
        pallete[counter] = *((FLASH_WORD*)flashAddress);
        flashAddress += 2;
    }

    CS_LAT_BIT = 0;     
    for(y=0; y<sizeY; y++){
        tempFlashAddress = flashAddress;
        for(stretchY = 0; stretchY<stretch; stretchY++){
            flashAddress = tempFlashAddress;
            SetAddress(address);
            for(x=0; x<sizeX; x++){
                // Read pixels from flash
                temp = *flashAddress;
                flashAddress++;

                // Set color
                SetColor(pallete[temp]);

                // Write pixel to screen       
                for(stretchX=0; stretchX<stretch; stretchX++){
                    WriteData(_color);
                }
            }
            address += LINE_MEM_PITCH;
        }
    }
    CS_LAT_BIT = 1;
}

/*********************************************************************
* Function: void PutImage16BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner, bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs hicolor image starting from left,top coordinates
*
* Note: image must be located in flash
*
********************************************************************/
void PutImage16BPP(SHORT left, SHORT top, FLASH_BYTE* bitmap, BYTE stretch){
register DWORD address;
register FLASH_WORD* flashAddress;
register FLASH_WORD* tempFlashAddress;
WORD sizeX, sizeY;
register WORD x,y;
WORD temp;
register BYTE stretchX,stretchY;

    // Move pointer to size information
    flashAddress = (FLASH_WORD*)bitmap + 1;

    // Set start address
    address = (long)LINE_MEM_PITCH*top+ left;

    // Read image size
    sizeY = *flashAddress;
    flashAddress++;
    sizeX = *flashAddress;
    flashAddress++;

    CS_LAT_BIT = 0;
    for(y=0; y<sizeY; y++){
        tempFlashAddress = flashAddress;
        for(stretchY = 0; stretchY<stretch; stretchY++){
            flashAddress = tempFlashAddress;
            SetAddress(address);
            for(x=0; x<sizeX; x++){
                // Read pixels from flash
                temp = *flashAddress;
                flashAddress++;

                // Set color
                SetColor(temp);

                // Write pixel to screen       
                for(stretchX=0; stretchX<stretch; stretchX++){
                    WriteData(_color);
                }
            }
            address += LINE_MEM_PITCH;
        }
    }
    CS_LAT_BIT = 1;
}
#endif

#ifdef USE_BITMAP_EXTERNAL
/*********************************************************************
* Function: void PutImage1BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner, bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs monochrome image starting from left,top coordinates
*
* Note: image must be located in external memory
*
********************************************************************/
void PutImage1BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch){
register DWORD_VAL  address;
register DWORD      memOffset;
BITMAP_HEADER       bmp;
WORD                pallete[2];
BYTE                lineBuffer[(SCREEN_HOR_SIZE/8)+1];
BYTE*               pData; 
SHORT               byteWidth;

BYTE                temp;
BYTE                mask;
WORD                sizeX, sizeY;
WORD                x,y;
BYTE                stretchX, stretchY;

    // Set start address
    address = (long)LINE_MEM_PITCH*top+ left;

    // Get bitmap header
    ExternalMemoryCallback(bitmap, 0, sizeof(BITMAP_HEADER), &bmp);

    // Get pallete (2 entries)
    ExternalMemoryCallback(bitmap, sizeof(BITMAP_HEADER), 2*sizeof(WORD), pallete);

    // Set offset to the image data
    memOffset = sizeof(BITMAP_HEADER) + 2*sizeof(WORD);

    // Line width in bytes
    byteWidth = bmp.width>>3;
    if(bmp.width&0x0007)
        byteWidth++;

    // Get size
    sizeX = bmp.width;
    sizeY = bmp.height;

    for(y=0; y<sizeY; y++){
        // Get line
        ExternalMemoryCallback(bitmap, memOffset, byteWidth, lineBuffer);
        memOffset += byteWidth;

        CS_LAT_BIT = 0;
        for(stretchY = 0; stretchY<stretch; stretchY++){
            pData = lineBuffer;
            SetAddress(address);
            mask = 0;
            for(x=0; x<sizeX; x++){

                // Read 8 pixels from flash
                if(mask == 0){
                    temp = *pData++;
                    mask = 0x80;
                }
                
                // Set color
                if(mask&temp){
                    SetColor(pallete[1]);
                }else{
                    SetColor(pallete[0]);
                }

                // Write pixel to screen
                for(stretchX=0; stretchX<stretch; stretchX++){
                    WriteData(_color);
                }

                // Shift to the next pixel
                mask >>= 1;
           }
           address += LINE_MEM_PITCH; 
        }
        CS_LAT_BIT = 1;
    }

}

/*********************************************************************
* Function: void PutImage4BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner, bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs monochrome image starting from left,top coordinates
*
* Note: image must be located in external memory
*
********************************************************************/
void PutImage4BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch){
register DWORD_VAL  address;
register DWORD      memOffset;
BITMAP_HEADER       bmp;
WORD                pallete[16];
BYTE                lineBuffer[(SCREEN_HOR_SIZE/2)+1];
BYTE*               pData; 
SHORT               byteWidth;

BYTE                temp;
WORD                sizeX, sizeY;
WORD                x,y;
BYTE                stretchX, stretchY;

    // Set start address
    address = (long)LINE_MEM_PITCH*top+ left;

    // Get bitmap header
    ExternalMemoryCallback(bitmap, 0, sizeof(BITMAP_HEADER), &bmp);

    // Get pallete (16 entries)
    ExternalMemoryCallback(bitmap, sizeof(BITMAP_HEADER), 16*sizeof(WORD), pallete);

    // Set offset to the image data
    memOffset = sizeof(BITMAP_HEADER) + 16*sizeof(WORD);

    // Line width in bytes
    byteWidth = bmp.width>>1;
    if(bmp.width&0x0001)
        byteWidth++;

    // Get size
    sizeX = bmp.width;
    sizeY = bmp.height;

    for(y=0; y<sizeY; y++){

        // Get line
        ExternalMemoryCallback(bitmap, memOffset, byteWidth, lineBuffer);
        memOffset += byteWidth;
        CS_LAT_BIT = 0;
        for(stretchY = 0; stretchY<stretch; stretchY++){

            pData = lineBuffer;
            SetAddress(address);

            for(x=0; x<sizeX; x++){

                // Read 2 pixels from flash
                if(x&0x0001){
                    // second pixel in byte
                    SetColor(pallete[temp>>4]);
                }else{
                    temp = *pData++;
                    // first pixel in byte
                    SetColor(pallete[temp&0x0f]);
                }

                // Write pixel to screen       
                for(stretchX=0; stretchX<stretch; stretchX++){
                    WriteData(_color);
                }
           }
           address += LINE_MEM_PITCH; 
        }
        CS_LAT_BIT = 1;
    }
}

/*********************************************************************
* Function: void PutImage8BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner, bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs monochrome image starting from left,top coordinates
*
* Note: image must be located in external memory
*
********************************************************************/
void PutImage8BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch){
register DWORD_VAL  address;
register DWORD      memOffset;
BITMAP_HEADER       bmp;
WORD                pallete[256];
BYTE                lineBuffer[SCREEN_HOR_SIZE];
BYTE*               pData; 

BYTE                temp;
WORD                sizeX, sizeY;
WORD                x,y;
BYTE                stretchX, stretchY;

    // Set start address
    address = (long)LINE_MEM_PITCH*top+ left;

    // Get bitmap header
    ExternalMemoryCallback(bitmap, 0, sizeof(BITMAP_HEADER), &bmp);

    // Get pallete (256 entries)
    ExternalMemoryCallback(bitmap, sizeof(BITMAP_HEADER), 256*sizeof(WORD), pallete);

    // Set offset to the image data
    memOffset = sizeof(BITMAP_HEADER) + 256*sizeof(WORD);

    // Get size
    sizeX = bmp.width;
    sizeY = bmp.height;

    for(y=0; y<sizeY; y++){

        // Get line
        ExternalMemoryCallback(bitmap, memOffset, sizeX, lineBuffer);
        memOffset += sizeX;
        CS_LAT_BIT = 0;
        for(stretchY = 0; stretchY<stretch; stretchY++){

            pData = lineBuffer;
            SetAddress(address);

            for(x=0; x<sizeX; x++){

                temp = *pData++;
                SetColor(pallete[temp]);                    

                // Write pixel to screen       
                for(stretchX=0; stretchX<stretch; stretchX++){
                    WriteData(_color);
                }
           }
           address += LINE_MEM_PITCH; 
        }
        CS_LAT_BIT = 1;
    }
}

/*********************************************************************
* Function: void PutImage16BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch)
*
* PreCondition: none
*
* Input: left,top - left top image corner, bitmap - image pointer,
*        stretch - image stretch factor
*
* Output: none
*
* Side Effects: none
*
* Overview: outputs monochrome image starting from left,top coordinates
*
* Note: image must be located in external memory
*
********************************************************************/
void PutImage16BPPExt(SHORT left, SHORT top, void* bitmap, BYTE stretch){
register DWORD_VAL  address;
register DWORD      memOffset;
BITMAP_HEADER       bmp;
WORD                lineBuffer[SCREEN_HOR_SIZE];
WORD*               pData; 
WORD                byteWidth;

WORD                temp;
WORD                sizeX, sizeY;
WORD                x,y;
BYTE                stretchX, stretchY;

    // Set start address
    address = (long)LINE_MEM_PITCH*top+ left;

    // Get bitmap header
    ExternalMemoryCallback(bitmap, 0, sizeof(BITMAP_HEADER), &bmp);

    // Set offset to the image data
    memOffset = sizeof(BITMAP_HEADER);

    // Get size
    sizeX = bmp.width;
    sizeY = bmp.height;

    byteWidth = sizeX<<1;

    for(y=0; y<sizeY; y++){
        // Get line
        ExternalMemoryCallback(bitmap, memOffset, byteWidth, lineBuffer);
        memOffset += byteWidth;
        CS_LAT_BIT = 0;
        for(stretchY = 0; stretchY<stretch; stretchY++){

            pData = lineBuffer;
            SetAddress(address);

            for(x=0; x<sizeX; x++){

                temp = *pData++;
                SetColor(temp);                    

                // Write pixel to screen       
                for(stretchX=0; stretchX<stretch; stretchX++){
                    WriteData(_color);
                }

           }

           address += LINE_MEM_PITCH; 

        }
        CS_LAT_BIT = 1;
    }
}
#endif

#endif // #if (GFX_USE_DISPLAY_CONTROLLER_MI043_LQ043)
