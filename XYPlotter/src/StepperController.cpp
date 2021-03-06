/*
 * ServoController.cpp
 *
 *  Created on: 28 Sep 2020
 *      Author: Lauri
 */

#include "StepperController.h"

volatile uint32_t MRT_count;
SemaphoreHandle_t sbRIT;

#include "assert.h"

DigitalIoPin *limSW1, *limSW2,*limSW3,*limSW4,*xMotor,*xMotorDir,*yMotor,*yMotorDir;

extern "C" {


void MRT_IRQHandler(void)
{
	uint32_t int_pend;

	portBASE_TYPE xHigherPriorityWoken = pdFALSE;

	/* Get and clear interrupt pending status for all timers */
	int_pend = Chip_MRT_GetIntPending();
	Chip_MRT_ClearIntPending(int_pend);

	if(MRT_count > 0) {

		/* Channel 0 */
		if (int_pend & MRTn_INTFLAG(0)) {
			--MRT_count;
			xMotor->flip();
		}
		/* Channel 1 */
		if (int_pend & MRTn_INTFLAG(1)) {
			--MRT_count;
			yMotor->flip();

		}

	} else {
		// disable timers

		Chip_SYSCTL_PeriphReset(RESET_MRT); // disabling MRT channel dosn't work, need to reset it...
/*
		Chip_MRT_SetDisabled(Chip_MRT_GetRegPtr(0));
		Chip_MRT_SetDisabled(Chip_MRT_GetRegPtr(1));
*/

		// Give semaphore and set context switch flag if a higher priority task was woken up
		xSemaphoreGiveFromISR(sbRIT, &xHigherPriorityWoken);
	}
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}

}
/* Setup a timer for a periodic (repeat mode) rate */

static void setupMRT(uint8_t ch, MRT_MODE_T mode, uint32_t rate)
{
	LPC_MRT_CH_T *pMRT;

	/* Get pointer to timer selected by ch */
	pMRT = Chip_MRT_GetRegPtr(ch);

	/* Setup timer with rate based on MRT clock */
	Chip_MRT_SetInterval(pMRT, (Chip_Clock_GetSystemClockRate() / rate) |
			MRT_INTVAL_LOAD);

	/* Timer mode */
	Chip_MRT_SetMode(pMRT, mode);


	/* Clear pending interrupt and enable timer */

	Chip_MRT_IntClear(pMRT);
	Chip_MRT_SetEnabled(pMRT);
}

void MRT_start(int xCount, int yCount) {

	for (int mrtch = 0; mrtch < MRT_CHANNELS_NUM; mrtch++) {
		Chip_MRT_SetDisabled(Chip_MRT_GetRegPtr(mrtch));
	}


	MRT_count = xCount + yCount; // ammount of motor movements needed

	MRT_count *= 2; // increment by 2 to account for IoPin->flip() on off cycle

	NVIC_EnableIRQ(MRT_IRQn);


	if ((xCount != 0) && (yCount != 0)){// check if one number is zero
		// move both motors at the same time
		float ratio =  (float)yCount/(float)xCount;

		setupMRT(0, MRT_MODE_REPEAT, 1000);/* n Hz rate */
		setupMRT(1, MRT_MODE_REPEAT, 1000*ratio);/* n * ratio Hz rate */


	}else {
		// move one dir
		if(yCount == 0){
			setupMRT(0, MRT_MODE_REPEAT, 1000);
		}else{
			setupMRT(1, MRT_MODE_REPEAT, 1000);
		}

	}
	// wait for ISR to tell that we're done
	if(xSemaphoreTake(sbRIT, portMAX_DELAY) == pdTRUE) {
		// Disable the interrupt signal in NVIC (the interrupt controller)
		NVIC_DisableIRQ(MRT_IRQn);


	} else {
		// unexpected error
	}
}

#define simulator

