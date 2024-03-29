/******************************************************************************

    USB Host Client Driver for Android(tm) Devices - protocol version 1

The Android Operating System update v2.3.4 includes a module for accessing
the USB port of the device with an application.  This is done through an
USB accessory framework in the OS.  This client driver provides support to 
talk to that interface


*******************************************************************************/
//DOM-IGNORE-BEGIN
/******************************************************************************

 File Name:       usb_host_android.c
 Dependencies:    None
 Processor:       PIC24F/PIC32MX
 Compiler:        C30/C32
 Company:         Microchip Technology, Inc.

Software License Agreement

The software supplied herewith by Microchip Technology Incorporated
(the �Company�) for its PICmicro� Microcontroller is intended and
supplied to you, the Company�s customer, for use solely and
exclusively on Microchip PICmicro Microcontroller products. The
software is owned by the Company and/or its supplier, and is
protected under applicable copyright laws. All rights are reserved.
Any use in violation of the foregoing restrictions may subject the
user to criminal sanctions under applicable laws, as well as to
civil liability for the breach of the terms and conditions of this
license.

THIS SOFTWARE IS PROVIDED IN AN �AS IS� CONDITION. NO WARRANTIES,
WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED
TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. THE COMPANY SHALL NOT,
IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL OR
CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.

Change History:
  Rev         Description
  ----------  ----------------------------------------------------------
  2.9         Initial release

*******************************************************************************/

#include "Compiler.h"
#include "GenericTypeDefs.h"

#include "USB/usb.h"
#include "USB/usb_host_android.h"
#include "usb_host_android_protocol_v1_local.h"

//************************************************************
// External global variables include
//************************************************************
extern ANDROID_ACCESSORY_INFORMATION *accessoryInfo;

//************************************************************
// Internal type definitions
//************************************************************
typedef enum _ANDROID_ACCESSORY_STRINGS
{
    ANDROID_ACCESSORY_STRING_MANUFACTURER   = 0,
    ANDROID_ACCESSORY_STRING_MODEL          = 1,
    ANDROID_ACCESSORY_STRING_DESCRIPTION    = 2,
    ANDROID_ACCESSORY_STRING_VERSION        = 3,
    ANDROID_ACCESSORY_STRING_URI            = 4,
    ANDROID_ACCESSORY_STRING_SERIAL         = 5
} ANDROID_ACCESSORY_STRINGS;

#define USB_DEV_DESC_VID_OFFSET                         8
#define USB_DEV_DESC_PID_OFFSET                         10

#define USB_DESC_BLENGTH_OFFSET                         0
#define USB_DESC_BDESCRIPTORTYPE_OFFSET                 1

#define USB_INTERFACE_DESC_BINTERFACENUMBER_OFFSET      2
#define USB_INTERFACE_DESC_BALTERNATESETTING_OFFSET     3
#define USB_INTERFACE_DESC_BINTERFACECLASS_OFFSET       5
#define USB_INTERFACE_DESC_BINTERFACESUBCLASS_OFFSET    6
#define USB_INTERFACE_DESC_BINTERFACEPROTOCOL_OFFSET    7

#define USB_CDC_DESC_BDESCRIPTORSUBTYPE_OFFSET          2
#define USB_CDC_DESC_UNION_BMASTERINTERFACE_OFFSET      3  

#define USB_ENDPOINT_DESC_BENDPOINTADDRESS_OFFSET       2 
#define USB_ENDPOINT_DESC_BMATTRIBUTES_OFFSET           3 
#define USB_ENDPOINT_DESC_WMAXPACKETSIZE_OFFSET         4 
#define USB_ENDPOINT_DESC_BINTERVAL_OFFSET              5

//** Control transfer codes **
#define     ANDROID_ACCESSORY_SEND_STRING   52
#define     ANDROID_ACCESSORY_START         53

typedef enum
{
    //NO_DEVICE needs to be 0 so that the memset in the init function
    //  clears this to the right value
    NO_DEVICE = 0,
    DEVICE_ATTACHED,
    SEND_MANUFACTUER_STRING,
    SEND_MODEL_STRING,
    SEND_DESCRIPTION_STRING,
    SEND_VERSION_STRING,
    SEND_URI_STRING,
    SEND_SERIAL_STRING,
    START_ACCESSORY,
    ACCESSORY_STARTING,
    WAITING_FOR_ACCESSORY_RETURN,
    RETURN_OF_THE_ACCESSORY,
    READY
    
} ANDROID_DEVICE_STATUS_PV1;

typedef struct
{
    BYTE address;
    BYTE clientDriverID;
    BYTE OUTEndpointNum;
    WORD OUTEndpointSize;
    BYTE INEndpointNum;
    WORD INEndpointSize;
    ANDROID_DEVICE_STATUS_PV1 state;
    WORD countDown;

    struct
    {
        BYTE TXBusy :1;
        BYTE RXBusy :1;
        BYTE EP0TransferPending :1;
    } status;

} ANDROID_PROTOCOL_V1_DEVICE_DATA;

