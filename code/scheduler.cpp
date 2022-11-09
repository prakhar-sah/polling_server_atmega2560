#include "scheduler.h"

#define schedUSE_TCB_ARRAY 1

/* Extended Task control block for managing periodic tasks within this library. */
typedef struct xExtended_TCB
{
	TaskFunction_t pvTaskCode; 		/* Function pointer to the code that will be run periodically. */
	const char *pcName; 			/* Name of the task. */
	UBaseType_t uxStackDepth; 			/* Stack size of the task. */
	void *pvParameters; 			/* Parameters to the task function. */
	UBaseType_t uxPriority; 		/* Priority of the task. */
	TaskHandle_t *pxTaskHandle;		/* Task handle for the task. */
	TickType_t xReleaseTime;		/* Release time of the task. */
	TickType_t xRelativeDeadline;	/* Relative deadline of the task. */
	TickType_t xAbsoluteDeadline;	/* Absolute deadline of the task. */
	TickType_t xPeriod;				/* Task period. */
	TickType_t xLastWakeTime; 		/* Last time stamp when the task was running. */
	TickType_t xMaxExecTime;		/* Worst-case execution time of the task. */
	TickType_t xExecTime;			/* Current execution time of the task. */
	TickType_t xStartTime;          /* Current start time of the task. */

	BaseType_t xWorkIsDone; 		/* pdFALSE if the job is not finished, pdTRUE if the job is finished. */

	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xPriorityIsSet; 	/* pdTRUE if the priority is assigned. */
		BaseType_t xInUse; 			/* pdFALSE if this extended TCB is empty. */
	#endif

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		BaseType_t xExecutedOnce;	/* pdTRUE if the task has executed once. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 || schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		TickType_t xAbsoluteUnblockTime; /* The task will be unblocked at this time if it is blocked by the scheduler task. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME || schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		BaseType_t xSuspended; 		/* pdTRUE if the task is suspended. */
		BaseType_t xMaxExecTimeExceeded; /* pdTRUE when execTime exceeds maxExecTime. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

	#if( schedUSE_POLLING_SERVER == 1 )
		BaseType_t xIsPollingServer; /* pdTRUE if the task is a polling server. */
	#endif /* schedUSE_POLLING_SERVER */
	
	/* add if you need anything else */	
	
} SchedTCB_t;

#if( schedUSE_APERIODIC_JOBS == 1 )
	/* Control block for managing Aperiodic jobs. */
	typedef struct xAperiodicTaskControl
	{
		TaskFunction_t pvTaskCode;	/* Function pointer to the code of the aperiodic job. */
		const char *pcName;			/* Name of the aperiodic job. */
		void *pvParameters;			/* Parameters to the job function. */
		TickType_t xReleaseTime;
		TickType_t xMaxExecTime;	/* Worst-case execution time of the aperiodic job. */
		TickType_t xExecTime;		/* Current execution time of the aperiodic job. */
	} ATC_t;
#endif /* schedUSE_APERIODIC_JOBS */

#if( schedUSE_TCB_ARRAY == 1 )
	static BaseType_t prvGetTCBIndexFromHandle( TaskHandle_t xTaskHandle );
	static void prvInitTCBArray( void );
	/* Find index for an empty entry in xTCBArray. Return -1 if there is no empty entry. */
	static BaseType_t prvFindEmptyElementIndexTCB( void );
	/* Remove a pointer to extended TCB from xTCBArray. */
	static void prvDeleteTCBFromArray( BaseType_t xIndex );
#endif /* schedUSE_TCB_ARRAY */

static TickType_t xSystemStartTime = 0;

static void prvPeriodicTaskCode( void *pvParameters );
static void prvCreateAllTasks( void );


#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
	static void prvSetFixedPriorities( void );	
#endif /* schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY_DMS */

#if( schedUSE_SCHEDULER_TASK == 1 )
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB );
	static void prvSchedulerFunction( void );
	static void prvCreateSchedulerTask( void );
	static void prvWakeScheduler( void );

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB );
		static void prvDeadlineMissedHook( SchedTCB_t *pxTCB, TickType_t xTickCount );
		static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount );				
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		static void prvExecTimeExceedHook( TickType_t xTickCount, SchedTCB_t *pxCurrentTask );
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	
#endif /* schedUSE_SCHEDULER_TASK */

