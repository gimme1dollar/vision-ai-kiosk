/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "crc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "app_x-cube-ai.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// SR-HC14
#define SR_STATE_IDLE 0 // idle
#define SR_STATE_REQ 1	// user requested to measure
#define SR_STATE_TRIG 2 // triggering
#define SR_STATE_WAIT 3 // waiting echo back
#define SR_STATE_ECHO 4 // measuring echo pulse duration

// STEPPER MOTOR
#define STEP_DIRECTION_CW 0
#define STEP_DIRECTION_CCW 1
#define STEP_MODE_DISABLE 0
#define STEP_MODE_STALL 1
#define STEP_MODE_DYNAMIC 2
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

// AI
volatile float img[40000];
volatile uint16_t img_idx = 0;
volatile uint8_t rx_data;

// Timer instances and ticks
extern TIM_HandleTypeDef htim15;
extern TIM_HandleTypeDef htim2;
volatile uint64_t us_count = 0;

// SR-HC14
volatile uint8_t sr_state = SR_STATE_IDLE;
volatile uint64_t sr_trigger_us = 0, sr_echo_us = 0;
volatile uint32_t sr_elapsed_us;

// USART
volatile uint8_t rx_data;

// STEPPER MOTOR
volatile uint32_t step_cnt = 0;
volatile uint32_t step_target = 0;
volatile uint8_t step_mode = STEP_MODE_STALL;
volatile uint8_t step_init = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);
void STEP_turn(uint32_t count_step);
uint16_t SR_ReadDistance(int *sr_state, unsigned long *sr_elapsed_us);
uint64_t HAL_GetTickUS();
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
int __io_putchar(int ch);
int _write(int file, char *ptr, int len);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int age_detection()
{
	float aiout[4];
	int max = 0;

	HAL_UART_Transmit(&huart2, 'a', 1, 1000);
	while(img_idx<40000);

	MX_X_CUBE_AI_Process(img, aiout);

	for (int i = 0; i < 6; i++){
		if(aiout[i] > aiout[max])
			max = i;
	}
	//HAL_UART_Transmit(&huart2, img, 40000, 1000);
	img_idx = 0;
	memset(img, 0, sizeof(img));
	return max;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
	int16_t distance = 0;
	uint8_t tx_data[3] = {0,'\r','\n'};
	uint8_t phase = 0;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
	SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
	MX_GPIO_Init();
	MX_UART4_Init();
	MX_USART1_UART_Init();
	MX_USART2_UART_Init();
	MX_USART3_UART_Init();
	MX_TIM15_Init();
	MX_TIM2_Init();
	MX_CRC_Init();
	MX_X_CUBE_AI_Init();
  /* USER CODE BEGIN 2 */

	HAL_UART_Receive_IT(&huart1, &rx_data, 1);
	HAL_UART_Receive_IT(&huart2, &rx_data, 1);
	HAL_UART_Receive_IT(&huart4, &rx_data, 1);

	if (HAL_TIM_Base_Start_IT(&htim15) != HAL_OK)
	{
		Error_Handler();
	}
	if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK)
	{
		Error_Handler();
	}

	printf("Started\r\n");
	HAL_Delay(100);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	while (1)
	{
		distance = 0;
		for(int i=0; i<3; i++) {
			uint16_t d = SR_ReadDistance(&sr_state, &sr_elapsed_us);
			distance += d;
			HAL_Delay(100);
		}
		distance /= 3;
		printf("Distance %d\r\n", distance);

		if(phase == 0) {
			if(distance < 60) {
				if (HAL_TIM_Base_Stop_IT(&htim15) != HAL_OK)
					Error_Handler();
				if (HAL_TIM_Base_Stop_IT(&htim2) != HAL_OK)
					Error_Handler();

				int age = age_detection();
				printf("Max label %d\r\n", age);

				if (HAL_TIM_Base_Start_IT(&htim15) != HAL_OK)
					Error_Handler();
				if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK)
					Error_Handler();

				tx_data[0] = age +'0';
				HAL_UART_Transmit(&huart4, tx_data, 2, 100);
				HAL_Delay(5000);
				HAL_UART_Transmit(&huart4, tx_data, 2, 100);
				phase = 1;
			}
		}
		else if(phase == 1) {
			if(distance<46.5&&distance>26.5) {
				STEP_turn((double)(46.5-distance)/26.5*14200);
            //200-distance -> 180  14200
            //200-distance -> 153.5  0
            //14200:26.5 = k:a
			}
			else {
				STEP_turn(7100);
			}
		}
		else if(phase == 2) {
			if(distance>60)
				phase = 0;
		}

		HAL_Delay(500);


#if 0
    /* USER CODE END WHILE */

  MX_X_CUBE_AI_Process();
    /* USER CODE BEGIN 3 */