static ANDROID_PROTOCOL_V1_DEVICE_DATA devices_pv1[NUM_ANDROID_DEVICES_SUPPORTED];

//************************************************************
// Internal prototypes
//************************************************************
static BYTE AndroidCommandSendString_Pv1(void *handle, ANDROID_ACCESSORY_STRINGS stringType, const char *string, WORD stringLength);
static BYTE AndroidCommandStart_Pv1(void *handle);

//************************************************************
// External function prototypes
//************************************************************
BOOL AndroidIsLastCommandComplete(BYTE address, BYTE *errorCode, DWORD *byteCount);

//************************************************************
// Internal macro helper functions
//************************************************************
#define ReadWORD(dest,source) {memcpy(dest,source,2);}
#define ReadDWORD(dest,source) {memcpy(dest,source,4);}

#define ANDROID_GetOUTEndpointSize(handle)  handle->OUTEndpointSize
#define ANDROID_GetINEndpointSize(handle)   handle->INEndpointSize
#define ANDROID_GetOUTEndpointNum(handle)   handle->OUTEndpointNum
#define ANDROID_GetINEndpointNum(handle)    handle->INEndpointNum


/********************************************************************/
/********************************************************************/
/********************************************************************/
/**     Interface Functions                                        **/
/********************************************************************/
/********************************************************************/
/********************************************************************/


/****************************************************************************
  Function:
    void AndroidAppStart_Pv1(void)

  Summary:
    Initializes the Android protocol version 1 sub-driver

  Description:
    Initializes the Android protocol version 1 sub-driver    

  Precondition:
    None

  Parameters:
    None

  Return Values:
    None

  Remarks:
    Should never be called after the system initialization.  This will kill
    any currently attached device information.
  ***************************************************************************/
void AndroidAppStart_Pv1(void)
{
    memset(&devices_pv1,0x00,sizeof(devices_pv1));
}


/****************************************************************************
  Function:
    BYTE AndroidAppWrite_Pv1(void* handle, BYTE* data, DWORD size)

  Summary:
    Writes data out to the Android device

  Description:
    Writes data out to the Android device

  Precondition:
    Protocol version 1 sub-driver initialized through AndroidAppStart_Pv1()

  Parameters:
    void* handle - handle to the device that should receive the data
    BYTE* data - the data to send
    DWORD size - the amount of data to send

  Return Values:
    USB_SUCCESS                     - Write started successfully.
    USB_UNKNOWN_DEVICE              - Device with the specified address not found.
    USB_INVALID_STATE               - We are not in a normal running state.
    USB_ENDPOINT_ILLEGAL_TYPE       - Must use USBHostControlWrite to write
                                        to a control endpoint.
    USB_ENDPOINT_ILLEGAL_DIRECTION  - Must write to an OUT endpoint.
    USB_ENDPOINT_STALLED            - Endpoint is stalled.  Must be cleared
                                        by the application.
    USB_ENDPOINT_ERROR              - Endpoint has too many errors.  Must be
                                        cleared by the application.
    USB_ENDPOINT_BUSY               - A Write is already in progress.
    USB_ENDPOINT_NOT_FOUND          - Invalid endpoint.

  Remarks:
    None
  ***************************************************************************/
BYTE AndroidAppWrite_Pv1(void* handle, BYTE* data, DWORD size)
{
    BYTE errorCode;
    ANDROID_PROTOCOL_V1_DEVICE_DATA* device = (ANDROID_PROTOCOL_V1_DEVICE_DATA*)handle;

    if(device == NULL)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if(device->address == 0)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if(device->state != READY)
    {
        return USB_INVALID_STATE;
    }    

    if(device->status.TXBusy == 1)
    {
        return USB_ENDPOINT_BUSY;
    }

    errorCode = USBHostWrite( device->address, ANDROID_GetOUTEndpointNum(device),
                                            data, size );
    
    switch(errorCode)
    {
        case USB_ENDPOINT_BUSY:
        case USB_SUCCESS:
            device->status.TXBusy = 1;
            break;
        default:
            device->status.TXBusy = 0;
            break;
    }

    return errorCode;
}