#if( schedUSE_POLLING_SERVER == 1 )
	static void prvPollingServerFunction( void );
	void prvCreatePollingServer( void );
#endif /* schedUSE_POLLING_SERVER */

#if( schedUSE_APERIODIC_JOBS == 1 )
	static ATC_t *prvGetNextAperiodicTask( void );
	static BaseType_t prvGetEmptyIndexATC( void );
#endif /* schedUSE_APERIODIC_JOBS */

#if( schedUSE_TCB_ARRAY == 1 )
	/* Array for extended TCBs. */
	static SchedTCB_t xTCBArray[ schedMAX_NUMBER_OF_PERIODIC_TASKS ] = { 0 };
	/* Counter for number of periodic tasks. */
	static BaseType_t xTaskCounter = 0;
#endif /* schedUSE_TCB_ARRAY */

#if( schedUSE_SCHEDULER_TASK )
	static TickType_t xSchedulerWakeCounter = 0; /* useful. why? */
	static TaskHandle_t xSchedulerHandle = NULL; /* useful. why? */
#endif /* schedUSE_SCHEDULER_TASK */

#if( schedUSE_APERIODIC_JOBS == 1 )
	/* Array for extended ATCs (Aperiodic Task Control ). */
	static ATC_t xATCArray[ schedMAX_NUMBER_OF_APERIODIC_JOBS ] = { 0 };
	//static ATC_t xATCBridgeArray[ schedMAX_NUMBER_OF_APERIODIC_JOBS ] = { 0 };
	static BaseType_t xATCArrayFirst = 0;
	static BaseType_t xATCArrayLast = 0;
	static UBaseType_t uxAperiodicTaskCounter = 0;
#endif /* schedUSE_APERIODIC_JOBS */

#if( schedUSE_POLLING_SERVER == 1 )
    static TaskHandle_t xPollingServerHandle = NULL;
	#if( schedUSE_APERIODIC_JOBS == 1 )
		static ATC_t *pxCurrentAperiodicTask;
	#endif /* schedUSE_APERIODIC_JOBS */
#endif /* schedUSE_POLLING_SERVER */

#if( schedUSE_TCB_ARRAY == 1 )
	/* Returns index position in xTCBArray of TCB with same task handle as parameter. */
	static BaseType_t prvGetTCBIndexFromHandle( TaskHandle_t xTaskHandle )
	{
		static BaseType_t xIndex = 0;
		BaseType_t xIterator;

		for( xIterator = 0; xIterator < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIterator++ )
		{
		
			if( pdTRUE == xTCBArray[ xIndex ].xInUse && *xTCBArray[ xIndex ].pxTaskHandle == xTaskHandle )
			{
				return xIndex;
			}
		
			xIndex++;
			if( schedMAX_NUMBER_OF_PERIODIC_TASKS == xIndex )
			{
				xIndex = 0;
			}
		}
		return -1;
	}

	/* Initializes xTCBArray. */
	static void prvInitTCBArray( void )
	{
	UBaseType_t uxIndex;
		for( uxIndex = 0; uxIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; uxIndex++)
		{
			xTCBArray[ uxIndex ].xInUse = pdFALSE;
			xTCBArray[uxIndex].pxTaskHandle = NULL;
		}
	}

	/* Find index for an empty entry in xTCBArray. Returns -1 if there is no empty entry. */
	static BaseType_t prvFindEmptyElementIndexTCB( void )
	{
		BaseType_t xIndex;
		for(xIndex=0;xIndex<schedMAX_NUMBER_OF_PERIODIC_TASKS;xIndex++){
			if(!xTCBArray[xIndex].xInUse){
				break;
			}
		}
		if(xIndex == schedMAX_NUMBER_OF_PERIODIC_TASKS){
			xIndex = -1;
		}
		return xIndex;

	}

	/* Remove a pointer to extended TCB from xTCBArray. */
	static void prvDeleteTCBFromArray( BaseType_t xIndex )
	{
		configASSERT(xIndex>=0);
		if(xTCBArray[xIndex].xInUse == pdTRUE){
			xTCBArray[xIndex].xInUse = pdFALSE;
			xTaskCounter--;
		}
	}
	
#endif /* schedUSE_TCB_ARRAY */


