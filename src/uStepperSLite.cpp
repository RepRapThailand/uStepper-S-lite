/********************************************************************************************
* 	 	File: 		uStepper.cpp															*
*		Version:    1.3.0                                           						*
*      	date: 		January 10th, 2018 	                                    				*
*      	Author: 	Thomas Hørring Olsen                                   					*
*                                                   										*	
*********************************************************************************************
*			            uStepper class 					   									*
* 																							*
*	This file contains the implementation of the class methods, incorporated in the  		*
*	uStepper arduino library. The library is used by instantiating an uStepper object 		*
*	by calling either of the two overloaded constructors: 									*
*																							*
*		example:																			*
*																							*
*		uStepper stepper; 																	*
*																							*
*		OR 																					*
*																							*
*		uStepper stepper(500, 2000);														*
*																							*
*	The first instantiation above creates a uStepper object with default acceleration 		*
*	and maximum speed (1000 steps/s^2 and 1000steps/s respectively).						*
*	The second instantiation overwrites the default settings of acceleration and 			*
*	maximum speed (in this case 500 steps/s^2 and 2000 steps/s, respectively);				*
*																							*
*	after instantiation of the object, the object setup function should be called within 	*
*	arduino's setup function:																*
*																							*
*		example:																			*
*																							*
*		uStepper stepper;																	*
*																							*
*		void setup()																		*
*		{																					*
*			stepper.setup();																*
*		} 																					*
*																							*
*		void loop()																			*
*		{																					*
*																							*
*		}																					*
*																							*
*	After this, the library is ready to control the motor!									*
*																							*
*********************************************************************************************
*	(C) 2018																				*
*																							*
*	uStepper ApS																			*
*	www.ustepper.com 																		*
*	administration@ustepper.com 															*
*																							*
*	The code contained in this file is released under the following open source license:	*
*																							*
*			Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International			*
* 																							*
* 	The code in this file is provided without warranty of any kind - use at own risk!		*
* 	neither uStepper ApS nor the author, can be held responsible for any damage				*
* 	caused by the use of the code contained in this file ! 									*
*                                                                                           *
********************************************************************************************/
/**
 * @file uStepper.cpp
 * @brief      Class implementations for the uStepper library
 *
 *             This file contains the implementations of the classes defined in
 *             uStepper.h
 *
 * @author     Thomas Hørring Olsen (thomas@ustepper.com)
 */