/****************************************************************************
  Function:
    BOOL AndroidAppIsWriteComplete_Pv1(void* handle, BYTE* errorCode, DWORD* size)

  Summary:
    Check to see if the last write to the Android device was completed

  Description:
    Check to see if the last write to the Android device was completed.  If 
    complete, returns the amount of data that was sent and the corresponding 
    error code for the transmission.

  Precondition:
    Transfer has previously been sent to Android device.

  Parameters:
    void* handle - the handle passed to the device in the EVENT_ANDROID_ATTACH event
    BYTE* errorCode - a pointer to the location where the resulting error code should be written
    DWORD* size - a pointer to the location where the resulting size information should be written

  Return Values:
    TRUE    - Transfer is complete.
    FALSE   - Transfer is not complete.

  Remarks:
    Possible values for errorCode are:
        * USB_SUCCESS                     - Transfer successful
        * USB_UNKNOWN_DEVICE              - Device not attached
        * USB_ENDPOINT_STALLED            - Endpoint STALL'd
        * USB_ENDPOINT_ERROR_ILLEGAL_PID  - Illegal PID returned
        * USB_ENDPOINT_ERROR_BIT_STUFF
        * USB_ENDPOINT_ERROR_DMA
        * USB_ENDPOINT_ERROR_TIMEOUT
        * USB_ENDPOINT_ERROR_DATA_FIELD
        * USB_ENDPOINT_ERROR_CRC16
        * USB_ENDPOINT_ERROR_END_OF_FRAME
        * USB_ENDPOINT_ERROR_PID_CHECK
        * USB_ENDPOINT_ERROR              - Other error
  ***************************************************************************/
BOOL AndroidAppIsWriteComplete_Pv1(void* handle, BYTE* errorCode, DWORD* size)
{
    ANDROID_PROTOCOL_V1_DEVICE_DATA* device = (ANDROID_PROTOCOL_V1_DEVICE_DATA*)handle;

    if(device == NULL)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if(device->address == 0)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if(device->state != READY)
    {
        return USB_INVALID_STATE;
    }    

    //If there was a transfer pending, then get the state of the transfer
    if(USBHostTransferIsComplete(
                                    device->address, 
                                    ANDROID_GetOUTEndpointNum(device),
                                    errorCode,
                                    size
                                ) == TRUE)
    {
        if(errorCode != USB_SUCCESS)
        {
            device->status.TXBusy = 0;
            return TRUE;
        }

        device->status.TXBusy = 0;
        return TRUE;
    }

    //Then the transfer was not complete.
    return FALSE;
}

/****************************************************************************
  Function:
    BYTE AndroidAppRead_Pv1(void* handle, BYTE* data, DWORD size)

  Summary:
    Attempts to read information from the specified Android device

  Description:
    Attempts to read information from the specified Android device.  This
    function does not block.  Data availability is checked via the 
    AndroidAppIsReadComplete() function.

  Precondition:
    A read request is not already in progress and an Android device is attached.

  Parameters:
    void* handle - the handle passed to the device in the EVENT_ANDROID_ATTACH event
    BYTE* data - a pointer to the location of where the data should be stored.  This location
                should be accessible by the USB module
    DWORD size - the amount of data to read.

  Return Values:
    USB_SUCCESS                     - Read started successfully.
    USB_UNKNOWN_DEVICE              - Device with the specified address not found.
    USB_INVALID_STATE               - We are not in a normal running state.
    USB_ENDPOINT_ILLEGAL_TYPE       - Must use USBHostControlRead to read
                                        from a control endpoint.
    USB_ENDPOINT_ILLEGAL_DIRECTION  - Must read from an IN endpoint.
    USB_ENDPOINT_STALLED            - Endpoint is stalled.  Must be cleared
                                        by the application.
    USB_ENDPOINT_ERROR              - Endpoint has too many errors.  Must be
                                        cleared by the application.
    USB_ENDPOINT_BUSY               - A Read is already in progress.
    USB_ENDPOINT_NOT_FOUND          - Invalid endpoint.

  Remarks:
    None
  ***************************************************************************/
BYTE AndroidAppRead_Pv1(void* handle, BYTE* data, DWORD size)
{
    ANDROID_PROTOCOL_V1_DEVICE_DATA* device = (ANDROID_PROTOCOL_V1_DEVICE_DATA*) handle;

    BYTE errorCode;

    if(device == NULL)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if(device->address == 0)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if(device->state != READY)
    {
        return USB_INVALID_STATE;
    }    

    if(device->status.RXBusy == 1)
    {
        return USB_ENDPOINT_BUSY;
    }

    if(size < device->INEndpointSize)
    {
        return USB_ERROR_BUFFER_TOO_SMALL;
    }

    errorCode = USBHostRead( device->address, ANDROID_GetINEndpointNum(device),
                                            data, ((size / device->INEndpointSize) * device->INEndpointSize) );
    
    switch(errorCode)
    {
        case USB_SUCCESS:
        case USB_ENDPOINT_BUSY:
            device->status.RXBusy = 1;
            break;
        default:
            device->status.RXBusy = 0;
            break;
    }

    return errorCode;
}


