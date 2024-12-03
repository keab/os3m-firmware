/* USER CODE BEGIN Header */
/*
Copyright(C) 2023 Colton Baldridge

This program is free software : you can redistribute it and /or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.If not, see < https://www.gnu.org/licenses/>.
*/
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "STM_LDC16xx.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

//Set the size of the averaging window. Set to 1 if you want no averaging at all
#define WIN_SIZE 5

typedef struct Sensor_st {
  uint8_t LDCAdr;
  uint8_t channel;
  uint32_t cal;
  uint32_t val;
  int32_t diff;
  uint32_t vals[WIN_SIZE];
} Sensor_st;

typedef enum {
  HONED = 0,
  ACTIVE = 1, 
} honing_state_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BOOTLOADER_ADDRESS 0x1FFFC400 // address of the bootloader ROM


#define LDC_HONE_DEADBAND 30 // the magnitude of all channel readings should be less than this before LDC honing begins (honing counter is increased)
#define LDC_HONE_PERIOD 40 // this many samples should pass within LDC_HONE_DEADBAND before LDC honing (and Autozero) executes
//Autozeroing parameter
//If the summed changes from one sampling-loop to the next is less than this value, honing counter will be increased, eventually leading to zeroing. 
//If you experience that the zeroing is done when you don't want to, try reducing this value
//Note that the stiffer your flexure part is, the lower this number must be, and vice versa. Also, the larger WIN_SIZE, the smaller this parameter needs to be.
//Set it to 0 to disable Autozero
#define AUTOZERO_MAX_TOTAL_CHANGE 10 

// Scaling factors are set to give good results with the "custom target" and Spacemouse's default driver settings
#define X_SCALE_FACTOR -1.68 
#define Y_SCALE_FACTOR 1.68
#define Z_SCALE_FACTOR -0.2
#define RX_SCALE_FACTOR 1
#define RY_SCALE_FACTOR -1
#define RZ_SCALE_FACTOR -1.68

//This parameter is used to scale the sensor data uniformly. 
//If you want to increase or decrease the overall gain, you can do it here.
#define COMMON_DENOM 125


//Constants to index the different sensors. Not to be changed.
#define NUM_SENSORS 6
#define LDC1CH0_id 0
#define LDC1CH1_id 1
#define LDC1CH2_id 2
#define LDC1CH3_id 3
#define LDC2CH0_id 4
#define LDC2CH1_id 5

//Bit in USB report 0x03 corresponding to the respective spacemouse-button. Not to be changed.
#define BUTTON_1_BIT 12
#define BUTTON_2_BIT 13
#define BUTTON_3_BIT 14
#define BUTTON_4_BIT 15
#define BUTTON_ESC_BIT 22
#define BUTTON_CTRL_BIT 25
#define BUTTON_ALT_BIT 23
#define BUTTON_SHIFT_BIT 24
#define BUTTON_MENU_BIT 0
#define BUTTON_VIEW_FIT_BIT 1
#define BUTTON_VIEW_FRONT_BIT 5
#define BUTTON_VIEW_RIGHT_BIT 4
#define BUTTON_VIEW_TOP_BIT 2
#define BUTTON_VIEW_ROLLL90_BIT 8
#define BUTTON_ROT_TOGGLE_BIT 26

//Here select/define which function each os3m button (each connected to a TP) should have.
//Comment out the lines you don't use (i.e. if no button is connected or you use the TP for something else)
//Note that buttons can also be assigned other functions in the PC driver
//All buttons are active low, i.e. the switch shall be between TP and GND.
#define TP1_BIT BUTTON_VIEW_FIT_BIT
#define TP2_BIT BUTTON_VIEW_FRONT_BIT
#define TP3_BIT BUTTON_ROT_TOGGLE_BIT
#define TP4_BIT BUTTON_CTRL_BIT


/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/