#include <uStepperSLite.h>
#include <math.h>
uStepperSLite *pointer;
i2cMaster I2C(1);
float debug[5];
extern "C" {

	void interrupt1(void)
	{
		if(PIND & 0x04)
		{
			PORTD |= (1 << 4);
		}
		else
		{
			PORTD &= ~(1 << 4);
		}
	}

	void interrupt0(void)
	{
		if(PIND & 0x04)
		{
			PORTD |= (1 << 4);
		}
		else
		{
			PORTD &= ~(1 << 4);
		}
		if((PINB & (0x08)))			//CCW
		{	
			pointer->stepCnt--;				//DIR is set to CCW, therefore we subtract 1 step from step count (negative values = number of steps in CCW direction from initial postion)
		}
		else						//CW
		{
			pointer->stepCnt++;			//DIR is set to CW, therefore we add 1 step to step count (positive values = number of steps in CW direction from initial postion)	
		}
	}

	void TIMER1_COMPA_vect(void)
	{
		uint8_t data[2];
		uint16_t curAngle;
		int16_t deltaAngle;
		static uint16_t lastTcnt1;
		int16_t tcnt1Diff;
		float posError = 0.0;
		float sampleTime;
		static float posEst = 0.0;
		static float velIntegrator = 0.0;
		static float velEst = 0.0;

		if(pointer->mode == DROPIN)
		{
			sei();
		}

		sampleTime = ENCODERINTSAMPLETIME;

		if(I2C.getStatus() != I2CFREE)
		{
			return;
		}

		I2C.read(ENCODERADDR, ANGLE, 2, data);

		curAngle = (((uint16_t)data[0]) << 8 ) | (uint16_t)data[1];
		pointer->encoder.angle = curAngle;
		curAngle -= pointer->encoder.encoderOffset;

		if(curAngle > 4095)
		{
			curAngle -= 61440;
		}

		deltaAngle = (int16_t)pointer->encoder.oldAngle - (int16_t)curAngle;

		if(deltaAngle < -2047)
		{
			deltaAngle += 4096;
		}
		
		else if(deltaAngle > 2047)
		{
			deltaAngle -= 4096;
		}

		pointer->encoder.angleMoved += deltaAngle;		

		pointer->encoder.curSpeed *= 0.9;
		pointer->encoder.curSpeed += 0.1 * (float)deltaAngle * DELTAANGLETORPM;

		pointer->encoder.oldAngle = curAngle;

		if(pointer->mode == DROPIN)
		{
			posEst += velEst * sampleTime;
			cli();
			posError = (float)pointer->stepCnt - posEst;
			sei();
			velIntegrator += posError * PULSEFILTERKI;
			velEst = (posError * PULSEFILTERKP) + velIntegrator;
			pointer->currentPidSpeed = (velIntegrator * pointer->stepsPerSecondToRPM);

			if(!pointer->pidDisabled)
			{
				pointer->pid(deltaAngle);
			}

			pointer->detectStall((float)deltaAngle, pointer->getMotorState());
		}
		else
		{
			
			pointer->currentPidSpeed += pointer->currentPidAcceleration * sampleTime;
			if(pointer->direction == CW)
			{
				if(pointer->currentPidSpeed >= pointer->velocity * pointer->stepsPerSecondToRPM)
				{
					pointer->currentPidSpeed = pointer->velocity * pointer->stepsPerSecondToRPM;
				}
			}
			else
			{
				if(pointer->currentPidSpeed <= -pointer->velocity * pointer->stepsPerSecondToRPM)
				{
					pointer->currentPidSpeed = -pointer->velocity * pointer->stepsPerSecondToRPM;
				}
			}
			
			
			pointer->pidStepsSinceReset += (pointer->currentPidSpeed * pointer->RPMToStepsPerSecond) * sampleTime;
			pointer->stepsSinceReset = (int32_t)(pointer->pidStepsSinceReset + 0.5);

			

			if(pointer->state == INITDECEL)
			{
				if(pointer->direction == CW)
				{
					pointer->currentPidAcceleration = (pointer->acceleration * pointer->stepsPerSecondToRPM);
					if(pointer->stepsSinceReset >= pointer->decelToAccelThreshold)
					{
						pointer->state = ACCEL;
					}
				} 
				else
				{
					pointer->currentPidAcceleration = -(pointer->acceleration * pointer->stepsPerSecondToRPM);
					if(pointer->stepsSinceReset >= pointer->decelToAccelThreshold)
					{
						pointer->state = ACCEL;
					}
				}

			}
			else if(pointer->state == ACCEL)
			{
				if(pointer->direction == CCW)
				{
					if(pointer->stepsSinceReset <= pointer->accelToCruiseThreshold)
					{
						pointer->state = CRUISE;
					}
					pointer->currentPidAcceleration = -(pointer->acceleration * pointer->stepsPerSecondToRPM);
				} 
				else
				{
					if(pointer->stepsSinceReset >= pointer->accelToCruiseThreshold)
					{
						pointer->state = CRUISE;
					}
					pointer->currentPidAcceleration = pointer->acceleration * pointer->stepsPerSecondToRPM;
				}
			}
			else if(pointer->state == CRUISE)
			{
				if(pointer->direction == CCW)
				{
					if(pointer->stepsSinceReset <= pointer->cruiseToDecelThreshold)
					{
						pointer->state = DECEL;
					}
				} 
				else
				{
					if(pointer->stepsSinceReset >= pointer->cruiseToDecelThreshold)
					{
						pointer->state = DECEL;
					}
				}
				pointer->currentPidAcceleration = 0;
				if(pointer->continous == 1)
				{
					pointer->state = CRUISE;
				}
			}
			else if(pointer->state == DECEL)
			{
				if(pointer->direction == CW)
				{
					if(pointer->stepsSinceReset >= pointer->decelToStopThreshold)
					{
						pointer->state = STOP;
						pointer->currentPidAcceleration = 0.0;
						pointer->currentPidSpeed = 0.0;
					}
					else
					{
						pointer->currentPidAcceleration = -(pointer->acceleration * pointer->stepsPerSecondToRPM);
					}
				} 
				else if(pointer->direction == CCW)
				{
					if(pointer->stepsSinceReset <= pointer->decelToStopThreshold)
					{
						pointer->state = STOP;
						pointer->currentPidAcceleration = 0.0;
						pointer->currentPidSpeed = 0.0;
					}
					else
					{
						pointer->currentPidAcceleration = pointer->acceleration * pointer->stepsPerSecondToRPM;
					}
					
				}
			}
			else if(pointer->state == STOP)
			{
				pointer->currentPidAcceleration = 0.0;
				pointer->currentPidSpeed = 0.0;
			}
			
			if(!pointer->pidDisabled && pointer->mode == PID)
			{
				pointer->pid(deltaAngle);
			}
			else
			{
				pointer->driver.setVelocity(pointer->currentPidSpeed);
			}
		
			pointer->detectStall((float)deltaAngle, pointer->getMotorState());

		}
	}
}