/****************************************************************************
  Function:
    BOOL AndroidAppIsReadComplete_Pv1(void* handle, BYTE* errorCode, DWORD* size)

  Summary:
    Check to see if the last read to the Android device was completed

  Description:
    Check to see if the last read to the Android device was completed.  If 
    complete, returns the amount of data that was sent and the corresponding 
    error code for the transmission.

  Precondition:
    Transfer has previously been requested from an Android device.

  Parameters:
    void* handle - the handle passed to the device in the EVENT_ANDROID_ATTACH event
    BYTE* errorCode - a pointer to the location where the resulting error code should be written
    DWORD* size - a pointer to the location where the resulting size information should be written

  Return Values:
    TRUE    - Transfer is complete.
    FALSE   - Transfer is not complete.

  Remarks:
    Possible values for errorCode are:
        * USB_SUCCESS                     - Transfer successful
        * USB_UNKNOWN_DEVICE              - Device not attached
        * USB_ENDPOINT_STALLED            - Endpoint STALL'd
        * USB_ENDPOINT_ERROR_ILLEGAL_PID  - Illegal PID returned
        * USB_ENDPOINT_ERROR_BIT_STUFF
        * USB_ENDPOINT_ERROR_DMA
        * USB_ENDPOINT_ERROR_TIMEOUT
        * USB_ENDPOINT_ERROR_DATA_FIELD
        * USB_ENDPOINT_ERROR_CRC16
        * USB_ENDPOINT_ERROR_END_OF_FRAME
        * USB_ENDPOINT_ERROR_PID_CHECK
        * USB_ENDPOINT_ERROR              - Other error
  ***************************************************************************/
BOOL AndroidAppIsReadComplete_Pv1(void* handle, BYTE* errorCode, DWORD* size)
{
    ANDROID_PROTOCOL_V1_DEVICE_DATA* device = (ANDROID_PROTOCOL_V1_DEVICE_DATA*)handle;

    if(device == NULL)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if(device->address == 0)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if(device->state != READY)
    {
        return USB_INVALID_STATE;
    }    

    //If there was a transfer pending, then get the state of the transfer
    if(USBHostTransferIsComplete(
                                    device->address, 
                                    ANDROID_GetINEndpointNum(device),
                                    errorCode,
                                    size
                                ) == TRUE)
    {
        device->status.RXBusy = 0;
        return TRUE;
    }

    //Then the transfer was not complete.
    return FALSE;
}


/****************************************************************************
  Function:
    void AndroidTasks_Pv1(void)

  Summary:
    Tasks function that keeps the Android client driver moving

  Description:
    Tasks function that keeps the Android client driver moving.  Keeps the driver
    processing requests and handling events.  This function should be called
    periodically (the same frequency as USBHostTasks() would be helpful).

  Precondition:
    AndroidAppStart() function has been called before the first calling of this function

  Parameters:
    None

  Return Values:
    None

  Remarks:
    This function should be called periodically to keep the Android driver moving.
  ***************************************************************************/