/* The whole function code that is executed by every periodic task.
 * This function wraps the task code specified by the user. */
static void prvPeriodicTaskCode( void *pvParameters )
{
 
	SchedTCB_t *pxThisTask;	
	TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();  
	
    /* your implementation goes here */
    
    /* Check the handle is not NULL. */
    configASSERT(xCurrentTaskHandle != NULL)
    BaseType_t xIndex = prvGetTCBIndexFromHandle(xCurrentTaskHandle);
	if(xIndex < 0){
		Serial.print("Invalid index\n");
		Serial.flush();
	}
	configASSERT(xIndex >= 0);
	
	pxThisTask = &xTCBArray[xIndex];
	if( pxThisTask->xReleaseTime != 0){
	    xTaskDelayUntil(&pxThisTask->xLastWakeTime,pxThisTask->xReleaseTime);
	}    
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
        /* your implementation goes here */
        pxThisTask->xExecutedOnce = pdTRUE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
    
	if( 0 == pxThisTask->xReleaseTime )
	{
		pxThisTask->xLastWakeTime = xSystemStartTime;
	}
	
	for( ; ; )
	{	
        pxThisTask->xStartTime = xTaskGetTickCount();      //ps&ac
		pxThisTask->xWorkIsDone = pdFALSE;
		Serial.print(pxThisTask->pcName);
		Serial.print(" - START - ");
		Serial.print(xTaskGetTickCount());
		Serial.print("\n");
		Serial.flush();
		pxThisTask->pvTaskCode( pvParameters );
		Serial.print(pxThisTask->pcName);
		Serial.print(" - END - ");
		Serial.print(xTaskGetTickCount());
		Serial.print("\n");
		Serial.flush();
		pxThisTask->xWorkIsDone = pdTRUE;
		pxThisTask->xExecTime = 0;  
        
		xTaskDelayUntil(&pxThisTask->xLastWakeTime, pxThisTask->xPeriod);
	}
}

/* Creates a periodic task. */
void vSchedulerPeriodicTaskCreate( TaskFunction_t pvTaskCode, const char *pcName, UBaseType_t uxStackDepth, void *pvParameters, UBaseType_t uxPriority,
		TaskHandle_t *pxCreatedTask, TickType_t xPhaseTick, TickType_t xPeriodTick, TickType_t xMaxExecTimeTick, TickType_t xDeadlineTick )
{
	taskENTER_CRITICAL();
	SchedTCB_t *pxNewTCB;
	
	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xIndex = prvFindEmptyElementIndexTCB();
		configASSERT( xTaskCounter < schedMAX_NUMBER_OF_PERIODIC_TASKS );
		configASSERT( xIndex != -1 );
		pxNewTCB = &xTCBArray[ xIndex ];	
	#endif /* schedUSE_TCB_ARRAY */

	/* Intialize item. */
	
    pxNewTCB->pvTaskCode = pvTaskCode;
	pxNewTCB->pcName = pcName;
	pxNewTCB->uxStackDepth = uxStackDepth;
	pxNewTCB->pvParameters = pvParameters;
	pxNewTCB->uxPriority = uxPriority;
	pxNewTCB->pxTaskHandle = pxCreatedTask;
	pxNewTCB->xReleaseTime = xPhaseTick;
	pxNewTCB->xPeriod = xPeriodTick;
	
    pxNewTCB->xRelativeDeadline = xDeadlineTick;
    pxNewTCB->xAbsoluteDeadline = pxNewTCB->xReleaseTime + xSystemStartTime + pxNewTCB->xRelativeDeadline;
	pxNewTCB->xWorkIsDone = pdFALSE;
	pxNewTCB->xExecTime = 0;
	pxNewTCB->xMaxExecTime = xMaxExecTimeTick;
	#if( schedUSE_POLLING_SERVER == 1)
		pxNewTCB->xIsPollingServer = pdFALSE;
	#endif /* schedUSE_POLLING_SERVER */


    
	#if( schedUSE_TCB_ARRAY == 1 )
		pxNewTCB->xInUse = pdTRUE;
	#endif /* schedUSE_TCB_ARRAY */
	
	#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
		/* member initialization */
		pxNewTCB->xPriorityIsSet = pdFALSE;
	#endif /* schedSCHEDULING_POLICY */
	
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		/* member initialization */
		pxNewTCB->xExecutedOnce = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	
	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
        pxNewTCB->xSuspended = pdFALSE;
        pxNewTCB->xMaxExecTimeExceeded = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */	

	#if( schedUSE_TCB_ARRAY == 1 )
		xTaskCounter++;	
	#endif /* schedUSE_TCB_SORTED_LIST */
	taskEXIT_CRITICAL();
  //Serial.println(pxNewTCB->xMaxExecTime);
}