uStepperSLite::uStepperSLite(float accel, float vel)
{
	this->state = STOP;

	this->setMaxVelocity(vel);
	this->setMaxAcceleration(accel);

	pointer = this;

	DDRB |= (1 << 2);		//set direction pin to output
	DDRD |= (1 << 7);		//set step pin to output
	DDRD |= (1 << 4);		//set enable pin to output
}

void uStepperSLite::setMaxAcceleration(float accel)
{
	this->acceleration = accel;

	if(this->state != STOP)
	{
		if(this->continous == 1)	//If motor was running continously
		{
			this->runContinous(this->direction);	//We should make it run continously again
		}
		else						//If motor still needs to perform some steps
		{
			this->moveSteps(this->targetPosition - this->stepsSinceReset + 1, this->direction, this->brake);	//we should make sure the motor gets to execute the remaining steps				
		}
	}
}

void uStepperSLite::setMaxVelocity(float vel)
{
	
	if(vel < 0.5005)
	{
		this->velocity = 0.5005;			//Limit velocity in order to not overflow delay variable
	}

	else if(vel > 28000.0)
	{
		this->velocity = 28000.0;			//limit velocity in order to not underflow delay variable
	}

	else
	{
		this->velocity = vel;
	}
	
	if(this->state != STOP)		//If motor was running, we should make sure it runs again
	{
		if(this->continous == 1)	//If motor was running continously
		{
			this->runContinous(this->direction);	//We should make it run continously again
		}
		else					//If motor still needs to perform some steps
		{
			this->moveSteps(this->targetPosition - this->stepsSinceReset + 1, this->direction, this->brake);	//we should make sure the motor gets to execute the remaining steps	
		}
	}
}

void uStepperSLite::runContinous(bool dir)
{
	float curVel, startVelocity = 0;
	uint8_t state;
	uint32_t accelSteps;
	uint32_t initialDecelSteps;

	if(this->mode == DROPIN)
	{
		return;		//Drop in feature is activated. just return since this function makes no sense with drop in activated!
	}

	curVel = this->currentPidSpeed * this->RPMToStepsPerSecond;

	if(this->state == STOP)											//If motor is currently running at desired speed
	{
		initialDecelSteps = 0;
		state = ACCEL;						//We should just run at cruise speed
		startVelocity = 0.0;//sqrt(2.0*this->acceleration);	//number of interrupts before the first step should be performed.
		accelSteps = (uint32_t)((this->velocity * this->velocity)/(2.0*this->acceleration));	//Number of steps to bring the motor to max speed (S = (V^2 - V0^2)/(2*a)))

	}
	else if((dir == CW && curVel < 0) || (dir == CCW && curVel > 0))									//If motor turns CCW and should turn CW, or if motor turns CW and shoúld turn CCW
	{
		state = INITDECEL;									//We should decelerate the motor to full stop
		initialDecelSteps = (uint32_t)((curVel*curVel)/(2.0*this->acceleration));		//the amount of steps needed to bring the motor to full stop. (S = (V^2 - V0^2)/(2*-a)))
		accelSteps = (uint32_t)((this->velocity * this->velocity)/(2.0*this->acceleration));									//Number of steps to bring the motor to max speed (S = (V^2 - V0^2)/(2*a)))
		
		startVelocity = curVel;//sqrt((curVel*curVel) + 2.0*this->acceleration);	//number of interrupts before the first step should be performed.
	}
	else if((dir == CW && curVel > 0) || (dir == CCW && curVel < 0))												//If the motor is currently rotating the same direction as the desired direction
	{
		if(abs(curVel) > this->velocity)						//If current velocity is greater than desired velocity
		{
			state = INITDECEL;						//We need to decelerate the motor to desired velocity
			initialDecelSteps = (uint32_t)(((this->velocity*this->velocity) - (curVel*curVel))/(-2.0*this->acceleration));		//Number of steps to bring the motor down from current speed to max speed (S = (V^2 - V0^2)/(2*-a)))
			accelSteps = 0;						//No acceleration phase is needed
		}

		else if(abs(curVel) < this->velocity)					//If the current velocity is less than the desired velocity
		{
			state = ACCEL;							//Start accelerating
			accelSteps = (uint32_t)(((this->velocity*this->velocity) - (curVel*curVel))/(2.0*this->acceleration));	//Number of Steps needed to accelerate from current velocity to full speed
			initialDecelSteps = 0;
		}

		else 											//If motor is currently running at desired speed
		{
			initialDecelSteps = 0;
			state = CRUISE;						//We should just run at cruise speed
			accelSteps = 0;						//No acceleration phase is needed
		}
	}

	cli();
		this->direction = dir;

		if(curVel < 0)
		{
			this->decelToAccelThreshold = this->stepsSinceReset - initialDecelSteps;
		}
		else
		{
			this->decelToAccelThreshold = this->stepsSinceReset + initialDecelSteps;
		}

		if(dir == CW)
		{
			this->accelToCruiseThreshold = this->decelToAccelThreshold + accelSteps;
		}
		else
		{
			this->accelToCruiseThreshold = this->decelToAccelThreshold - accelSteps;
		}
		this->currentPidSpeed = startVelocity * this->stepsPerSecondToRPM;
		this->state = state;
		this->continous = 1;			//Set continous variable to 1, in order to let the interrupt routine now, that the motor should run continously
		PORTD &= ~(1 << 4);
	sei();
	this->pidDisabled = 0;
}

