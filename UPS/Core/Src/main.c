/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "SC8815.h"
#include "stm32f1xx_hal.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */


/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
		uint16_t VbusVolt;
    uint16_t VbusCurr;
    uint16_t BattVolt;
    uint16_t BattCurr;
		uint8_t ram[3];
		extern SC8815_InterruptStatusTypeDef SC8815_InterruptMaskInitStruct;
		uint8_t POWERIN_CHANGED=1;
		uint16_t count=0;
		uint8_t CHONGDIAN=1;
		uint16_t VLIM=4950;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void I2C_WriteRegByte(uint8_t SlaveAddress, uint8_t RegAddress, uint8_t ByteData){   //通过I2C向设备寄存器写一个字节

HAL_I2C_Mem_Write(&hi2c1, SlaveAddress, RegAddress, I2C_MEMADD_SIZE_8BIT, &ByteData, 1, 1000);
/* 第1个参数为I2C操作句柄
   第2个参数为从机设备地址
   第3个参数为从机寄存器地址
   第4个参数为从机寄存器地址长度
   第5个参数为发送的数据的起始地址
   第6个参数为传输数据的大小
   第7个参数为操作超时时间 　　*/

};
uint8_t I2C_ReadRegByte(uint8_t SlaveAddress, uint8_t RegAddress){                   //通过I2C从设备寄存器读一个字节
	uint8_t param;
	HAL_I2C_Mem_Read(&hi2c1, SlaveAddress, RegAddress, I2C_MEMADD_SIZE_8BIT, &param, 1, 1000);
	return param;
}

void SoftwareDelay(uint8_t ms){//软件延迟
	HAL_Delay(ms);
};
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
	if(GPIO_Pin==GPIO_PIN_1){
		//SC8815_OTG_Enable();
		HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET);
		POWERIN_CHANGED=1;
	}else 
	if(GPIO_Pin==GPIO_PIN_15){
    SC8815_ReadInterrupStatus(&SC8815_InterruptMaskInitStruct);     //MCU 收到 SC8815 中断后调用此函数读 SC8815 中断 (读中断状态后将清除中断状态位)
    if (SC8815_InterruptMaskInitStruct.AC_OK == 1)
    {
        // AC_OK 中断处理代码
    }
    else if (SC8815_InterruptMaskInitStruct.EOC == 1)
    {
        // EOC 中断处理代码
    }

}


}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

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
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
	//HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
//  HAL_I2C_Init(&hi2c1);
//	HAL_I2C_Init(&hi2c2);
	SC8815_Init_Demo();
	ram[2]=0x93;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
		HAL_Delay(500);
		count++;
		if(POWERIN_CHANGED){
		HAL_Delay(500);//延时使得检测口稳定
			if(HAL_GPIO_ReadPin(POWER_GPIO_Port,POWER_Pin)==GPIO_PIN_SET){
				//SC8815_OTG_Disable();
				HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
				POWERIN_CHANGED=0;
			}
		
		}
     BattVolt = SC8815_Read_BATT_Voltage();
		ram[0] = (uint8_t)(BattVolt & 0xFF);
    ram[1] = (uint8_t)((BattVolt >> 8) & 0xFF);
		HAL_UART_Transmit(&huart2,ram,3,100);
//     BattCurr = SC8815_Read_BATT_Current();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		
		if(BattVolt<0x3C00){
			CHONGDIAN=1;
		}else if(BattVolt>0x3DE0){
			CHONGDIAN=0;
		}
			if(count>1000){
				VLIM=CHONGDIAN?4950:5000;
				SC8815_OTG_Disable();
			SoftwareDelay(5);
			    SC8815_SetBatteryCurrLimit(2000);//VBAT限流
    SC8815_SetBusCurrentLimit(VLIM);//VBUS限流
    SC8815_VINREG_SetVoltage(16400);//VBAT电压,设置低一点
		SC8815_SetOutputVoltage(VLIM);//OUTPUT电压
		SC8815_PGATE_Enable();//打开OUTPUT与VBUS连接
		SC8815_GPO_Enable();//打开INPUT与VBUS连接
    SC8815_OTG_Enable();
				count=0;
	
	}
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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