void AndroidTasks_Pv1(void)
{
    BYTE i;
    ANDROID_PROTOCOL_V1_DEVICE_DATA* device;
    BYTE errorCode;
    DWORD byteCount;

    //See if any of the devices need to do something
    for(i=0;i<NUM_ANDROID_DEVICES_SUPPORTED;i++)
    {
        device = &devices_pv1[i];

        switch(device->state)
        {
            case DEVICE_ATTACHED:
                //Fall through
            case SEND_MANUFACTUER_STRING:
                //Check to see if something else is going on with EP0
                //TODO: should switch this to use the transfer events instead.  It is safer.
                if(AndroidIsLastCommandComplete(device->address, &errorCode, &byteCount) == TRUE)
                {
                    //If not, then let's send the manufacturer's string
                    AndroidCommandSendString_Pv1(device, ANDROID_ACCESSORY_STRING_MANUFACTURER, accessoryInfo->manufacturer, accessoryInfo->manufacturer_size);

                    device->status.EP0TransferPending = 1;
                    device->state = SEND_MODEL_STRING;
                }
                break;

            case SEND_MODEL_STRING:
                if(device->status.EP0TransferPending == 0)
                {
                    //The manufacturing string is sent.  Now try to send the model string
                    //TODO: should switch this to use the transfer events instead.  It is safer.
                    if(AndroidIsLastCommandComplete(device->address, &errorCode, &byteCount) == TRUE)
                    {
                        //If not, then let's send the manufacturer's string
                        AndroidCommandSendString_Pv1(device, ANDROID_ACCESSORY_STRING_MODEL, accessoryInfo->model, accessoryInfo->model_size);
    
                        device->status.EP0TransferPending = 1;
                        device->state = SEND_DESCRIPTION_STRING;
                    }
                }
                break;

            case SEND_DESCRIPTION_STRING:
                if(device->status.EP0TransferPending == 0)
                {
                    //The manufacturing string is sent.  Now try to send the model string
                    //TODO: should switch this to use the transfer events instead.  It is safer.
                    if(AndroidIsLastCommandComplete(device->address, &errorCode, &byteCount) == TRUE)
                    {
                        //If not, then let's send the manufacturer's string
                        AndroidCommandSendString_Pv1(device, ANDROID_ACCESSORY_STRING_DESCRIPTION, accessoryInfo->description, accessoryInfo->description_size);
    
                        device->status.EP0TransferPending = 1;
                        device->state = SEND_VERSION_STRING;
                    }
                }
                break;

            case SEND_VERSION_STRING:
                if(device->status.EP0TransferPending == 0)
                {
                    //The manufacturing string is sent.  Now try to send the model string
                    //TODO: should switch this to use the transfer events instead.  It is safer.
                    if(AndroidIsLastCommandComplete(device->address, &errorCode, &byteCount) == TRUE)
                    {
                        //If not, then let's send the manufacturer's string
                        AndroidCommandSendString_Pv1(device, ANDROID_ACCESSORY_STRING_VERSION, accessoryInfo->version, accessoryInfo->version_size);
    
                        device->status.EP0TransferPending = 1;
                        device->state = SEND_URI_STRING;
                    }
                }
                break;

            case SEND_URI_STRING:
                if(device->status.EP0TransferPending == 0)
                {
                    //The manufacturing string is sent.  Now try to send the model string
                    //TODO: should switch this to use the transfer events instead.  It is safer.
                    if(AndroidIsLastCommandComplete(device->address, &errorCode, &byteCount) == TRUE)
                    {
                        //If not, then let's send the manufacturer's string
                        AndroidCommandSendString_Pv1(device, ANDROID_ACCESSORY_STRING_URI, accessoryInfo->URI, accessoryInfo->URI_size);
    
                        device->status.EP0TransferPending = 1;
                        device->state = SEND_SERIAL_STRING;
                    }
                }
                break;

            case SEND_SERIAL_STRING:
                if(device->status.EP0TransferPending == 0)
                {
                    //The manufacturing string is sent.  Now try to send the model string
                    //TODO: should switch this to use the transfer events instead.  It is safer.
                    if(AndroidIsLastCommandComplete(device->address, &errorCode, &byteCount) == TRUE)
                    {
                        //If not, then let's send the manufacturer's string
                        AndroidCommandSendString_Pv1(device, ANDROID_ACCESSORY_STRING_SERIAL, accessoryInfo->serial, accessoryInfo->serial_size);
    
                        device->status.EP0TransferPending = 1;
                        device->state = START_ACCESSORY;
                    }
                }
                break;

            case START_ACCESSORY:
                if(device->status.EP0TransferPending == 0)
                {
                    //The manufacturing string is sent.  Now try to send the model string
                    //TODO: should switch this to use the transfer events instead.  It is safer.
                    if(AndroidIsLastCommandComplete(device->address, &errorCode, &byteCount) == TRUE)
                    {
                        //Set up a timer to remove the device if it hasn't returned to us as an
                        //  accessory mode device in a specified time, we kill the device
                        device->countDown = ANDROID_DEVICE_ATTACH_TIMEOUT;

                        //If not, then let's send the manufacturer's string
                        AndroidCommandStart_Pv1(device);
    
                        device->status.EP0TransferPending = 1;
                        device->state = ACCESSORY_STARTING;
                    }
                }
                break;

            case ACCESSORY_STARTING:
                if(device->status.EP0TransferPending == 0)
                {
                    device->state = WAITING_FOR_ACCESSORY_RETURN;
                }   
                break;

            case WAITING_FOR_ACCESSORY_RETURN:
                break;

            case RETURN_OF_THE_ACCESSORY:
                //The accessory has returned and has been initialialized.  It is now ready to use.
                device->state = READY;
                USB_HOST_APP_EVENT_HANDLER(device->address,EVENT_ANDROID_ATTACH,device,sizeof(ANDROID_PROTOCOL_V1_DEVICE_DATA*));
                break; 
             
            case READY:
                break;

            default:
                //Don't know what state the device is in.  Do some recovery here?
                break;
        }
    }
}


/****************************************************************************
  Function:
    void* AndroidInitialize_Pv1 ( BYTE address, DWORD flags, BYTE clientDriverID )

  Summary:
    Per instance client driver for Android device.  Called by USB host stack from
    the client driver table.

  Description:
    Per instance client driver for Android device.  Called by USB host stack from
    the client driver table.

  Precondition:
    None

  Parameters:
    BYTE address - the address of the device that is being initialized
    DWORD flags - the initialization flags for the device
    BYTE clientDriverID - the clientDriverID for the device

  Return Values:
    TRUE - initialized successfully
    FALSE - does not support this device

  Remarks:
    This is a internal API only.  This should not be called by anything other
    than the USB host stack via the client driver table
  ***************************************************************************/
