#include "scheduler.h"
#include "task.h" 

TaskHandle_t xHandle1 = NULL;
TaskHandle_t xHandle2 = NULL;
TaskHandle_t xHandle3 = NULL;
TaskHandle_t xHandle4 = NULL;

// the loop function runs over and over again forever
void loop() {}

static void testFuncA1( void *pvParameters )
{
  Serial.print("TestA1 start ");
  Serial.println(xTaskGetTickCount());
  unsigned long start_time = xTaskGetTickCount();
  unsigned long endtime = start_time;
  while((endtime - start_time) <= 21)
  {
    endtime = xTaskGetTickCount();
  }
  Serial.print("TestA1 end ");
  Serial.println(xTaskGetTickCount());
}

static void testFuncA2( void *pvParameters)
{
  Serial.print("TestA2 start ");
  Serial.println(xTaskGetTickCount());
  unsigned long start_time = xTaskGetTickCount();
  unsigned long endtime = start_time;
  while((endtime - start_time) <= 21)
  {
    endtime = xTaskGetTickCount();
  }
  Serial.print("TestA2 end ");
  Serial.println(xTaskGetTickCount());
}

static void testFuncA3( void *pvParameters)
{
  Serial.print("TestA3 start ");
  Serial.println(xTaskGetTickCount());
  unsigned long start_time = xTaskGetTickCount();
  unsigned long endtime = start_time;
  while((endtime - start_time) <= 21)
  {
    endtime = xTaskGetTickCount();
  }
  Serial.print("TestA3 end ");
  Serial.println(xTaskGetTickCount());
}

static void testFunc1( void *pvParameters )
{
  (void) pvParameters;
  unsigned long start_time = xTaskGetTickCount();
  unsigned long endtime = start_time;
  while((endtime - start_time) <= 21)
  {
    endtime = xTaskGetTickCount();
  }
}

static void testFunc2( void *pvParameters )
{ 
  (void) pvParameters;  
  unsigned long start_time = xTaskGetTickCount();
  unsigned long endtime = start_time;
  while((endtime - start_time) <= 21)
  {
    endtime = xTaskGetTickCount();
  }
}

int main( void )
{
  char c1 = 'a';
  char c2 = 'b';
  char c3 = 'c';
  char c4 = 'd';
  char c5 = 'e';
   
  Serial.begin(9600);

  vSchedulerInit();

  vSchedulerPeriodicTaskCreate(testFunc1, "T1", configMINIMAL_STACK_SIZE, &c1, 0, &xHandle1, pdMS_TO_TICKS(0), pdMS_TO_TICKS(2000), pdMS_TO_TICKS(450), pdMS_TO_TICKS(2000));
  vSchedulerPeriodicTaskCreate(testFunc2, "T2", configMINIMAL_STACK_SIZE, &c2, 1, &xHandle2, pdMS_TO_TICKS(100), pdMS_TO_TICKS(4000), pdMS_TO_TICKS(450), pdMS_TO_TICKS(1000));
  
  vSchedulerAperiodicTaskCreate( testFuncA1, "A1", "A1-1", pdMS_TO_TICKS(450), pdMS_TO_TICKS(0) );
  vSchedulerAperiodicTaskCreate( testFuncA2, "A2", "A2-1", pdMS_TO_TICKS(450), pdMS_TO_TICKS(50) );
  vSchedulerAperiodicTaskCreate( testFuncA3, "A3", "A3-1", pdMS_TO_TICKS(450), pdMS_TO_TICKS(1050) );

  vSchedulerStart();

  /* If all is well, the scheduler will now be running, and the following line
  will never be reached. */
  
  for( ;; );
}