/* Deletes a periodic task. */
void vSchedulerPeriodicTaskDelete( TaskHandle_t xTaskHandle )
{
	BaseType_t xIndex = prvGetTCBIndexFromHandle(xTaskHandle);
	prvDeleteTCBFromArray(xIndex);
	
	vTaskDelete( xTaskHandle );
}

/* Creates all periodic tasks stored in TCB array, or TCB list. */
static void prvCreateAllTasks( void )
{
	SchedTCB_t *pxTCB;

	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xIndex;
		for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
		{
			configASSERT( pdTRUE == xTCBArray[ xIndex ].xInUse );
			pxTCB = &xTCBArray[ xIndex ];
			BaseType_t xReturnValue = xTaskCreate(prvPeriodicTaskCode,pxTCB->pcName,pxTCB->uxStackDepth,pxTCB->pvParameters,pxTCB->uxPriority,pxTCB->pxTaskHandle);
			if(xReturnValue == pdPASS) {
				Serial.print(pxTCB->pcName);
				Serial.print(", Period- ");
				Serial.print(pxTCB->xPeriod);
				Serial.print(", Released at- ");
				Serial.print(pxTCB->xReleaseTime);
				Serial.print(", Priority- ");
				Serial.print(pxTCB->uxPriority);				
				Serial.print(", WCET- ");
				Serial.print(pxTCB->xMaxExecTime);
				Serial.print(", Deadline- ");
				Serial.print(pxTCB->xRelativeDeadline);
				Serial.println();
				Serial.flush();
			}
			else
			{
				Serial.println("Task creation failed\n");
				Serial.flush();
			}
		}	
	#endif /* schedUSE_TCB_ARRAY */
}

#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS  || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
	/* Initiazes fixed priorities of all periodic tasks with respect to RMS policy. */
static void prvSetFixedPriorities( void )
{
	BaseType_t xIter, xIndex;
	TickType_t xShortest, xPreviousShortest=0;
	SchedTCB_t *pxShortestTaskPointer, *pxTCB;

	#if( schedUSE_SCHEDULER_TASK == 1 )
		BaseType_t xHighestPriority = schedSCHEDULER_PRIORITY; 
	#else
		BaseType_t xHighestPriority = configMAX_PRIORITIES;
	#endif /* schedUSE_SCHEDULER_TASK */

	for( xIter = 0; xIter < xTaskCounter; xIter++ )
	{
		xShortest = portMAX_DELAY;

		/* search for shortest period */
		for( xIndex = 0; xIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIndex++ )
		{
			/* your implementation goes here */
			#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS )
				if ((xTCBArray[xIndex].xInUse == pdTRUE) && (xTCBArray[xIndex].xPriorityIsSet == pdFALSE) && (xTCBArray[xIndex].xPeriod < xShortest)){
					xShortest = xTCBArray[ xIndex ].xPeriod;
					pxShortestTaskPointer = &xTCBArray[xIndex];
				}
			#elif( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
			    if ((xTCBArray[xIndex].xInUse == pdTRUE) && (xTCBArray[xIndex].xPriorityIsSet == pdFALSE) && (xTCBArray[xIndex].xRelativeDeadline < xShortest)){
                    xShortest = xTCBArray[ xIndex ].xRelativeDeadline;
					pxShortestTaskPointer = &xTCBArray[xIndex];
				}
			#endif /* schedSCHEDULING_POLICY */
		}
		
		/* set highest priority to task with xShortest period (the highest priority is configMAX_PRIORITIES-1) */		
		
		/* your implementation goes here */
	pxShortestTaskPointer->uxPriority = xHighestPriority;
	if(xHighestPriority > 0){
		xHighestPriority--;
	}else{
		xHighestPriority = 0;
	}
	
	pxShortestTaskPointer->xPriorityIsSet = pdTRUE;
	}
}
#endif /* schedSCHEDULING_POLICY */