/* USER CODE BEGIN PFP */
void SystemClock_Config(void);


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void sendGamepadReport(int16_t x, int16_t y, int16_t z, int16_t rx, int16_t ry, int16_t rz)
{
 // int16_t buffer[6] = {x, y, z, rx, ry, rz};
  uint8_t buffer_trans[7] = {0x01, LOBYTE(x), HIBYTE(x), LOBYTE(y), HIBYTE(y), LOBYTE(z), HIBYTE(z)};
  uint8_t buffer_rot[7] = {0x02, LOBYTE(rx), HIBYTE(rx), LOBYTE(ry), HIBYTE(ry), LOBYTE(rz), HIBYTE(rz)};

  //uint8_t buffer_full[14] = {0x01, LOBYTE(x), HIBYTE(x), LOBYTE(y), HIBYTE(y), LOBYTE(z), HIBYTE(z), 0x02, LOBYTE(rx), HIBYTE(rx), LOBYTE(ry), HIBYTE(ry), LOBYTE(rz), HIBYTE(rz)};

  USBD_HID_SendReport(&hUsbDeviceFS, &buffer_trans, sizeof(buffer_trans));
  HAL_Delay(2);
  USBD_HID_SendReport(&hUsbDeviceFS, &buffer_rot, sizeof(buffer_rot));
}


/*
Send a button report.

For some currently unknown reason, if the knob is moved at the same time as a button is pressed, 
the two highest bytes transmitted (buffer_but[5] and buffer_but[6]) will contain non-zero values.
No effects of this on the pc side have been noticed so far.
This was *not* observed when tracing a Spacemouse Pro.

*/
void sendGamepadReportTpButtons(void)
{
  static uint8_t last_sent = 0;
  uint8_t new_val = 0;
  uint8_t buffer_but[7] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  //memset(buffer_but + 1, 0, 6*sizeof(uint8_t));

  #ifdef TP1_BIT
    const uint8_t TP1_By = 1 + (TP1_BIT / 8);
    const uint8_t TP1_bi = TP1_BIT % 8;
    if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_RESET)
    {
      buffer_but[TP1_By] |= 1 << TP1_bi;
      new_val |= 1;
    }
  #endif
  #ifdef TP2_BIT
    const uint8_t TP2_By = 1 + (TP2_BIT / 8);
    const uint8_t TP2_bi = TP2_BIT % 8;
    if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_RESET)
    {
      buffer_but[TP2_By] |= 1 << TP2_bi;
      new_val |= 2;
    }
  #endif
  #ifdef TP3_BIT
    const uint8_t TP3_By = 1 + (TP3_BIT / 8);
    const uint8_t TP3_bi = TP3_BIT % 8;
    if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET)
    {
      buffer_but[TP3_By] |= 1 << TP3_bi;
      new_val |= 4;
    }
  #endif
  #ifdef TP4_BIT
    const uint8_t TP4_By = 1 + (TP4_BIT / 8);
    const uint8_t TP4_bi = TP4_BIT % 8;
    if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET)
    {
      buffer_but[TP4_By] |= 1 << TP4_bi;
      new_val |= 8;
    }
  #endif


  if (new_val != last_sent)
  {
    HAL_Delay(10); //For some reason this delay is needed to always get the packet sent. 5ms was not enough.
    
    USBD_HID_SendReport(&hUsbDeviceFS, &buffer_but, sizeof(buffer_but));

    last_sent = new_val;
  }
}

int16_t boundToInt16(int32_t value) {
    if (value < INT16_MIN) {
        return INT16_MIN;
    } else if (value > INT16_MAX) {
        return INT16_MAX;
    } else {
        return (int16_t)value;
    }
}