#endif
	}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Configure the main internal regulator output voltage
  */
	if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST) != HAL_OK)
	{
		Error_Handler();
	}
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
	RCC_OscInitStruct.MSIState = RCC_MSI_ON;
	RCC_OscInitStruct.MSICalibrationValue = 0;
	RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
	RCC_OscInitStruct.PLL.PLLM = 1;
	RCC_OscInitStruct.PLL.PLLN = 60;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
	RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}
  /** Initializes the CPU, AHB and APB buses clocks
  */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
	|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
	{
		Error_Handler();
	}
	PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1|RCC_PERIPHCLK_USART2
	|RCC_PERIPHCLK_USART3|RCC_PERIPHCLK_UART4;
	PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
	PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
	PeriphClkInit.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
	PeriphClkInit.Uart4ClockSelection = RCC_UART4CLKSOURCE_PCLK1;
	if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
	{
		Error_Handler();
	}
}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == SR_ECHO_Pin)
	{
		switch (sr_state)
		{
			case SR_STATE_WAIT:
			sr_state = SR_STATE_ECHO;
			sr_echo_us = HAL_GetTickUS();
			break;
			case SR_STATE_ECHO:
			sr_state = SR_STATE_IDLE;
			sr_elapsed_us = HAL_GetTickUS() - sr_echo_us;
			break;
		}
	}
	else if (GPIO_Pin == BUTTON_EXTI13_Pin)
	{
		if (step_mode == STEP_MODE_STALL)
			step_mode = STEP_MODE_DYNAMIC;
		else
			step_mode = STEP_MODE_STALL;

		printf("STEP_ENABLE %d, %d\r\n", step_mode, TIM2->PSC);
	}
	else if (GPIO_Pin == LIMSW_BOT_Pin)
	{
		printf("Limit switch pushed\r\n");
		step_init = 1;
		//step_mode = STEP_MODE_DISABLE;
	}
}

void STEP_turn(uint32_t count_step)
{
	step_target = count_step;
	step_mode = STEP_MODE_DYNAMIC;
}

uint16_t SR_ReadDistance(int *sr_state, unsigned long *sr_elapsed_us)
{
	uint32_t t = HAL_GetTick();
	*sr_state = SR_STATE_REQ;
	while (*sr_state != SR_STATE_IDLE)
	{
		if (*sr_elapsed_us > 38000u)
		{
			return -1;
		}
		if (HAL_GetTick() - t > 500u)
			return -2;
	}
	return *sr_elapsed_us / 58;
}

uint64_t HAL_GetTickUS()
{
	return 10u * us_count;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{

	if (htim->Instance == TIM15)
	{
		// 10 us counter
		us_count++;

		// SR Ultrasonic sensor
		switch (sr_state)
		{
			case SR_STATE_REQ:
			sr_trigger_us = HAL_GetTickUS();
			sr_state = SR_STATE_TRIG;
			HAL_GPIO_WritePin(SR_TRIG_Port, SR_TRIG_Pin, GPIO_PIN_SET);
			break;
			case SR_STATE_TRIG:
			if (HAL_GetTickUS() - sr_trigger_us >= 10)
			{
				sr_state = SR_STATE_WAIT;
				HAL_GPIO_WritePin(SR_TRIG_Port, SR_TRIG_Pin, GPIO_PIN_RESET);
			}
			break;
		}

		// Blink LED2 every seconds
		if (HAL_GetTickUS() % 1000000u == 0)
		{
			HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
		}
	}
	else if (htim->Instance == TIM2)
	{
		if (step_init == 0)
		{
			HAL_GPIO_WritePin(STEP_DIR_Port, STEP_DIR_Pin, GPIO_PIN_SET);
			HAL_GPIO_WritePin(STEP_EN_Port, STEP_EN_Pin, GPIO_PIN_RESET);
			HAL_GPIO_TogglePin(STEP_PULSE_Port, STEP_PULSE_Pin);
		}
		else
		{
			switch (step_mode)
			{
				case STEP_MODE_DISABLE:
				HAL_GPIO_WritePin(STEP_EN_Port, STEP_EN_Pin, GPIO_PIN_SET);
				break;
				case STEP_MODE_STALL:
				HAL_GPIO_WritePin(STEP_EN_Port, STEP_EN_Pin, GPIO_PIN_RESET);
				break;
				case STEP_MODE_DYNAMIC:
				HAL_GPIO_WritePin(STEP_EN_Port, STEP_EN_Pin, GPIO_PIN_RESET);
				if (step_cnt < step_target)
				{
					step_cnt++;
					HAL_GPIO_WritePin(STEP_DIR_Port, STEP_DIR_Pin, GPIO_PIN_RESET);
					HAL_GPIO_TogglePin(STEP_PULSE_Port, STEP_PULSE_Pin);
				}
				else if(step_cnt > step_target)
				{
					step_cnt--;
					HAL_GPIO_WritePin(STEP_DIR_Port, STEP_DIR_Pin, GPIO_PIN_SET);
					HAL_GPIO_TogglePin(STEP_PULSE_Port, STEP_PULSE_Pin);
				}
				else
				{
					step_mode = STEP_MODE_STALL;
				}

				break;
			}
		}
	}
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART1)
	{
		HAL_UART_Receive_IT(&huart1, &rx_data, 1);
		HAL_UART_Transmit(&huart1, &rx_data, 1, 100);
	}
	if(huart->Instance == USART2) {
		HAL_UART_Receive_IT(&huart2, &rx_data, 1);
//		HAL_UART_Transmit(&huart2, &rx_data, 1, 1);
		if(img_idx<40000)
			img[img_idx++] = rx_data;
		else
			HAL_UART_Transmit(&huart2, &rx_data, 1, 1);

	}
	if (huart->Instance == UART4)
	{
		HAL_UART_Receive_IT(&huart4, &rx_data, 1);
		HAL_UART_Transmit(&huart4, &rx_data, 1, 100);
	}
}

int __io_putchar(int ch)
{
	while (HAL_OK != HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 3000))
		;
	return ch;
}

int _write(int file, char *ptr, int len)
{
	int DataIdx;

	for (DataIdx = 0; DataIdx < len; DataIdx++)
	{
		__io_putchar(*ptr++);
	}
	return len;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1)
	{
	}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