#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )

	/* Recreates a deleted task that still has its information left in the task array (or list). */
	static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB )
	{
		BaseType_t xReturnValue = xTaskCreate(prvPeriodicTaskCode,pxTCB->pcName,pxTCB->uxStackDepth,pxTCB->pvParameters,pxTCB->uxPriority,pxTCB->pxTaskHandle);
				                      		
		if( pdPASS == xReturnValue )
		{
			#if( schedUSE_TCB_ARRAY == 1 )
				pxTCB->xInUse = pdTRUE;
			#endif /* schedUSE_TCB_ARRAY */
    		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
				pxTCB->xExecutedOnce = pdFALSE;
			#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
        		pxTCB->xSuspended = pdFALSE;
        		pxTCB->xMaxExecTimeExceeded = pdFALSE;
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */	
		}
		else
		{
			Serial.println("Task Recreation failed");
			Serial.flush();
		}
	}

	/* Called when a deadline of a periodic task is missed.
	 * Deletes the periodic task that has missed it's deadline and recreate it.
	 * The periodic task is released during next period. */
	static void prvDeadlineMissedHook( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{
		Serial.print("Deadline missed - ");
		Serial.print(pxTCB->pcName);
		Serial.print(" - ");
		Serial.println(xTaskGetTickCount());
		Serial.flush();
		/* Delete the pxTask and recreate it. */
		vTaskDelete( *pxTCB->pxTaskHandle );
		pxTCB->xWorkIsDone = pdFALSE;
		pxTCB->xExecTime = 0;
		prvPeriodicTaskRecreate( pxTCB );	
		
		pxTCB->xReleaseTime = pxTCB->xLastWakeTime + pxTCB->xPeriod;
		pxTCB->xLastWakeTime = xTickCount;
		pxTCB->xAbsoluteDeadline = pxTCB->xRelativeDeadline + pxTCB->xReleaseTime;
	}

	/* Checks whether given task has missed deadline or not. */
	static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{ 
		/* check whether deadline is missed. */     		
		/* your implementation goes here */
		if((pxTCB->xWorkIsDone==pdFALSE)&&(pxTCB->xExecutedOnce==pdTRUE)){
			pxTCB->xAbsoluteDeadline = pxTCB->xLastWakeTime + pxTCB->xRelativeDeadline;
			if(pxTCB->xAbsoluteDeadline < xTickCount){
				prvDeadlineMissedHook(pxTCB, xTickCount);
			}
		}
	}	

#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */


#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )

	/* Called if a periodic task has exceeded its worst-case execution time.
	 * The periodic task is blocked until next period. A context switch to
	 * the scheduler task occur to block the periodic task. */
	static void prvExecTimeExceedHook( TickType_t xTickCount, SchedTCB_t *pxCurrentTask )
	{
        Serial.print(pxCurrentTask->pcName);
        Serial.print(" Exceeded - ");
		Serial.println(xTaskGetTickCount());
        Serial.flush();
        pxCurrentTask->xMaxExecTimeExceeded = pdTRUE;
        /* Is not suspended yet, but will be suspended by the scheduler later. */
        pxCurrentTask->xSuspended = pdTRUE;
        pxCurrentTask->xAbsoluteUnblockTime = pxCurrentTask->xLastWakeTime + pxCurrentTask->xPeriod;
        pxCurrentTask->xExecTime = 0;

		#if( schedUSE_POLLING_SERVER == 1)
			if( pxCurrentTask->xIsPollingServer == pdTRUE )
			{
				pxCurrentTask->xAbsoluteDeadline = pxCurrentTask->xAbsoluteUnblockTime + pxCurrentTask->xRelativeDeadline;
			}
		#endif /* schedUSE_POLLING_SERVER */
        
        BaseType_t xHigherPriorityTaskWoken;
        vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
        xTaskResumeFromISR(xSchedulerHandle);
	}
#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Called by the scheduler task. Checks all tasks for any enabled
	 * Timing Error Detection feature. */
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB )
	{
		/* your implementation goes here */
    

		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )						
			/* check if task missed deadline */
            /* your implementation goes here */
			#if( schedUSE_POLLING_SERVER == 1 )
				if( pdFALSE == pxTCB->xIsPollingServer )
					{
						if(xTickCount - (pxTCB->xLastWakeTime) > 0)
						{                
						  pxTCB->xWorkIsDone = pdFALSE;
					    }
						prvCheckDeadline( pxTCB, xTickCount );
					}
			#else
				if(xTickCount - (pxTCB->xLastWakeTime) > 0){                
					pxTCB->xWorkIsDone = pdFALSE;
				}
					prvCheckDeadline( pxTCB, xTickCount );	
			#endif				
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
		

		#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
        if( pdTRUE == pxTCB->xMaxExecTimeExceeded )
        {
            pxTCB->xMaxExecTimeExceeded = pdFALSE;
            Serial.print(pxTCB->pcName);
            Serial.print(" suspended - ");
            Serial.print(xTaskGetTickCount());
            Serial.print("\n");
            Serial.flush();
            vTaskSuspend( *pxTCB->pxTaskHandle );
        }
        if( pdTRUE == pxTCB->xSuspended )
        {
            if( ( signed ) ( pxTCB->xAbsoluteUnblockTime - xTickCount ) <= 0 )
            {
                pxTCB->xSuspended = pdFALSE;
                pxTCB->xLastWakeTime = xTickCount;
                Serial.print(pxTCB->pcName);
                Serial.print(" resumed - ");
                Serial.print(xTaskGetTickCount());
            	Serial.print("\n");
                Serial.flush();
                vTaskResume( *pxTCB->pxTaskHandle );
            }
        }
		#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

		return;
	}

	#if( schedUSE_APERIODIC_JOBS == 1 )
	/* Returns ATC of first aperiodic job stored in ATC Array. Returns NULL if
	 * the ATC Array is empty. */
	static ATC_t *prvGetNextAperiodicTask( void )
	{
		/* If ATC Array is empty. */
		if( uxAperiodicTaskCounter == 0 )
		{
			return NULL;
		}

        TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();  
        configASSERT(xCurrentTaskHandle != NULL);
		BaseType_t xIndex = prvGetTCBIndexFromHandle(xCurrentTaskHandle);
        configASSERT(xIndex != -1);
	    SchedTCB_t *pxThisTask = &xTCBArray[xIndex];
        
        if( xATCArray[ xATCArrayFirst ].xReleaseTime <= pxThisTask->xStartTime )
		{
			ATC_t *pxNextAT = &xATCArray[ xATCArrayFirst ];

			/* Move ATC Array head to next element in the queue. */
			xATCArrayFirst++;
			if( schedMAX_NUMBER_OF_APERIODIC_JOBS == xATCArrayFirst )
			{
				xATCArrayFirst = 0;             //restting ATC Array head to 0 when end of array is reached
			}

			return pxNextAT;
		}
		else
		{
			return NULL;
		}
	}

	/* Find index for an empty entry in xATCArray. Returns -1 if there is
	 * no empty entry. */
	static BaseType_t prvGetEmptyIndexATC( void )
	{
		/* If the ATC Array is full. */
		if( schedMAX_NUMBER_OF_APERIODIC_JOBS == uxAperiodicTaskCounter )
		{
			return -1;
		}

		BaseType_t xEmptyIndex = xATCArrayLast;

		/* Extend the ATC Array tail. */
		xATCArrayLast++;
		if( schedMAX_NUMBER_OF_APERIODIC_JOBS == xATCArrayLast )
		{
			xATCArrayLast = 0;        //restting ATC Array tail to 0 when end of array is reached
		}

		return xEmptyIndex;
	}

	/* Creates an aperiodic job. */
	void vSchedulerAperiodicTaskCreate( TaskFunction_t pvTaskCode, const char *pcName, void *pvParameters, TickType_t xMaxExecTimeTick, TickType_t xPhaseTick )
	{
		taskENTER_CRITICAL();
	    BaseType_t xIndex = prvGetEmptyIndexATC();
		if( -1 == xIndex)
		{
			/* The ATC Array is full. */
			taskEXIT_CRITICAL();
			return;
		}
		configASSERT( uxAperiodicTaskCounter < schedMAX_NUMBER_OF_APERIODIC_JOBS );
		ATC_t *pxNewATC = &xATCArray[ xIndex ];
        
		/* Add item to ATC Array. */
		*pxNewATC = ( ATC_t ) { .pvTaskCode = pvTaskCode, .pcName = pcName, .pvParameters = pvParameters, .xReleaseTime = xPhaseTick, .xMaxExecTime = xMaxExecTimeTick, .xExecTime = 0, };
		
		uxAperiodicTaskCounter++;
		taskEXIT_CRITICAL();
	}