void uStepperSLite::moveSteps(int32_t steps, bool dir, bool holdMode)
{
	float curVel, startVelocity = 0;
	uint8_t state;
	uint32_t totalSteps;
	uint32_t accelSteps;
	uint32_t decelSteps;
	uint32_t initialDecelSteps;
	uint32_t cruiseSteps = 0;

	if(this->mode == DROPIN)
	{
		return;		//Drop in feature is activated. just return since this function makes no sense with drop in activated!
	}

	if(steps < 1)
	{
		return;
	}
	totalSteps = steps;
	curVel = this->currentPidSpeed * this->RPMToStepsPerSecond;
	steps--;

	if(this->state == STOP)								//If motor is currently at full stop (state = STOP)
	{
		state = ACCEL;
		accelSteps = (uint32_t)((this->velocity * this->velocity)/(2.0*this->acceleration));	//Number of steps to bring the motor to max speed (S = (V^2 - V0^2)/(2*a)))
		initialDecelSteps = 0;		//No initial deceleration phase needed

		if((int32_t)accelSteps > (totalSteps >> 1))	//If we need to accelerate for longer than half of the total steps, we need to start decelerating before we reach max speed
		{
			cruiseSteps = 0; 		//No cruise phase needed
			accelSteps = decelSteps = (totalSteps >> 1);				//Accelerate and decelerate for the same amount of steps (half the total steps)
			accelSteps += totalSteps - accelSteps - decelSteps;	//if there are still a step left to perform, due to rounding errors, do this step as an acceleration step	
		}

		else								
		{
			decelSteps = accelSteps;	//If top speed is reached before half the total steps are performed, deceleration period should be same length as acceleration period
			cruiseSteps = totalSteps - accelSteps - decelSteps;	//Perform remaining steps as cruise steps
		}
		startVelocity = 0.0;//sqrt(2.0*this->acceleration);	//number of interrupts before the first step should be performed.
	}
	else if((dir == CW && curVel < 0) || (dir == CCW && curVel > 0))									//If motor turns CCW and should turn CW, or if motor turns CW and shoúld turn CCW
	{
		state = INITDECEL;									//We should decelerate the motor to full stop
		initialDecelSteps = (uint32_t)((curVel*curVel)/(2.0*this->acceleration));		//the amount of steps needed to bring the motor to full stop. (S = (V^2 - V0^2)/(2*-a)))
		accelSteps = (uint32_t)((this->velocity * this->velocity)/(2.0*this->acceleration));									//Number of steps to bring the motor to max speed (S = (V^2 - V0^2)/(2*a)))
		totalSteps += initialDecelSteps;				//Add the steps used for initial deceleration to the totalSteps variable, since we moved this number of steps, passed the initial position, and therefore need to move this amount of steps extra, in the desired direction

		if(accelSteps > (totalSteps >> 1))			//If we need to accelerate for longer than half of the total steps, we need to start decelerating before we reach max speed
		{
			accelSteps = decelSteps = (totalSteps >> 1);	//Accelerate and decelerate for the same amount of steps (half the total steps)
			accelSteps += totalSteps - accelSteps - decelSteps;				//If there are still a step left to perform, due to rounding errors, do this step as an acceleration step	
			cruiseSteps = 0;
		}
		else
		{
			decelSteps = accelSteps;					//If top speed is reached before half the total steps are performed, deceleration period should be same length as acceleration period
			cruiseSteps = totalSteps - accelSteps - decelSteps; 			//Perform remaining steps, as cruise steps
		}

		startVelocity = sqrt((curVel*curVel) + 2.0*this->acceleration);	//number of interrupts before the first step should be performed.
	}
	else if((dir == CW && curVel > 0) || (dir == CCW && curVel < 0))							//If the motor is currently rotating the same direction as desired, we dont necessarily need to decelerate
	{
		if(abs(curVel) > this->velocity)	//If current velocity is greater than desired velocity
		{
			state = INITDECEL;	//We need to decelerate the motor to desired velocity
			initialDecelSteps = (uint32_t)(((this->velocity*this->velocity) - (curVel*curVel))/(-2.0*this->acceleration));		//Number of steps to bring the motor down from current speed to max speed (S = (V^2 - V0^2)/(2*-a)))
			accelSteps = 0;	//No acceleration phase is needed
			decelSteps = (uint32_t)((this->velocity*this->velocity)/(2.0*this->acceleration));	//Number of steps needed to decelerate the motor from top speed to full stop
			startVelocity = curVel;//sqrt((curVel*curVel) + (2.0*this->acceleration));

			if(totalSteps <= (initialDecelSteps + decelSteps))
			{
				cruiseSteps = 0;
			}
			else
			{
				cruiseSteps = steps - initialDecelSteps - decelSteps;					//Perform remaining steps as cruise steps
			}
		}

		else if(abs(curVel) < this->velocity)	//If current velocity is less than desired velocity
		{
			state = ACCEL;			//Start accelerating
			accelSteps = (uint32_t)((((this->velocity*this->velocity) - curVel*curVel))/(2.0*this->acceleration));	//Number of Steps needed to accelerate from current velocity to full speed

			if(accelSteps > (totalSteps >> 1))			//If we need to accelerate for longer than half of the total steps, we need to start decelerating before we reach max speed
			{
				accelSteps = decelSteps = (totalSteps >> 1);	//Accelerate and decelerate for the same amount of steps (half the total steps)
				accelSteps += totalSteps - accelSteps - decelSteps;				//If there are still a step left to perform, due to rounding errors, do this step as an acceleration step	
				cruiseSteps = 0;
			}
			else
			{
				decelSteps = accelSteps;					//If top speed is reached before half the total steps are performed, deceleration period should be same length as acceleration period
				cruiseSteps = totalSteps - accelSteps - decelSteps; 			//Perform remaining steps, as cruise steps
			}

			cruiseSteps = steps - accelSteps - decelSteps;	//Perform remaining steps as cruise steps
			initialDecelSteps = 0;								//No initial deceleration phase needed
		}

		else						//If current velocity is equal to desired velocity
		{
			state = CRUISE;	//We are already at desired speed, therefore we start at cruise phase
			decelSteps = (uint32_t)((this->velocity*this->velocity)/(2.0*this->acceleration));	//Number of steps needed to decelerate the motor from top speed to full stop
			accelSteps = 0;	//No acceleration phase needed
			initialDecelSteps = 0;		//No initial deceleration phase needed

			if(decelSteps >= totalSteps)
			{
				cruiseSteps = 0;
			}
			else
			{
				cruiseSteps = steps - decelSteps;	//Perform remaining steps as cruise steps
			}
		}
	}
	cli();
		this->direction = dir;
		this->continous = 0;			
		if(curVel < 0)
		{
			this->decelToAccelThreshold = this->stepsSinceReset - initialDecelSteps;
		}
		else
		{
			this->decelToAccelThreshold = this->stepsSinceReset + initialDecelSteps;
		}

		if(dir == CW)
		{
			this->accelToCruiseThreshold = this->decelToAccelThreshold + accelSteps;
			this->cruiseToDecelThreshold = this->accelToCruiseThreshold + cruiseSteps;
			this->decelToStopThreshold = this->cruiseToDecelThreshold + decelSteps;
		}
		else
		{
			this->accelToCruiseThreshold = this->decelToAccelThreshold - accelSteps;
			this->cruiseToDecelThreshold = this->accelToCruiseThreshold - cruiseSteps;
			this->decelToStopThreshold = this->cruiseToDecelThreshold - decelSteps;
		}
		this->currentPidSpeed = startVelocity * this->stepsPerSecondToRPM;
		this->state = state;
		this->targetPosition = this->decelToStopThreshold;
		this->brake = holdMode;
		PORTD &= ~(1 << 4);
	sei();
	this->pidDisabled = 0;
}