StepperController::StepperController() {

	//Chip_RIT_Init(LPC_RITIMER);

	Chip_MRT_Init();

	sbRIT = xSemaphoreCreateBinary();

#ifndef simulator
	// plotter

	limSW1 = new DigitalIoPin(1,3,DigitalIoPin::pullup);
	limSW2 = new DigitalIoPin(0,0,DigitalIoPin::pullup);
	limSW3 = new DigitalIoPin(0,9,DigitalIoPin::pullup);
	limSW4 = new DigitalIoPin(0,29,DigitalIoPin::pullup);

#else
	// simulator

	limSW1 = new DigitalIoPin(0,9,DigitalIoPin::pullup);
	limSW2 = new DigitalIoPin(0,29,DigitalIoPin::pullup);
	limSW3 = new DigitalIoPin(0,0,DigitalIoPin::pullup);
	limSW4 = new DigitalIoPin(1,3,DigitalIoPin::pullup);


#endif

	xMotor = new DigitalIoPin(0,24,DigitalIoPin::output);
	xMotorDir = new DigitalIoPin(1,0,DigitalIoPin::output);

	yMotor = new DigitalIoPin(0,27,DigitalIoPin::output);
	yMotorDir = new DigitalIoPin(0,28,DigitalIoPin::output);
}

StepperController::~StepperController() {

	delete limSW1;
	delete limSW1;
	delete limSW1;
	delete limSW1;

	delete xMotor;
	delete xMotorDir;
	delete yMotor;
	delete yMotorDir;
	vSemaphoreDelete(sbRIT);
}

/*
 * Moves the servo motors +-xy dir,
 * @Param xSteps: steps to move in xAxis, takes positive or negative number
 * @Param ySteps: steps to move in xAxis, takes positive or negative number
 */
void StepperController::move(signed int xSteps,signed int ySteps){

	update_cor(xSteps, ySteps);

	if(xSteps > 0){
		xMotorDir->write(1);
	}else{
		xSteps *= -1; // reverse number and set motor dir
		xMotorDir->write(0);
	}
	if(ySteps > 0){
		yMotorDir->write(1);
	}else {
		ySteps *= -1; /// reverse number and set motor dir
		yMotorDir->write(0);
	}


	MRT_start(xSteps, ySteps);

}

void StepperController::calibrate(){
	bool calibrated = false;
	int dir = 1;
	int times = 0;
	int calibState = toOrigin;

	while (!calibrated)
	{

		switch(calibState)
		{
			case toOrigin:
				while (limSW2->read()) //move x until hit limit switch
					move(dir,0);
				while (limSW4->read()) //move y until hit limit switch
					move(0,dir);
				totalStepX = 0;
				totalStepY = 0;
				calibState = xAxis;
				dir *= -1;

				break;
			case xAxis:									// xAxis calibration
				while (times < 2) // do 2 runs
				{
					move(dir, 0);
					totalStepX++;
					if (!limSW1->read() || !limSW2->read()) //if hit switch
					{
						dir *= -1; //change dir
						times++;
					}
				}
				xSteps = totalStepX / 2; //get average

				times = 0; //reset counter
				calibState = yAxis; //move to yAxis
				break;
			case yAxis:								// yAxis Calibration
				while (times < 2)
				{
					move(0, dir);
					totalStepY++;
					if (!limSW3->read() || !limSW4->read()) //if hit switch
					{
						dir *= -1; //change dir
						times++;
					}
				}
				ySteps = totalStepY / 2; // get average
				stepsPerMM = ySteps / height;
				CordinateX = xSteps;
				CordinateY = ySteps;
				calibrated = true;
				break;
		}

	}
}

void StepperController::update_cor(int x, int y) {
	CordinateX += x;
	CordinateY += y;
}

int StepperController::getY() {
	return CordinateY;
}

int StepperController::getX() {
	return CordinateX;
}

int StepperController::getSPMM() {
	return stepsPerMM;
}

int StepperController::getWidth() {
	return width;
}

int StepperController::getHeight() {
	return height;
}

void StepperController::setWidth(int TO_width) {
	width = TO_width;
}

void StepperController::setHeight(int TO_height) {
	height = TO_height;
}

bool* StepperController::getLimitSwitchStatus() {
	LimitSwitchStatus[0] = limSW1->read();
	LimitSwitchStatus[1] = limSW2->read();
	LimitSwitchStatus[2] = limSW3->read();
	LimitSwitchStatus[3] = limSW4->read();
	return LimitSwitchStatus;
}