#endif /* schedUSE_APERIODIC_JOBS */

#if( schedUSE_POLLING_SERVER == 1 )
	/* Function code for the Polling Server. */
	static void prvPollingServerFunction( void )
	{
		for( ; ; )
		{
			#if( schedUSE_APERIODIC_JOBS == 1 )
				pxCurrentAperiodicTask = prvGetNextAperiodicTask();
				if( pxCurrentAperiodicTask == NULL )
				{
					/* No ready aperiodic task in the queue. */
					return;
				}
				else
				{
					/* Run aperiodic task */
					pxCurrentAperiodicTask->pvTaskCode( pxCurrentAperiodicTask->pvParameters );
					uxAperiodicTaskCounter--;
				}
			#endif /* schedUSE_APERIODIC_JOBS */
		}
	}

	/* Creates Polling Server as a periodic task. */
	void prvCreatePollingServer( void )
	{
		taskENTER_CRITICAL();
	SchedTCB_t *pxNewTCB;
		#if( schedUSE_TCB_ARRAY == 1 )
			BaseType_t xIndex = prvFindEmptyElementIndexTCB();
			configASSERT( xTaskCounter < schedMAX_NUMBER_OF_PERIODIC_TASKS );
			configASSERT( xIndex != -1 );
			pxNewTCB = &xTCBArray[ xIndex ];
		#endif /* schedUSE_TCB_ARRAY */

		/* Initialize item. */
		//*pxNewTCB = ( SchedTCB_t ) { .pvTaskCode = (TaskFunction_t) prvPollingServerFunction, .pcName = "PS", .uxStackDepth = schedPOLLING_SERVER_STACK_SIZE, .pvParameters = NULL, 
			//.uxPriority = 4, .pxTaskHandle = &xPollingServerHandle, .xReleaseTime = 0, .xRelativeDeadline = schedPOLLING_SERVER_DEADLINE, .xAbsoluteDeadline = pxNewTCB->xReleaseTime + xSystemStartTime + pxNewTCB->xRelativeDeadline, 
			//.xPeriod = schedPOLLING_SERVER_PERIOD, .xLastWakeTime = 0, .xMaxExecTime = schedPOLLING_SERVER_MAX_EXECUTION_TIME, .xExecTime = 0, .xWorkIsDone = pdTRUE, .xIsPollingServer = pdTRUE };   
			//put in ino
    
		pxNewTCB->pvTaskCode = (TaskFunction_t) prvPollingServerFunction;
		pxNewTCB->pcName = "PS";
		pxNewTCB->uxStackDepth = schedPOLLING_SERVER_STACK_SIZE;
		pxNewTCB->pvParameters = NULL;
		pxNewTCB->uxPriority = 0;
		pxNewTCB->pxTaskHandle = &xPollingServerHandle;
		pxNewTCB->xReleaseTime = 0;
		pxNewTCB->xRelativeDeadline = schedPOLLING_SERVER_DEADLINE;
		pxNewTCB->xAbsoluteDeadline = pxNewTCB->xReleaseTime + xSystemStartTime + pxNewTCB->xRelativeDeadline;
		pxNewTCB->xMaxExecTime = schedPOLLING_SERVER_MAX_EXECUTION_TIME;
		pxNewTCB->xExecTime = 0;
		pxNewTCB->xWorkIsDone = pdTRUE;
		pxNewTCB->xPeriod = schedPOLLING_SERVER_PERIOD; 
	    
		#if( schedUSE_TCB_ARRAY == 1 )
			pxNewTCB->xInUse = pdTRUE;
		#endif /*schedUSE_TCB_ARRAY */
        
		#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
			pxNewTCB->xPriorityIsSet = pdFALSE;
		#endif /* schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY_DMS */

		#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
			pxNewTCB->xSuspended = pdFALSE;
			pxNewTCB->xMaxExecTimeExceeded = pdFALSE;
		#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

		pxNewTCB->xIsPollingServer = pdTRUE;
	
		#if( schedUSE_TCB_ARRAY == 1 )
			xTaskCounter++;
		#endif /* schedUSE_TCB_ARRAY */
		taskEXIT_CRITICAL();
	}