void uStepperSLite::hardStop(bool holdMode)
{

	this->stop(holdMode);
}

void uStepperSLite::stop(bool brake)
{
	if(this->mode == DROPIN)
	{
		return;		//Drop in feature is activated. just return since this function makes no sense with drop in activated!
	}

	this->stall = 0;
	this->state = STOP;			//Set current state to STOP
	this->continous = 0;	
	this->pidDisabled = 1;
	pointer->driver.setVelocity(0);
	this->stepsSinceReset = (int32_t)((float)this->encoder.angleMoved * this->stepConversion);
	this->targetPosition = this->stepsSinceReset;
	this->brake = brake;
	if(brake == BRAKEOFF)
	{
		this->disableMotor();
	}
	
	else if (brake == BRAKEON)
	{
		this->enableMotor();
	}

}

void uStepperSLite::softStop(bool holdMode)
{
	this->stop(holdMode);
}

void uStepperSLite::checkConnectorOrientation(void)
{
	uint8_t data[2], i;
	uint16_t angle;
	int16_t angleDiff[3];

	I2C.read(ENCODERADDR, ANGLE, 2, data);
	angle = (((uint16_t)data[0]) << 8 ) | (uint16_t)data[1];

	PORTB |= (1 << 2);

	for(i = 0; i < 50; i++)
	{
		PORTD |= (1 << 7);
		delayMicroseconds(1);
		PORTD &= ~(1 << 7);
		_delay_ms(10);
	}

	angleDiff[0] = (int16_t)angle;

	I2C.read(ENCODERADDR, ANGLE, 2, data);
	angle = (((uint16_t)data[0]) << 8 ) | (uint16_t)data[1];

	angleDiff[0] -= (int16_t)angle;

	PORTB &= ~(1 << 2);

	for(i = 0; i < 50; i++)
	{
		PORTD |= (1 << 7);
		delayMicroseconds(1);
		PORTD &= ~(1 << 7);
		_delay_ms(10);
	}

	angleDiff[1] = (int16_t)angle;

	I2C.read(ENCODERADDR, ANGLE, 2, data);
	angle = (((uint16_t)data[0]) << 8 ) | (uint16_t)data[1];

	angleDiff[1] -= (int16_t)angle;

	PORTB |= (1 << 2);

	for(i = 0; i < 50; i++)
	{
		PORTD |= (1 << 7);
		delayMicroseconds(1);
		PORTD &= ~(1 << 7);
		_delay_ms(10);
	}

	angleDiff[2] = (int16_t)angle;

	I2C.read(ENCODERADDR, ANGLE, 2, data);
	angle = (((uint16_t)data[0]) << 8 ) | (uint16_t)data[1];

	angleDiff[2] -= (int16_t)angle;

	for(i = 0; i < 3; i++)
	{
		if(angleDiff[i] > 2048)
		{
			angleDiff[i] -= 4096;
		}
		else if(angleDiff[i] < -2048)
		{
			angleDiff[i] += 4096;
		}
	}

	if(!(angleDiff[0] < -2 && angleDiff[1] > 2 && angleDiff[2] < -2))
	{
		this->driver.invertDirection();
	}
}