void* AndroidInitialize_Pv1 ( BYTE address, DWORD flags, BYTE clientDriverID )
{
    BYTE   *config_descriptor         = NULL;
    BYTE *device_descriptor = NULL;
    WORD tempWord;
    BYTE *config_desc_end;

    ANDROID_PROTOCOL_V1_DEVICE_DATA* device = NULL;
    BYTE i;

    device_descriptor = USBHostGetDeviceDescriptor(address);

    ReadWORD(&tempWord, &device_descriptor[USB_DEV_DESC_VID_OFFSET]);

    if(tempWord == 0x18D1)
    {
        ReadWORD(&tempWord, &device_descriptor[USB_DEV_DESC_PID_OFFSET]);
        if((tempWord == 0x2D00) || (tempWord == 0x2D01))
        {
            for(i=0;i<NUM_ANDROID_DEVICES_SUPPORTED;i++)
            {
                if(devices_pv1[i].state == WAITING_FOR_ACCESSORY_RETURN)
                {
                    device = &devices_pv1[i];
                    device->state = RETURN_OF_THE_ACCESSORY;
                    break;
                }
            }
        }
    }

    //if this isn't an old accessory, then it must be a new one
    if(device == NULL)
    {
        //Find the first available device.
        for(i=0;i<NUM_ANDROID_DEVICES_SUPPORTED;i++)
        {
            if(devices_pv1[i].state == NO_DEVICE)
            {
                device = &devices_pv1[i];
                device->state = DEVICE_ATTACHED;
                break;
            }
        }
    }

    config_descriptor = USBHostGetCurrentConfigurationDescriptor( address );

    //Save the total length for this configuration descriptor
    ReadWORD(&tempWord,&config_descriptor[2]);

    //Record the end of the descriptor so we know when to stop searching through
    //  the descriptor list
    config_desc_end = config_descriptor + tempWord;

    //Skip past the configuration part of this descriptor to the next 
    //  descriptor in the configuration descriptor list.  The size of the config
    //  part of the descriptor is the first byte of the list.
    config_descriptor += *config_descriptor;

    //Search the entire configuration descriptor for COMM interfaces
    while(config_descriptor < config_desc_end)
    {
        //We are expecting a interface descriptor
        if(config_descriptor[USB_DESC_BDESCRIPTORTYPE_OFFSET] != USB_DESCRIPTOR_INTERFACE)
        {
            //Jump past this descriptor by adding the current descriptor length
            //  to the current descriptor pointer.
            config_descriptor += config_descriptor[USB_DESC_BLENGTH_OFFSET];

            //Jump back to the top of the while loop to continue searching through
            //  this configuration for the next interface
            continue;
        }

        device->address = address;
        device->clientDriverID = clientDriverID;

        if( (config_descriptor[USB_INTERFACE_DESC_BINTERFACECLASS_OFFSET] == 0xFF) &&
            (config_descriptor[USB_INTERFACE_DESC_BINTERFACESUBCLASS_OFFSET] == 0xFF) &&
            (config_descriptor[USB_INTERFACE_DESC_BINTERFACEPROTOCOL_OFFSET] == 0x00))
        {
            //Jump past this descriptor to the next descriptor.
            config_descriptor += config_descriptor[USB_DESC_BLENGTH_OFFSET];

            //Parse through the rest of this interface.  Stop when we reach the
            //  next interface or the end of the configuration descriptor
            while((config_descriptor[USB_DESC_BDESCRIPTORTYPE_OFFSET] != USB_DESCRIPTOR_INTERFACE) && (config_descriptor < config_desc_end))
            {
                if(config_descriptor[USB_DESC_BDESCRIPTORTYPE_OFFSET] == USB_DESCRIPTOR_ENDPOINT)
                {
                    //If this is an endpoint descriptor in the DATA interface, then
                    //  copy all of the endpoint data to the device information.

                    if((config_descriptor[USB_ENDPOINT_DESC_BENDPOINTADDRESS_OFFSET] & 0x80) == 0x80)
                    {
                        //If this is an IN endpoint, record the endpoint number
                        device->INEndpointNum = config_descriptor[USB_ENDPOINT_DESC_BENDPOINTADDRESS_OFFSET]; 

                        //record the endpoint size (2 bytes)
                        device->INEndpointSize = (config_descriptor[USB_ENDPOINT_DESC_WMAXPACKETSIZE_OFFSET]) + (config_descriptor[USB_ENDPOINT_DESC_WMAXPACKETSIZE_OFFSET+1] << 8);
                    }
                    else
                    {
                        //Otherwise this is an OUT endpoint, record the endpoint number
                        device->OUTEndpointNum = config_descriptor[USB_ENDPOINT_DESC_BENDPOINTADDRESS_OFFSET]; 

                        //record the endpoint size (2 bytes)
                        device->OUTEndpointSize = (config_descriptor[USB_ENDPOINT_DESC_WMAXPACKETSIZE_OFFSET]) + (config_descriptor[USB_ENDPOINT_DESC_WMAXPACKETSIZE_OFFSET+1] << 8);
                    }
                }
                config_descriptor += config_descriptor[USB_DESC_BLENGTH_OFFSET];
            }
        }
        else
        {
            //Jump past this descriptor by adding the current descriptor length
            //  to the current descriptor pointer.
            config_descriptor += config_descriptor[USB_DESC_BLENGTH_OFFSET];
        }
    }

    return device;
}


