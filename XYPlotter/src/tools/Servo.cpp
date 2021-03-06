#include "Servo.h"
#include "chip.h"

#define PENUP 1640 // initial values
#define PENDOWN 1365

Servo::Servo(int port, int pin)
{
		Chip_SCT_Init(LPC_SCT0);
		Chip_SWM_MovablePortPinAssign(SWM_SCT0_OUT0_O, port, pin);

		LPC_SCT0->CONFIG |= (1 << 17); // Two 16-bit timers, auto limit
		LPC_SCT0->CTRL_L |= (72 - 1) << 5;// Set prescaler, SCTimer/PWM Clock = 1 Mhz

		LPC_SCT0->MATCHREL[0].L = 20000 - 1;// Match 0 @ 1000/1MHz = (1 kHz PWM freq)

		LPC_SCT0->EVENT[0].STATE = 0xFFFFFFFF;// Event 0 happens in all states
		LPC_SCT0->EVENT[0].CTRL = (1 << 12);// Match 0 condition only

		LPC_SCT0->EVENT[1].STATE = 0xFFFFFFFF;// Event 1 happens in all state
		LPC_SCT0->EVENT[1].CTRL = (1 << 0) | (1 << 12);// Match 1 condition only

		LPC_SCT0->OUT[0].SET = (1 << 0);// Event 0 will set SCTx_OUT0
		LPC_SCT0->OUT[0].CLR = (1 << 1);// Event 1 will clear SCTx_OUT0

		LPC_SCT0->CTRL_L &= ~(1 << 2);// Unhalt by clearing bit 2 of CTRL req

		servoPos = PENUP;
		LPC_SCT0->MATCHREL[1].L = PENUP;
}

void Servo::Stop() {
	while(LPC_SCT0->MATCHREL[1].L <= PENUP) {
		LPC_SCT0->MATCHREL[1].L  = ++servoPos; // need to decrement by 1 for rservo to lower smoothly and that task is not complete without pen beign down
		vTaskDelay(1);
	}
}

void Servo::Draw() {
	while(LPC_SCT0->MATCHREL[1].L >= PENDOWN) {
		LPC_SCT0->MATCHREL[1].L  = --servoPos;
		vTaskDelay(1);
	}
}
int Servo::getPenDownValue() {
	return penDOWN;
}

int Servo::getPenUpValue() {
	return penUP;
}

void Servo::setPenUpValue(int value) {
	penUP = value;
}

void Servo::setPenDownValue(int value) {
	penDOWN = value;
}