void uStepperSLite::setup(	uint8_t mode, 
							float stepsPerRevolution,
							float pTerm, 
							float iTerm,
							float dTerm,
							bool setHome)
{
	this->pidDisabled = 1;
	this->mode = mode;
	this->encoder.setup();

	this->state = STOP;
	this->driver.setup();
	this->driver.enableDriver();
	_delay_ms(200);
		
	if((uint16_t)stepsPerRevolution == FULL)
	{
		stepsPerRevolution = 200.0;
	}
	else if((uint16_t)stepsPerRevolution == HALF)
	{
		stepsPerRevolution = 400.0;
	}
	else if((uint16_t)stepsPerRevolution == QUARTER)
	{
		stepsPerRevolution = 800.0;
	}
	else if((uint16_t)stepsPerRevolution == EIGHT)
	{
		stepsPerRevolution = 1600.0;
	}
	else if((uint16_t)stepsPerRevolution == SIXTEEN)
	{
		stepsPerRevolution = 3200.0;
	}

	this->stepConversion = (float)(stepsPerRevolution)/4096.0;	//Calculate conversion coefficient from raw encoder data, to actual moved steps
	this->angleToStep = ((float)(stepsPerRevolution))/360.0;	//Calculate conversion coefficient from angle to corresponding number of steps
	this->stepToAngle = 360.0/((float)(stepsPerRevolution));	//Calculate conversion coefficient from steps to corresponding angle
	this->stepsPerSecondToRPM = 60.0/stepsPerRevolution;
	this->RPMToStepsPerSecond = stepsPerRevolution/60.0;
	this->encoder.setHome();
	this->checkConnectorOrientation();
	this->encoder.setHome();

	if(setHome)
	{
		this->encoder.setHome();	
	}
	else
	{
		pointer->stepsSinceReset = ((float)this->encoder.angleMoved * this->stepConversion) + 0.5;
	}

	if(this->mode)
	{
		if(this->mode == DROPIN)
		{
			//Set Enable, Step and Dir signal pins from 3dPrinter controller as inputs
			pinMode(2,INPUT);		
			pinMode(3,INPUT);
			pinMode(4,INPUT);
			//Enable internal pull-up resistors on the above pins
			digitalWrite(2,HIGH);
			digitalWrite(3,HIGH);
			digitalWrite(4,HIGH);
			attachInterrupt(0, interrupt0, FALLING);
			attachInterrupt(1, interrupt1, CHANGE);
		}		

		//Scale supplied controller coefficents. This is done to enable the user to use easier to manage numbers for these coefficients.
	    this->pTerm = pTerm; 
	    this->iTerm = iTerm * ENCODERINTSAMPLETIME;    
	    this->dTerm = dTerm * ENCODERINTFREQ;    
	}
	this->pidDisabled = 0;
	sei();
}