void jumpToBootloader(void) {
  // Taken from https://community.st.com/t5/stm32-mcus/how-to-jump-to-system-bootloader-from-application-code-on-stm32/tac-p/596379/highlight/true#M617 
  void (*SysMemBootJump)(void);
  uint8_t i;

  __disable_irq();
  // Reset USB
  USB->CNTR = 0x0003;

  //De-init all peripherals
  HAL_I2C_DeInit(&hi2c1);
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 );

  // Disable Systick
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  // Reset clock to default
  HAL_RCC_DeInit();

  // Clear all interrupt bits
  for (i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }

  __enable_irq();

  SysMemBootJump = (void (*)(void)) (*((uint32_t *) (BOOTLOADER_ADDRESS + 4)));
  __set_MSP(*(uint32_t *)BOOTLOADER_ADDRESS);
  SysMemBootJump();

  while (1); // Just in case...
}

 LDC_configReg default_config[] = {
  { LDC16xx_CLOCK_DIVIDERS_CH0,   0x5002 }, // get weird behavior with other dividers like 1002, just stick with this for now
  { LDC16xx_CLOCK_DIVIDERS_CH1,   0x5002 },
  { LDC16xx_CLOCK_DIVIDERS_CH2,   0x5002 },
  { LDC16xx_CLOCK_DIVIDERS_CH3,   0x5002 },
  { LDC16xx_SETTLECOUNT_CH0,      0x0040 },
  { LDC16xx_SETTLECOUNT_CH1,      0x0040 },
  { LDC16xx_SETTLECOUNT_CH2,      0x0040 },
  { LDC16xx_SETTLECOUNT_CH3,      0x0040 },
  { LDC16xx_RCOUNT_CH0,           0x1fff },
  { LDC16xx_RCOUNT_CH1,           0x1fff },
  { LDC16xx_RCOUNT_CH2,           0x1fff },
  { LDC16xx_RCOUNT_CH3,           0x1fff },
  { LDC16xx_DRIVE_CURRENT_CH0,    0xD000 },
  { LDC16xx_DRIVE_CURRENT_CH1,    0xD000 },
  { LDC16xx_DRIVE_CURRENT_CH2,    0xD000 },
  { LDC16xx_DRIVE_CURRENT_CH3,    0xD000 },
  { LDC16xx_ERROR_CONFIG,         0x0001 },
  { LDC16xx_MUX_CONFIG,           LDC16xx_BITS_DEGLITCH_3_3Mhz | LDC16xx_BITS_AUTOSCAN_EN | LDC16xx_BITS_RR_SEQUENCE_CH0_CH1_CH2_CH3 },
  { LDC16xx_CONFIG,               LDC16xx_BITS_ACTIVE_CHAN_CH0 // select channel 0
                                  | LDC16xx_BITS_AUTO_AMP_DIS // if 0, IDRIVE constantly adjusts
                                  | LDC16xx_BITS_RP_OVERRIDE_EN // if 0, IDRIVE auto calibrates on sensor startup
                                  | LDC16xx_BITS_INTB_DIS // disable intb pin
                                  | LDC16xx_BITS_REF_CLK_SRC // 40MHz ext clock
                                  // | LDC16xx_BITS_SLEEP_MODE_EN // start in sleep mode
                                  }, 
};

LDC_configReg SLEEP_CONFIG_config = { LDC16xx_CONFIG, LDC16xx_BITS_ACTIVE_CHAN_CH0 | LDC16xx_BITS_AUTO_AMP_DIS | LDC16xx_BITS_INTB_DIS | LDC16xx_BITS_SLEEP_MODE_EN};
LDC_configReg AWAKE_CONFIG_config = { LDC16xx_CONFIG, LDC16xx_BITS_ACTIVE_CHAN_CH0 | LDC16xx_BITS_AUTO_AMP_DIS | LDC16xx_BITS_INTB_DIS};

