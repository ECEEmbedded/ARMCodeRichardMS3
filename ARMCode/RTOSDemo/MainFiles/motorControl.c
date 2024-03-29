#include "myDefs.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "projdefs.h"
#include "semphr.h"

/* include files. */
#include "vtUtilities.h"
#include "i2c_ARM.h"
#include "myTypes.h"
#include "motorControl.h"
#include "webServer.h"
#include "LCDtask.h"
/* *********************************************** */

#define INSPECT_STACK 1
#define baseStack 2
#if PRINTF_VERSION == 1
#define conSTACK_SIZE       ((baseStack+5)*configMINIMAL_STACK_SIZE)
#else
#define conSTACK_SIZE       (baseStack*configMINIMAL_STACK_SIZE)
#endif

// Length of the queue to this task
#define motorControlQLen 10
// actual data structure that is sent in a message
typedef struct __motorControlMsg {
    uint8_t msgType;
    uint8_t length;  // Length of the message
    uint8_t buf[maxMotorMsgLen+1]; // On the way in, message to be sent, on the way out, message received (if any)
} motorControlMsg;

/* The Motor Control task. */
static portTASK_FUNCTION_PROTO( vMotorControlTask, pvParameters );

/*-----------------------------------------------------------*/
// Public API
void vStartMotorControlTask(motorControlStruct *params, unsigned portBASE_TYPE uxPriority, myI2CStruct *myi2c, webServerStruct *webData, vtLCDStruct *lcdData)
{
    // Create the queue that will be used to talk to this task
    if ((params->inQ = xQueueCreate(motorControlQLen,sizeof(motorControlMsg))) == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    /* Start the task */
    portBASE_TYPE retval;
    params->i2cData = myi2c;
    params->webData = webData;
    params->lcdData = lcdData;
    if ((retval = xTaskCreate( vMotorControlTask, ( signed char * ) "Motor Control", conSTACK_SIZE, (void *) params, uxPriority, ( xTaskHandle * ) NULL )) != pdPASS) {
        VT_HANDLE_FATAL_ERROR(TASK_CREATION_ERROR);
    }
}

portBASE_TYPE sendMotorSetDirForward(motorControlStruct *motorControlData)
{
    if (motorControlData == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    motorControlMsg buffer;
    buffer.length = 0;
    if (buffer.length > maxMotorMsgLen) {
        // no room for this message
        VT_HANDLE_FATAL_ERROR(INCORRECT_MOTOR_CONTROL_MSG_FORMAT);
    }
    buffer.msgType = setDirForwardMsgType;
    return(xQueueSend(motorControlData->inQ,(void *) (&buffer),portMAX_DELAY));
}

portBASE_TYPE sendMotorSetDirReverse(motorControlStruct *motorControlData)
{
    if (motorControlData == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    motorControlMsg buffer;
    buffer.length = 0;
    if (buffer.length > maxMotorMsgLen) {
        // no room for this message
        VT_HANDLE_FATAL_ERROR(INCORRECT_MOTOR_CONTROL_MSG_FORMAT);
    }
    buffer.msgType = setDirReverseMsgType;
    return(xQueueSend(motorControlData->inQ,(void *) (&buffer),portMAX_DELAY));
}

portBASE_TYPE sendMotorSetSpeed(motorControlStruct *motorControlData, uint8_t speed)
{
    if (motorControlData == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    motorControlMsg buffer;
    buffer.length = 1;
    if (buffer.length > maxMotorMsgLen) {
        // no room for this message
        VT_HANDLE_FATAL_ERROR(INCORRECT_MOTOR_CONTROL_MSG_FORMAT);
    }
    buffer.buf[0] = speed;
    buffer.msgType = setMotorSpeedMsgType;
    return(xQueueSend(motorControlData->inQ,(void *) (&buffer),portMAX_DELAY));
}

portBASE_TYPE sendMotorTurnLeft(motorControlStruct *motorControlData, uint8_t mag)
{
    if (motorControlData == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    motorControlMsg buffer;
    buffer.length = 1;
    if (buffer.length > maxMotorMsgLen) {
        // no room for this message
        VT_HANDLE_FATAL_ERROR(INCORRECT_MOTOR_CONTROL_MSG_FORMAT);
    }
    buffer.buf[0] = mag;
    buffer.msgType = turnLeftMsgType;
    return(xQueueSend(motorControlData->inQ,(void *) (&buffer),portMAX_DELAY));
}

portBASE_TYPE sendMotorTurnRight(motorControlStruct *motorControlData, uint8_t mag)
{
    if (motorControlData == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    motorControlMsg buffer;
    buffer.length = 1;
    if (buffer.length > maxMotorMsgLen) {
        // no room for this message
        VT_HANDLE_FATAL_ERROR(INCORRECT_MOTOR_CONTROL_MSG_FORMAT);
    }
    buffer.buf[0] = mag;
    buffer.msgType = turnRightMsgType;
    return(xQueueSend(motorControlData->inQ,(void *) (&buffer),portMAX_DELAY));
}

portBASE_TYPE sendMotorStop(motorControlStruct *motorControlData)
{
    if (motorControlData == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    motorControlMsg buffer;
    buffer.length = 0;
    if (buffer.length > maxMotorMsgLen) {
        // no room for this message
        VT_HANDLE_FATAL_ERROR(INCORRECT_MOTOR_CONTROL_MSG_FORMAT);
    }
    buffer.msgType = motorStopMsgType;
    return(xQueueSend(motorControlData->inQ,(void *) (&buffer),portMAX_DELAY));
}

portBASE_TYPE conductorSendMotorEncoderDataMsg(motorControlStruct *motorControlData, uint8_t *data, uint8_t length)
{
    if (motorControlData == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    motorControlMsg buffer;
    buffer.length = length;
    if (buffer.length > maxMotorMsgLen) {
        // no room for this message
        VT_HANDLE_FATAL_ERROR(INCORRECT_MOTOR_CONTROL_MSG_FORMAT);
    }
    memcpy(buffer.buf,data,length);
    buffer.msgType = encoderDataMsgType;
    return(xQueueSend(motorControlData->inQ,(void *) (&buffer),portMAX_DELAY));
}

portBASE_TYPE sendMotorTimerMsg(motorControlStruct *motorData, portTickType ticksElapsed, portTickType ticksToBlock)
{
    if (motorData == NULL) {
        VT_HANDLE_FATAL_ERROR(0);
    }
    motorControlMsg buffer;
    buffer.length = sizeof(ticksElapsed);
    if (buffer.length > maxMotorMsgLen) {
        // no room for this message
        VT_HANDLE_FATAL_ERROR(buffer.length);
    }
    memcpy(buffer.buf,(char *)&ticksElapsed,sizeof(ticksElapsed));
    buffer.msgType = motorTimerMsgType;
    return(xQueueSend(motorData->inQ,(void *) (&buffer),ticksToBlock));
}

// End of Public API
/*-----------------------------------------------------------*/

// #define constants

// Motor and Encoder constants
#define COUNTS_PER_CENTIMETER 30
#define COUNTS_PER_DEGREE 2    // 10
#define TIMER_COUNTS_PER_CENTIMETER 1
#define DEGREES_PER_TIMER_COUNT 1.0     // 1.9
#define MOTOR_FORWARD_SPEED 34
#define MOTOR_BACKWARD_SPEED 94
#define MOTOR_STOP_SPEED 64
#define RIGHT_MOTOR_OFFSET 128

// Operation constants
#define NONE 0
#define FORWARD 1
#define RIGHT 2
#define LEFT 3
#define REVERSE 4

// #define SEND_COUNTS_TO_LCD

// Private routines used to unpack the message buffers.
// I do not want to access the message buffer data structures outside of these routines.
// These routines are specific to accessing our packet protocol from the task struct.

// For accessing data sent between ARM local tasks:

// When sendMotorSetSpeed type, speed is in buf[0]
uint8_t getNewSpeed(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[0];
}

// When sendMotorTurnLeft type, magnitude is in buf[0]
uint8_t getLeftTurnMag(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[0];
}

// When sendMotorTurnRight type, magnitude is in buf[0]
uint8_t getRightTurnMag(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[0];
}

// For accessing data sent between Rover PIC(s) and the ARM:

// When conductor has to manage a task and send it to the
// conductorSendMotorEncoderDataMsg task and the message has the
// encoderDataMsgType type, then the next 8 functions should be used.
uint8_t getPcktProtoID(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[0];
}

uint8_t getPcktProtoSensorNum(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[1];
}

uint8_t getPcktProtoParity(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[2];
}

uint8_t getPcktProtoCount(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[3];
}

uint8_t getPcktProtoData1(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[4];
}

uint8_t getPcktProtoData2(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[5];
}

uint8_t getPcktProtoData3(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[6];
}

uint8_t getPcktProtoData4(motorControlMsg *motorControlBuf){
    return motorControlBuf->buf[7];
}

// End of private routines for message buffers
/*-----------------------------------------------------------*/

// Private routines used for data manipulation, etc.
// There should be NO accessing of our packet protocol from the task struct in these routines.

// This makes the code a little more readable
uint8_t getLeftCount(motorControlMsg *buffer){
    return getPcktProtoData1(buffer);
}

// This makes the code a little more readable as well
uint8_t getRightCount(motorControlMsg *buffer){
    return getPcktProtoData2(buffer);
}

int getMsgType(motorControlMsg *motorControlBuf)
{
    return(motorControlBuf->msgType);
}

uint8_t getDegrees(unsigned int right,unsigned int left){
    return ((right + left)/2)/COUNTS_PER_DEGREE;
}

uint8_t getCentimeters(unsigned int right,unsigned int left){
    return ((right + left)/2)/COUNTS_PER_CENTIMETER;
}

// End of private routines for data manipulation, etc.
/*-----------------------------------------------------------*/

static uint8_t currentOp, lastOp;
static motorControlStruct *param;
static myI2CStruct *i2cData;
static webServerStruct *webData;
static vtLCDStruct *lcdData;

// Buffer for receiving messages
static motorControlMsg msgBuffer;
static unsigned int leftEncoderCount, rightEncoderCount;
// static unsigned int targetVal;

// static unsigned int forward, backward, right, left;

// static uint8_t delay;

// static char msg[12];

// This is the actual task that is run
static portTASK_FUNCTION( vMotorControlTask, pvParameters )
{
    // Get the parameters
    param = (motorControlStruct *) pvParameters;
    // Get the I2C task pointer
    i2cData = param->i2cData;
    // Get the Navigation task pointer
    webData = param->webData;
    // Get the LCD task pointer
    lcdData = param->lcdData;

    currentOp = NONE;
    lastOp = NONE;
    leftEncoderCount = 0;
    rightEncoderCount = 0;

    // Like all good tasks, this should never exit
    for(;;)
    {
        // Wait for a message from the I2C (Encoder data) or from the Navigation Task (motor command)
        if (xQueueReceive(param->inQ,(void *) &msgBuffer,portMAX_DELAY) != pdTRUE) {
            VT_HANDLE_FATAL_ERROR(Q_RECV_ERROR);
        }
        switch(getMsgType(&msgBuffer)){
			case motorTimerMsgType:
			{
				break;
			}
            case setDirForwardMsgType:
            {
                currentOp = FORWARD;
                lastOp = FORWARD;
                leftEncoderCount = 0;
                rightEncoderCount = 0;
                //targetVal = getTargetVal(&msgBuffer)*TIMER_COUNTS_PER_CENTIMETER;
                //sendi2cMotorMsg(i2cData,MOTOR_FORWARD_SPEED + RIGHT_MOTOR_OFFSET,MOTOR_FORWARD_SPEED, portMAX_DELAY);
                break;
            }
			case setDirReverseMsgType:
            {
                break;
            }
            case setMotorSpeedMsgType:
            {
                break;
            }
            case turnLeftMsgType:
            {
                break;
            }
            case turnRightMsgType:
            {
                break;
            }
            case motorStopMsgType:
            {
                currentOp = NONE;
                //sendi2cMotorMsg(i2cData,MOTOR_STOP_SPEED + RIGHT_MOTOR_OFFSET,MOTOR_STOP_SPEED, portMAX_DELAY);
                break;
            }
            case encoderDataMsgType:
            {
                rightEncoderCount += getRightCount(&msgBuffer);
                leftEncoderCount += getLeftCount(&msgBuffer);
                break;
            }
            default:
            {
                VT_HANDLE_FATAL_ERROR(UNKNOWN_MOTOR_CONTROL_MSG_TYPE);
                break;
            }
        }
    }
}
