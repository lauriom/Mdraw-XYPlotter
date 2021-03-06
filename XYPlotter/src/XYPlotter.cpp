/*
 ===============================================================================
 Name        : main.c
 Author      : Lauri.M Tuomas.A Taneli.T
 Version     :
 Copyright   : $(copyright)
 Description : main definition
 ===============================================================================
 */

#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "IoPinInterupts.c"

#include "../inc/user_vcom.h"
#include "tools/Laser.h"
#include "tools/Servo.h"
#include "tools/Parser.h"
#include "StepperController.h"

QueueHandle_t q_cmd;

static void prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();

	DigitalIoPin lazer(0,12,DigitalIoPin::output);
	lazer.write(false);

	Chip_IOCON_PinMuxSet(LPC_IOCON, 1, 3,
			(IOCON_DIGMODE_EN | IOCON_MODE_INACT | IOCON_MODE_PULLUP ) );
	Chip_IOCON_PinMuxSet(LPC_IOCON, 0, 0,
			(IOCON_DIGMODE_EN | IOCON_MODE_INACT | IOCON_MODE_PULLUP ) );
	Chip_IOCON_PinMuxSet(LPC_IOCON, 0, 9,
			(IOCON_DIGMODE_EN | IOCON_MODE_INACT | IOCON_MODE_PULLUP ) );
	Chip_IOCON_PinMuxSet(LPC_IOCON, 0, 29,
			(IOCON_DIGMODE_EN | IOCON_MODE_INACT | IOCON_MODE_PULLUP ) );

	Chip_INMUX_PinIntSel(0, 1, 3); // LIM1
	Chip_INMUX_PinIntSel(1, 0, 0); // LIM2
	Chip_INMUX_PinIntSel(2, 0, 9);  // LIM3
	Chip_INMUX_PinIntSel(3, 0, 29);  // LIM4


	Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0)| PININTCH(1) | PININTCH(2) | PININTCH(3));
	Chip_PININT_SetPinModeEdge(LPC_GPIO_PIN_INT, PININTCH(0)| PININTCH(1) | PININTCH(2) | PININTCH(3));
	Chip_PININT_EnableIntLow  (LPC_GPIO_PIN_INT, PININTCH(0)| PININTCH(1) | PININTCH(2) | PININTCH(3));

	NVIC_SetPriority(PIN_INT0_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY+1);
	NVIC_SetPriority(PIN_INT1_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY+1);
	NVIC_SetPriority(PIN_INT2_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY+1);
	NVIC_SetPriority(PIN_INT3_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY+1);
	/* Initial LED0 state is off */
	Board_LED_Set(0, false);
}


/*****************************************************************************
 * Public functions
 ****************************************************************************/

/* the following is required if runtime statistics are to be collected */
extern "C" {

void vConfigureTimerForRunTimeStats(void) {
	Chip_SCT_Init(LPC_SCTSMALL1);
	LPC_SCTSMALL1->CONFIG = SCT_CONFIG_32BIT_COUNTER;
	LPC_SCTSMALL1->CTRL_U = SCT_CTRL_PRE_L(255) | SCT_CTRL_CLRCTR_L; // set prescaler to 256 (255 + 1), and start timer
}

}
/* end runtime statictics collection */


static void DrawTask(void *pvParameters) {
  char msg[80] = "M10 XY 380 310 0.00 0.00 A0 B0 H0 S80 U160 D90\r\n"; //Default values
	char OK[6] = "OK\r\n";
	
	StepperController stepper;
	Servo pen(0, 10);
  Laser laser(0,12);
	Command cmd;
  bool LimitSwitchStatus[4];

	vTaskDelay(1000);

	LimitSwitchStatus = stepper.getLimitSwitchStatus();
	while (LimitSwitchStatus[0] == false && LimitSwitchStatus[1] == false && 		//Start calibration when all limit switches are open
           LimitSwitchStatus[2] == false && LimitSwitchStatus[3] == false) {
		LimitSwitchStatus = stepper.getLimitSwitchStatus();
		vTaskDelay(100);
	}
	stepper.calibrate();

	while (1) {
		xQueueReceive(q_cmd, &cmd, portMAX_DELAY);

		if (cmd.type == COMMAND_MOVE) {
			stepper.move((int) (stepper.getSPMM()*cmd.x - stepper.getX()),
						 (int) (stepper.getSPMM()*cmd.y - stepper.getY()));

		} else if (cmd.type == COMMAND_PEN) {
			if (cmd.penvalue == pen.getPenDownValue()) {
				pen.Draw();
			} else if (cmd.penvalue == pen.getPenUpValue()) {
				pen.Stop();
			}
		} else if (cmd.type == COMMAND_SAVEPEN) {
			pen.setPenDownValue(cmd.penDOWN);
			pen.setPenUpValue(cmd.penUP);
		} else if (cmd.type == COMMAND_LASER) {
				laser.setVal(cmd.laservalue);
				vTaskDelay(250);
		} else if (cmd.type == COMMAND_ORIGIN) {
			stepper.move(((int)0 - stepper.getX()), (int) (0 - stepper.getY()));


		} else if (cmd.type == COMMAND_SET_DIR_AND_AREA_SPEED) {
			stepper.setWidth(cmd.width);
			stepper.setHeight(cmd.height);
			stepper.calibrate();
		} else if (cmd.type == COMMAND_LSQUERY) {
			LimitSwitchStatus = stepper.getLimitSwitchStatus();
			sprintf(msg, "M11 %d %d %d %d\r\n", *LimitSwitchStatus,*(LimitSwitchStatus+1),
												*(LimitSwitchStatus+2), *(LimitSwitchStatus+3));
			USB_send((uint8_t *)msg, strlen(msg));
		} else if (cmd.type == COMMAND_START) {
			sprintf(msg, "M10 XY %d %d 0.00 0.00 A0 B0 H0 S80 U%d D%d\r\n",
					      stepper.getWidth(), stepper.getHeight(),
						  pen.getPenUpValue(), pen.getPenDownValue());
			USB_send((uint8_t *)msg, strlen(msg));
			startLimSwitchCheck();
		}
		USB_send((uint8_t *)OK, strlen(OK));
		vTaskDelay(10);
	}
}
/**
 * @brief
 * @return	Nothing, function should not exit
 */



static void CommandParseTask(void *pvParameters) {
	Parser parse;
	Command cmd;

	vTaskDelay(100);
	while (1) {
		char str[80];
		uint32_t len = USB_receive((uint8_t *)str, 79);

		str[len] = 0;
		Command cmd = parse.parse(str);

		if (cmd.type != INVALID_COMMAND) {
			xQueueSendToBack(q_cmd, &cmd, portMAX_DELAY);
		} else Board_LED_Set(1, true);
		vTaskDelay(20);
	}
}


int main(void) {
	prvSetupHardware();
	q_cmd = xQueueCreate(5, sizeof(Command));

	xTaskCreate(DrawTask, "stepperTask",
			configMINIMAL_STACK_SIZE * 4, NULL, (tskIDLE_PRIORITY + 1UL),
			(TaskHandle_t *) NULL);

	xTaskCreate(CommandParseTask, "Uart Task",
			configMINIMAL_STACK_SIZE * 5, NULL, (tskIDLE_PRIORITY + 1UL),
			(TaskHandle_t *) NULL);

	xTaskCreate(cdc_task, "CDC",
			configMINIMAL_STACK_SIZE * 3, NULL, (tskIDLE_PRIORITY + 1UL),
			(TaskHandle_t *) NULL);


	vTaskStartScheduler();
	/* Should never arrive here */

	while(1);

	return 0 ;

}