#define LDC_CONFIG_SIZE sizeof(default_config)/sizeof(LDC_configReg)
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

  #ifdef TP1_BIT
  MX_GPIO_Init_Tp_Button(GPIO_PIN_4);
  #endif
  #ifdef TP2_BIT
  MX_GPIO_Init_Tp_Button(GPIO_PIN_5);
  #endif
  #ifdef TP3_BIT
  MX_GPIO_Init_Tp_Button(GPIO_PIN_6);
  #endif
  #ifdef TP4_BIT
  MX_GPIO_Init_Tp_Button(GPIO_PIN_7);
  #endif
  
  MX_I2C1_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  uint8_t is_earlier_revision = 0;
  // Check if user has earlier revision of board where UART pins are shorted by default
  if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_SET) {
    is_earlier_revision = 1;
  }
  // Reset the LDCs
  resetDevice(0x2a);
  resetDevice(0x2b);
  // Load the config onto the LDC1614
  loadConfig(0x2a, default_config, LDC_CONFIG_SIZE);
  // Change the mux register in the config to only scan 2 channels instead of 4 (for LDC1612)
  LDC_configReg LDC1612_MUX_config = {LDC16xx_MUX_CONFIG, LDC16xx_BITS_DEGLITCH_3_3Mhz | LDC16xx_BITS_AUTOSCAN_EN | LDC16xx_BITS_RR_SEQUENCE_CH0_CH1};
  default_config[16] = LDC1612_MUX_config;
  // Send the new config to the LDC1612
  loadConfig(0x2b, default_config, LDC_CONFIG_SIZE);


  Sensor_st* sensors[NUM_SENSORS];
  uint8_t i,j,w_cnt=0;
  uint32_t tmp;
  const uint32_t denom = COMMON_DENOM * WIN_SIZE;

  //Initialise all sensor structs
  for(i = 0; i < NUM_SENSORS; i++) {
    sensors[i] = (Sensor_st*)malloc(sizeof(Sensor_st));
    sensors[i]->LDCAdr = 0x2a; //Note: Some will be changed to 0x2b below
    sensors[i]->val = 0;
    sensors[i]->diff = 0;
    sensors[i]->cal =  0;
  }
  sensors[LDC2CH0_id]->LDCAdr = 0x2b;
  sensors[LDC2CH1_id]->LDCAdr = 0x2b;
  sensors[LDC1CH0_id]->channel = 0;
  sensors[LDC1CH1_id]->channel = 1;
  sensors[LDC1CH2_id]->channel = 2;
  sensors[LDC1CH3_id]->channel = 3;
  sensors[LDC2CH0_id]->channel = 0;
  sensors[LDC2CH1_id]->channel = 1;
  
  //Fill the averaging window(s) with samples and calculate the current value(s)
  for(j = 0; j < WIN_SIZE; j++) {
    for(i = 0; i < NUM_SENSORS; i++) {
      readChannel(sensors[i]->LDCAdr, sensors[i]->channel, &tmp);
      sensors[i]->vals[j] = tmp / denom;
      sensors[i]->val += sensors[i]->vals[j];
    }
    HAL_Delay(20);
  }
  //Set the zero position
  for(i = 0; i < NUM_SENSORS; i++) {
    sensors[i]->cal = sensors[i]->val;
  }



  // Honing counter
  uint32_t ldc_honing_count = 0;
  honing_state_t ldc_honing_state = HONED;
  uint8_t in_deadband = 1;