#endif /* schedUSE_POLLING_SERVER */

	/* Function code for the scheduler task. */
	static void prvSchedulerFunction( void *pvParameters )
	{

		for( ; ; )
		{ 
			
     		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				TickType_t xTickCount = xTaskGetTickCount();
        		SchedTCB_t *pxTCB;
        		for(BaseType_t xIndex=0;xIndex<xTaskCounter;xIndex++){
        			pxTCB = &xTCBArray[xIndex];
        			if ((pxTCB) && (pxTCB->xInUse == pdTRUE)&&(pxTCB->pxTaskHandle != NULL)) {
                    	prvSchedulerCheckTimingError( xTickCount, pxTCB );
                    }
              	}
			
			#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

			ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
		}
	}

	/* Creates the scheduler task. */
	static void prvCreateSchedulerTask( void )
	{
		xTaskCreate( (TaskFunction_t) prvSchedulerFunction, "Scheduler", schedSCHEDULER_TASK_STACK_SIZE, NULL, schedSCHEDULER_PRIORITY, &xSchedulerHandle );
	
	}
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Wakes up (context switches to) the scheduler task. */
	static void prvWakeScheduler( void )
	{
		BaseType_t xHigherPriorityTaskWoken;
		vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
		xTaskResumeFromISR(xSchedulerHandle);    
	}

	/* Called every software tick. */
	// In FreeRTOSConfig.h,
	// Enable configUSE_TICK_HOOK
	// Enable INCLUDE_xTaskGetIdleTaskHandle
	// Enable INCLUDE_xTaskGetCurrentTaskHandle
	
	void vApplicationTickHook( void )
	{            
		SchedTCB_t *pxCurrentTask;		
		TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();		
        UBaseType_t flag = 0;
        BaseType_t xIndex;
		BaseType_t prioCurrentTask = uxTaskPriorityGet(xCurrentTaskHandle);

		for(xIndex = 0; xIndex < xTaskCounter ; xIndex++){
			pxCurrentTask = &xTCBArray[xIndex];
			if(pxCurrentTask -> uxPriority == prioCurrentTask){
				flag = 1;
				break;
			}
		}
    
		if( xCurrentTaskHandle != xSchedulerHandle && xCurrentTaskHandle != xTaskGetIdleTaskHandle() && flag == 1)
		{
			
			
			pxCurrentTask->xExecTime++;     
     
			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
            if( pxCurrentTask->xMaxExecTime <= pxCurrentTask->xExecTime )
            {
                if( pdFALSE == pxCurrentTask->xMaxExecTimeExceeded )
                {
                    if( pdFALSE == pxCurrentTask->xSuspended )
                    {
                        prvExecTimeExceedHook( xTaskGetTickCountFromISR(), pxCurrentTask );
                    }
                }
            }
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
		}

		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )    
			xSchedulerWakeCounter++;      
			if( xSchedulerWakeCounter == schedSCHEDULER_TASK_PERIOD )
			{
				xSchedulerWakeCounter = 0;        
				prvWakeScheduler();
			}
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	}
#endif /* schedUSE_SCHEDULER_TASK */

/* This function must be called before any other function call from this module. */
void vSchedulerInit( void )
{
	#if( schedUSE_TCB_ARRAY == 1 )
		prvInitTCBArray();
	#endif /* schedUSE_TCB_ARRAY */
}

/* Starts scheduling tasks. All periodic tasks (including polling server) must
 * have been created with API function before calling this function. */
void vSchedulerStart( void )
{
    #if( schedUSE_POLLING_SERVER == 1 )
		prvCreatePollingServer();
	#endif /* schedUSE_POLLING_SERVER */

	#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
		prvSetFixedPriorities();	
	#endif /* schedSCHEDULING_POLICY */

	#if( schedUSE_SCHEDULER_TASK == 1 )
		prvCreateSchedulerTask();
	#endif /* schedUSE_SCHEDULER_TASK */

	prvCreateAllTasks();
	  
	xSystemStartTime = xTaskGetTickCount();
	
	vTaskStartScheduler();
}