void uStepperSLite::enableMotor(void)
{
	this->driver.enableDriver();				//Enable motor driver
}

void uStepperSLite::disableMotor(void)
{
	this->driver.disableDriver();			//Disable motor driver
}

bool uStepperSLite::getCurrentDirection(void)
{
	return this->direction;
}

uint8_t uStepperSLite::getMotorState(void)
{
	if(this->state != STOP)
	{
		return this->state;		//Motor running
	}

	return 0;			//Motor not running
}

int32_t uStepperSLite::getStepsSinceReset(void)
{
	return this->stepsSinceReset;
}

void uStepperSLite::setCurrent(uint8_t runCurrent, uint8_t holdCurrent)
{
	this->driver.setCurrent(runCurrent, holdCurrent);
}

void uStepperSLite::setHoldCurrent(uint8_t holdCurrent)
{
	this->driver.setHoldCurrent(holdCurrent);
}

void uStepperSLite::setRunCurrent(uint8_t runCurrent)
{
	this->driver.setHoldCurrent(runCurrent);
}

float uStepperSLite::moveToEnd(bool dir)
{
	uint8_t checks = 0;
  	float pos = 0.0;
  	float lengthMoved;

  	if(this->mode == DROPIN)
  	{
  		return 0.0;		//Doesn't make sense in dropin mode
  	}

  	lengthMoved = this->encoder.getAngleMoved();
  	
  	this->stop(HARD);
	_delay_ms(50);
  	this->runContinous(dir);

  	if(this->mode == PID)
  	{
  		while(!this->isStalled());
  		this->stop(SOFT);//stop motor without brake
  	}
  	else
  	{
  		delay(100);
		while(checks < 20)//allows for 2 checks on movement error
		{
			pos = abs(this->encoder.getAngleMoved() - (this->getStepsSinceReset()*this->stepToAngle));//see current position error

			if(abs(this->encoder.curSpeed) < (((float)this->velocity * this->stepsPerSecondToRPM)/2.0))//if position error is less than 5 steps it is okay...
			{
				checks++;
			}
			else //if position error is 5 steps or more, count up checks
			{
		  		checks = 0;
			}
			delay(1);
		}

	  	this->stop(SOFT);//stop motor without brake
  	}
  	
	this->moveSteps(20, !dir, SOFT);
	while(this->getMotorState())
	{
		_delay_ms(1);
	}
	_delay_ms(100);
	if(dir == CW)
	{
		lengthMoved = this->encoder.getAngleMoved() - lengthMoved;
	}
	else
	{
		lengthMoved -= this->encoder.getAngleMoved();
	}
  	this->encoder.setHome();//set new home position

  	return lengthMoved;
}