// Autozero variables
  uint8_t probably_home = 0;
  uint32_t total_change = 0;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_SET && !is_earlier_revision) {
      jumpToBootloader();
    }
    // Grab a new set of values
    
    total_change = 0;
    in_deadband = 1;


    for(i = 0; i< NUM_SENSORS; i++) {
      //Get the latest reading, divided by the window size and COMON_DENOM
      readChannel(sensors[i]->LDCAdr, sensors[i]->channel, &tmp);
      sensors[i]->vals[w_cnt] = tmp / denom;
      
      //Calculate the average
      sensors[i]->val = 0;
      for (j = 0 ; j < WIN_SIZE; j++){
        sensors[i]->val += sensors[i]->vals[j];
      }
      
      //Calculate the change(s) from previous sample(s) and sum them up
      //OBS uses the diff value calculated for the _previous_ sample
      total_change += abs((int32_t)sensors[i]->val + sensors[i]->diff - (int32_t)sensors[i]->cal);

      //calculate the new diff value
      sensors[i]->diff = ((int32_t)(sensors[i]->cal) - (int32_t)(sensors[i]->val));

      if(abs(sensors[i]->diff) > LDC_HONE_DEADBAND){
        in_deadband = 0;
      }
    }

    w_cnt = (w_cnt + 1) % WIN_SIZE;


   //For Autozero, judge whether the knob is still
    probably_home = total_change < AUTOZERO_MAX_TOTAL_CHANGE;

    // Perform honing (if necessary)
    switch (ldc_honing_state){
      case HONED:
        if( !in_deadband || !probably_home)
        {
          ldc_honing_state = ACTIVE;
        }
        break;
      case ACTIVE:
        if( in_deadband )
        {
          ldc_honing_count++;
        } 
        else if( probably_home )
        {
          ldc_honing_count++;
        } 
        else 
        {
          ldc_honing_count = 0;
        }
        if(ldc_honing_count > LDC_HONE_PERIOD)
        {
          for(i = 0; i< NUM_SENSORS; i++) {
            sensors[i]->cal = sensors[i]->val;
            sensors[i]->diff = 0;
          }

          ldc_honing_count = 0;
          ldc_honing_state = HONED;
        }
      default:
        break;
    }   


    // Get sums and differences
    int32_t cm1 = sensors[LDC1CH0_id]->diff + sensors[LDC1CH1_id]->diff;
    int32_t dm1 = sensors[LDC1CH0_id]->diff - sensors[LDC1CH1_id]->diff;
    int32_t cm2 = sensors[LDC1CH2_id]->diff + sensors[LDC1CH3_id]->diff;
    int32_t dm2 = sensors[LDC1CH2_id]->diff - sensors[LDC1CH3_id]->diff;
    int32_t cm3 = sensors[LDC2CH0_id]->diff + sensors[LDC2CH1_id]->diff;
    int32_t dm3 = sensors[LDC2CH0_id]->diff - sensors[LDC2CH1_id]->diff;

    // Compute tranformation
    int32_t z = cm1 + cm2 + cm3;
    int32_t y = - dm1 + dm3;
    int32_t x = - (dm2 - dm1 / 2 - dm3 / 2);
    int32_t rz = dm1 + dm2 + dm3;
    int32_t rx = cm1 / 2 - cm2 + cm3 / 2;
    int32_t ry =  - cm1 + cm3;

    if(in_deadband){
      z = 0;
      y = 0;
      x = 0;
      rz = 0;
      rx = 0;
      ry = 0;
    }

    x = X_SCALE_FACTOR * x;
    y = Y_SCALE_FACTOR * y;
    z = Z_SCALE_FACTOR * z;
    rx = RX_SCALE_FACTOR * rx;
    ry = RY_SCALE_FACTOR * ry;
    rz = RZ_SCALE_FACTOR * rz;
  
    // Send the data.
    sendGamepadReport(boundToInt16(x),boundToInt16(y),boundToInt16(z),boundToInt16(rx),boundToInt16(ry),boundToInt16(rz));
    sendGamepadReportTpButtons();


    // Debug send for if you want the raw coil data
    // sendGamepadReport(
    //   ldc1_ch0_dif,
    //   ldc1_ch1_dif,
    //   ldc1_ch2_dif,
    //   ldc1_ch3_dif,
    //   ldc2_ch0_dif,
    //   ldc2_ch1_dif
    // );

      // Delay a bit for the LDCs to get new readings 
      // (in the future, add INTB pin support so the LDCs can alert the MCU when they have new data ready)
      HAL_Delay(20);


    /* USER CODE END WHILE */
  }

  /* USER CODE BEGIN 3 */
  
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI48;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB|RCC_PERIPHCLK_I2C1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_SYSCLK;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
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