/****************************************************************************
  Function:
    BOOL AndroidAppDataEventHandler_Pv1( BYTE address, USB_EVENT event, void *data, DWORD size )

  Summary:
    Handles data events from the host stack

  Description:
    Handles data events from the host stack

  Precondition:
    None

  Parameters:
    BYTE address - the address of the device that caused the event
    USB_EVENT event - the event that occured
    void* data - the data for the event
    DWORD size - the size of the data in bytes

  Return Values:
    TRUE - the event was handled
    FALSE - the event was not handled

  Remarks:
    This is a internal API only.  This should not be called by anything other
    than the USB host stack via the client driver table
  ***************************************************************************/
BOOL AndroidAppDataEventHandler_Pv1( BYTE address, USB_EVENT event, void *data, DWORD size )
{
    BYTE i;

    Nop();
    Nop();
    Nop();
    Nop();

    switch (event)
    {
        case EVENT_SOF:              // Start of frame - NOT NEEDED
            Nop();
            Nop();
            Nop();
            return TRUE;
        case EVENT_1MS:              // 1ms timer
            for(i=0;i<NUM_ANDROID_DEVICES_SUPPORTED;i++)
            {
                if(devices_pv1[i].state == WAITING_FOR_ACCESSORY_RETURN)
                {
                    switch(devices_pv1[i].countDown)
                    {
                        case 0:
                            //do nothing
                            break;
                        case 1:
                            //USBHostTerminateTransfer( devices_pv1[i].address, devices_pv1[i].OUTEndpointNum );
                            //USBHostTerminateTransfer( devices_pv1[i].address, devices_pv1[i].INEndpointNum );
                            USB_HOST_APP_EVENT_HANDLER(devices_pv1[i].address,EVENT_ANDROID_DETACH,&devices_pv1[i],sizeof(ANDROID_PROTOCOL_V1_DEVICE_DATA*));

                            //Device has timed out.  Destroy its info.
                            memset(&devices_pv1[i],0x00,sizeof(ANDROID_PROTOCOL_V1_DEVICE_DATA));
                            break;
                        default:
                            //for every other number, decrement the count
                            devices_pv1[i].countDown--;
                            break;
                    }
                }
            }
            return TRUE;
        default:
            Nop();
            Nop();
            Nop();
            break;
    }
    return FALSE;
}

/****************************************************************************
  Function:
    BOOL AndroidAppEventHandler_Pv1( BYTE address, USB_EVENT event, void *data, DWORD size )

  Summary:
    Handles events from the host stack

  Description:
    Handles events from the host stack

  Precondition:
    None

  Parameters:
    BYTE address - the address of the device that caused the event
    USB_EVENT event - the event that occured
    void* data - the data for the event
    DWORD size - the size of the data in bytes

  Return Values:
    TRUE - the event was handled
    FALSE - the event was not handled

  Remarks:
    This is a internal API only.  This should not be called by anything other
    than the USB host stack via the client driver table
  ***************************************************************************/
BOOL AndroidAppEventHandler_Pv1( BYTE address, USB_EVENT event, void *data, DWORD size )
{
    HOST_TRANSFER_DATA* transfer_data = data ;
    ANDROID_PROTOCOL_V1_DEVICE_DATA *device = NULL;
    BYTE i;

    Nop();
    Nop();
    Nop();
    Nop();

    switch (event)
    {
        case EVENT_NONE:             // No event occured (NULL event)
            Nop();
            Nop();
            Nop();
            return TRUE;
        case EVENT_DETACH:           // USB cable has been detached (data: BYTE, address of device)
            for(i=0;i<NUM_ANDROID_DEVICES_SUPPORTED;i++)
            {
                if(devices_pv1[i].address == address)
                {
                    if(devices_pv1[i].state != WAITING_FOR_ACCESSORY_RETURN)
                    {
                        USBHostTerminateTransfer( device->address, device->OUTEndpointNum );
                        USBHostTerminateTransfer( device->address, device->INEndpointNum );
                        USB_HOST_APP_EVENT_HANDLER(device->address,EVENT_ANDROID_DETACH,device,sizeof(ANDROID_PROTOCOL_V1_DEVICE_DATA*));
                        
                        //Device has timed out.  Destroy its info.
                        memset(&devices_pv1[i],0x00,sizeof(ANDROID_PROTOCOL_V1_DEVICE_DATA));
                    }
                    //If we are WAITING_FOR_ACCESSORY_RETURN, then we will timeout in data handler instead
                }
            }

            return TRUE;
        case EVENT_HUB_ATTACH:       // USB hub has been attached
            Nop();
            Nop();
            Nop();
            return TRUE;
        case EVENT_TRANSFER:         // A USB transfer has completed - NOT USED
            Nop();
            Nop();
            Nop();

            for(i=0;i<NUM_ANDROID_DEVICES_SUPPORTED;i++)
            {
                if(devices_pv1[i].address == address)
                {
                    device = &devices_pv1[i];
                }
            }

            //If this is for a device that we don't know about, get rid of it.
            if(device == NULL)
            {
                return FALSE;
            }

            //Otherwise, handle the data
            if(transfer_data->bEndpointAddress == 0x00)
            {
                //If the transfer was EP0, just clear the pending bit and 
                //  we will handle the rest in the tasks function so we don't
                //  duplicate state machine changes both here and there
                device->status.EP0TransferPending = 0;
            }
            
            return TRUE;
        case EVENT_RESUME:           // Device-mode resume received
            Nop();
            Nop();
            Nop();
            return TRUE;
        case EVENT_SUSPEND:          // Device-mode suspend/idle event received
            Nop();
            Nop();
            Nop();
            return TRUE;
        case EVENT_RESET:            // Device-mode bus reset received
            Nop();
            Nop();
            Nop();
            return TRUE;
        case EVENT_STALL:            // A stall has occured
            Nop();
            Nop();
            Nop();
            return TRUE;
        case EVENT_BUS_ERROR:            // BUS error has occurred
            Nop();
            Nop();
            Nop();
            return TRUE;
        default:
            Nop();
            Nop();
            Nop();
            break;
    }
    return FALSE;
}