void uStepperSLite::moveToAngle(float angle, bool holdMode)
{
	float diff;
	uint32_t steps;

	if(this->encoder.detectMagnet())
	{
		//return;		//Magnet Not Detected. Abort
	}

	diff = angle - this->encoder.getAngleMoved();
	steps = (uint32_t)((abs(diff)*angleToStep) + 0.5);
	
	if(diff < 0.0)
	{
		this->moveSteps(steps, CCW, holdMode);
	}
	else
	{
		this->moveSteps(steps, CW, holdMode);
	}
}

void uStepperSLite::moveAngle(float angle, bool holdMode)
{
	int32_t steps;

	if(angle < 0.0)
	{
		steps = -(int32_t)((angle*angleToStep) - 0.5);
		this->moveSteps(steps, CCW, holdMode);
	}
	else
	{
		steps = (int32_t)((angle*angleToStep) + 0.5);
		this->moveSteps(steps, CW, holdMode);
	}
}

void uStepperSLite::pid(int16_t deltaAngle)
{
	static float output;
	float error;
	static float accumError = 0.0;
	float integral;
	static bool integralReset = 0;
	int32_t stepCnt;

	if(this->mode == DROPIN)
	{
		cli();
			stepCnt = this->stepCnt;
		sei();
		error = (((float)stepCnt - ((float)this->encoder.angleMoved * this->stepConversion))); 
	}
	else
	{
		error = (((float)this->stepsSinceReset - ((float)this->encoder.angleMoved * this->stepConversion)));
	}	



	PORTD &= ~(1 << 4);

	if(error > -2.0 && error < 2.0)
	{
		accumError = 0.0;
	}
	else
	{
		if(!(error > 10.0 || error < -10.0))
		{
			if(!integralReset)
			{
				accumError = 0.0;
			}
			integralReset = 1;
		}
		
		else
		{
			integralReset = 0;
		}
	}

	if(output <= -100.0)
	{
		integral = (this->iTerm * (10000.0/(output*output))) * error;
	}
	else if(output >= 100.0)
	{
		integral = (this->iTerm * (10000.0/(output*output))) * error;
	}
	else
	{
		integral = this->iTerm * error;
	}
	accumError += integral;

	if(accumError > 20000.0)
	{
		accumError = 20000.0;
	}
	else if(accumError < -20000.0)
	{
		accumError = -20000.0;
	}

	output = this->pTerm*error;	

	output += accumError;

	output *= this->stepsPerSecondToRPM;
	output += this->currentPidSpeed;
	this->driver.setVelocity(output);
}

bool uStepperSLite::detectStall(float diff, bool running)
{
	static uint8_t checks = 0;



	if(running)
	{
		if(this->getCurrentDirection() == CW)
		{
			if(this->encoder.curSpeed < this->currentPidSpeed * 0.5)
			{
				checks++;
				if(checks >= 10)
				{
					checks = 10;
					this->stall = 1;
				}
			}
			else
			{
				checks = 0;
				this->stall = 0;
			}
		}
		else if(this->getCurrentDirection() == CCW)
		{
			if(this->encoder.curSpeed > this->currentPidSpeed * 0.5)
			{
				checks++;
				if(checks >= 10)
				{
					checks = 10;
					this->stall = 1;
				}
			}
			else
			{
				checks = 0;
				this->stall = 0;
			}
		}
		
	}
	else
	{
		this->stall = 0;
		checks = 0;
	}
	return 0;
}

bool uStepperSLite::isStalled(void)
{
	return this->stall;
}