/********************************************************************/
/**     Internal Functions                                         **/
/********************************************************************/

/****************************************************************************
  Function:
    static BYTE AndroidCommandSendString_Pv1(void *handle, ANDROID_ACCESSORY_STRINGS stringType, const char *string, WORD stringLength)

  Summary:
    Sends a command String to the Android device using the EP0 command 

  Description:
    Sends a command String to the Android device using the EP0 command 

  Precondition:
    None

  Parameters:
    void* handle - the device to send the message to
    ANDROID_ACCESSORY_STRINGS stringType - the type of string message being sent
    const char* string - the string data being sent
    WORD stringLength - the length of the string

  Return Values:
    USB_SUCCESS                 - Request processing started
    USB_UNKNOWN_DEVICE          - Device not found
    USB_INVALID_STATE           - The host must be in a normal running state
                                    to do this request
    USB_ENDPOINT_BUSY           - A read or write is already in progress
    USB_ILLEGAL_REQUEST         - SET CONFIGURATION cannot be performed with
                                    this function.

  Remarks:
    This is a internal API only.
  ***************************************************************************/
static BYTE AndroidCommandSendString_Pv1(void *handle, ANDROID_ACCESSORY_STRINGS stringType, const char *string, WORD stringLength)
{
    ANDROID_PROTOCOL_V1_DEVICE_DATA* device = (ANDROID_PROTOCOL_V1_DEVICE_DATA*)handle;

    return USBHostIssueDeviceRequest (  device->address,                    //BYTE deviceAddress, 
                                        USB_SETUP_HOST_TO_DEVICE            //BYTE bmRequestType,
                                            | USB_SETUP_TYPE_VENDOR 
                                            | USB_SETUP_RECIPIENT_DEVICE,       
                                        ANDROID_ACCESSORY_SEND_STRING,      //BYTE bRequest,
                                        0,                                  //WORD wValue, 
                                        (WORD)stringType,                   //WORD wIndex, 
                                        stringLength,                       //WORD wLength, 
                                        (BYTE*)string,                      //BYTE *data, 
                                        USB_DEVICE_REQUEST_SET,             //BYTE dataDirection,
                                        device->clientDriverID              //BYTE clientDriverID 
                                      );
}

/****************************************************************************
  Function:
    static BYTE AndroidCommandStart_Pv1(void *handle)

  Summary:
    Sends a the start command that makes the Android device go into accessory mode 

  Description:
    Sends a the start command that makes the Android device go into accessory mode

  Precondition:
    None

  Parameters:
    void* handle - the device entering accessory mode

  Return Values:
    USB_SUCCESS                 - Request processing started
    USB_UNKNOWN_DEVICE          - Device not found
    USB_INVALID_STATE           - The host must be in a normal running state
                                    to do this request
    USB_ENDPOINT_BUSY           - A read or write is already in progress
    USB_ILLEGAL_REQUEST         - SET CONFIGURATION cannot be performed with
                                    this function.

  Remarks:
    This is a internal API only.
  ***************************************************************************/
static BYTE AndroidCommandStart_Pv1(void *handle)
{
    ANDROID_PROTOCOL_V1_DEVICE_DATA* device = (ANDROID_PROTOCOL_V1_DEVICE_DATA*)handle;

    return USBHostIssueDeviceRequest (  device->address,                    //BYTE deviceAddress, 
                                        USB_SETUP_HOST_TO_DEVICE            //BYTE bmRequestType,
                                            | USB_SETUP_TYPE_VENDOR 
                                            | USB_SETUP_RECIPIENT_DEVICE,       
                                        ANDROID_ACCESSORY_START,            //BYTE bRequest,
                                        0,                                  //WORD wValue, 
                                        0,                                  //WORD wIndex, 
                                        0,                                  //WORD wLength, 
                                        NULL,                               //BYTE *data, 
                                        USB_DEVICE_REQUEST_SET,             //BYTE dataDirection,
                                        device->clientDriverID              //BYTE clientDriverID 
                                      );
}

//DOM-IGNORE-END
