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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdlib.h>      // rand(), srand()
#include <string.h>      // memset(), memcpy()
#include <stdio.h>       // snprintf()
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* -------------------- TYPY ENUM – logika gry ----------------------------- */
typedef enum {
    ST_EKRAN_POWITALNY,
    ST_ZASADY,
    ST_MENU_TRYB_CZEKAJ,
    ST_MENU_AKCJA_CZEKAJ,
    ST_MENU_UTWOR_CZEKAJ,
    ST_MENU_POZIOM, ST_MENU_POZIOM_CZEKAJ, ST_MENU_PRED_CZEKAJ,
    ST_MENU_DZW_CZEKAJ,
    ST_BEZCZYNNY, ST_START_PAUZA,
    ST_POKAZ, ST_ODTWARZAJ, ST_OCZEKUJ,
    ST_SUKCES, ST_PORAZKA,
    ST_PAUZA_SUKCES, ST_PAUZA_PORAZKA,
    ST_KONCERT,
    ST_WOLNA_GRA,
    ST_KONIEC_CZEKAJ
} stan_gry_t;

typedef enum {
    TRYB_MEMORY_MIX = 0,   /* losowe sekwencje (dawny Simon)              */
    TRYB_MELODIE    = 1,   /* nauka melodii krok po kroku + koncert       */
    TRYB_ODTWARZAJ  = 2,   /* odtwarzacz wybranych melodii z diodami      */
    TRYB_FREESTYLE  = 3    /* swobodne granie na 7 klawiszach jak pianino */
} tryb_gry_t;

typedef enum { TR_LATWY, TR_TRUDNY }  trudnosc_t;
typedef enum { PR_WOLNO, PR_SREDNIO, PR_SZYBKO } predkosc_t;

/* ---------------- LCD – stany i kontekst ---------------- */
typedef enum
{
    LCD_BEZCZYN        = 0,   /* brak pracy do wykonania            */
    LCD_WYSLIJ_POLBAJT,       /* nadaj półbajt (hi albo lo)         */
    LCD_CZEKAJ_40US,          /* przerwa ≈ 40 µs (HD44780 timing)   */
    LCD_GOTOWE               /* bajt wysłany – wróć do BEZCZYN     */
} lcd_krok_t;

typedef struct
{
    lcd_krok_t krok;          /* aktualny krok automatu            */
    uint8_t    polbajt_hi;    /* górny  nibble + bity sterujące    */
    uint8_t    polbajt_lo;    /* dolny  nibble + bity sterujące    */
    uint8_t    sterowanie;    /* RS / RW / BACKLIGHT               */
    uint8_t    etap;          /* 0 = hi, 1 = lo                    */
    uint16_t   opoznienie_ms; /* licznik 1-ms (obsługuje TIM2)     */
} lcd_ctx_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* -------------------- PARAMETRY GRY -------------------------------------- */
#define DL_MAX_SEKW      50U      /* maksymalna długość sekwencji            */
#define LICZBA_LED        7U      /* liczba diod i przycisków                */
#define LICZBA_UTWOROW   10U      /* liczba dostępnych melodii               */
#define LICZBA_TRYBOW     4U      /* Memory Mix, Melodie, Odtwarzaj, Freestyle */

/* -------------------- SPRZĘT – mapowanie pinów --------------------------- */
static const uint16_t LED_PINY [LICZBA_LED] = {
    GPIO_PIN_6, GPIO_PIN_7, GPIO_PIN_8, GPIO_PIN_9,
    GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_9
};
static GPIO_TypeDef * LED_PORTY[LICZBA_LED] = {
    GPIOC, GPIOC, GPIOC, GPIOC,
    GPIOC, GPIOC, GPIOB
};
static const uint16_t PRZ_PINY [LICZBA_LED] = {
    GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15, GPIO_PIN_12,
    GPIO_PIN_11, GPIO_PIN_10, GPIO_PIN_8
};

/* Makro testujące dowolny przycisk startu                                   */
#define PRZYCISK_START  ( !HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[0]) || \
                          !HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[1]) || \
                          !HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[2]) || \
                          !HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[3]) || \
                          !HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[4]) || \
                          !HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[5]) || \
                          !HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[6]) )

/* -------- LCD – stałe sprzętowe -------- */
#define LCD_I2C_ADDR   (0x27 << 1)   /* = 0x4E */
#define LCD_BACKLIGHT  0x08
#define LCD_EN         0x04
#define LCD_RW         0x02
#define LCD_RS         0x01

/* -------- kontekst i bufor I²C -------- */
static volatile lcd_ctx_t lcd;       /* globalny kontekst sterownika */
static volatile uint8_t   tx_buf[2]; /* 2-bajtowy bufor do I²C-IT   */

/* -------- Buzzer – wybór stroju:
 * 4  = niski     (C4..B4: 262..494 Hz)
 * 45 = ŚRODKOWY  (G4..F#5: 392..740 Hz – złoty środek, ciepłe brzmienie bez piszczenia!)
 * 5  = wysoki    (C5..B5: 523..988 Hz)
 * 6  = b. wysoki (C6..B6: 1047..1976 Hz)
 * ------------------------------------------------------------------------ */
#define OKTAWA_BUZZERA 45

/* 4. oktawa (C4..B4) – pierwotna (262..494 Hz) */
#define NUTA_C4  262
#define NUTA_D4  294
#define NUTA_E4  330
#define NUTA_F4  349
#define NUTA_G4  392
#define NUTA_A4  440
#define NUTA_B4  494

/* ŚRODKOWA GAMA (G4..F#5: 392..740 Hz) – dokładnie w połowie między 4. a 5. oktawą */
#define NUTA_FS5 740

/* 5. oktawa (C5..B5) – wyższa (523..988 Hz) */
#define NUTA_C5  523
#define NUTA_D5  587
#define NUTA_E5  659
#define NUTA_F5  698
#define NUTA_G5  784
#define NUTA_A5  880
#define NUTA_B5  988

/* 6. oktawa (C6..B6) – bardzo wysoka (1047..1976 Hz) */
#define NUTA_C6  1047
#define NUTA_D6  1175
#define NUTA_E6  1319
#define NUTA_F6  1397
#define NUTA_G6  1568
#define NUTA_A6  1760
#define NUTA_B6  1976

#define PAUZA    0

#if (OKTAWA_BUZZERA == 6)
static const uint16_t LED_NUTY[LICZBA_LED] = {
    NUTA_C6, NUTA_D6, NUTA_E6, NUTA_F6,
    NUTA_G6, NUTA_A6, NUTA_B6
};
#elif (OKTAWA_BUZZERA == 5)
static const uint16_t LED_NUTY[LICZBA_LED] = {
    NUTA_C5, NUTA_D5, NUTA_E5, NUTA_F5,
    NUTA_G5, NUTA_A5, NUTA_B5
};
#elif (OKTAWA_BUZZERA == 45)
/* Złoty środek: tonacja G-dur (392..740 Hz) – brak buczenia na dole i brak piszczenia u góry */
static const uint16_t LED_NUTY[LICZBA_LED] = {
    NUTA_G4, NUTA_A4, NUTA_B4, NUTA_C5,
    NUTA_D5, NUTA_E5, NUTA_FS5
};
#else
static const uint16_t LED_NUTY[LICZBA_LED] = {
    NUTA_C4, NUTA_D4, NUTA_E4, NUTA_F4,
    NUTA_G4, NUTA_A4, NUTA_B4
};
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim16;

/* USER CODE BEGIN PV */

/* -------------------- ZMIENNE GLOBALNE GRY -------------------------------- */
static volatile stan_gry_t stan_gry = ST_BEZCZYNNY;
static volatile tryb_gry_t tryb_gry = TRYB_MEMORY_MIX;

/* bufor sekwencji + kursory ------------------------------------------------ */
static uint8_t sekwencja[DL_MAX_SEKW];
static uint8_t dl_sekw      = 0;   /* aktualna długość poziomu            */
static uint8_t dl_docelowa  = 0;   /* długość wybranej melodii            */
static volatile uint8_t poz_wejscia = 0; /* pozycja sprawdzania w sekwencji */

/* ustawienia użytkownika --------------------------------------------------- */
static volatile trudnosc_t  trudnosc      = TR_LATWY;
static volatile predkosc_t  predkosc_odtw = PR_SREDNIO;
static uint8_t  dzwiek_wlaczony           = 1;

/* indykatory/menu ---------------------------------------------------------- */
static uint8_t idx_menu_tryb   = 0;
static uint8_t idx_menu_akcja  = 0;
static uint8_t idx_menu_utwor  = 0;
static uint8_t idx_menu_poziom = 0;
static uint8_t idx_menu_pred   = 1;
static uint8_t idx_menu_dzwiek = 0;
static uint8_t idx_menu_koniec = 0;

/* baza melodii do trybów "Melodie" oraz "Odtwarzaj" (indeksy diod 0..6)     */
typedef struct {
    const char    *nazwa_menu;
    const uint8_t *nuty;
    uint8_t        dlugosc;
} utwor_t;

/* 1. Oda do radości (E E F G | G F E D | C C D E | E D D) */
static const uint8_t nuty_oda[] = {
    2, 2, 3, 4,  4, 3, 2, 1,  0, 0, 1, 2,  2, 1, 1
};
/* 2. Wlazł kotek na płotek (G E E | F D D | C E G | G E E | F D D | C E C) */
static const uint8_t nuty_kotek[] = {
    4, 2, 2,  3, 1, 1,  0, 2, 4,  4, 2, 2,  3, 1, 1,  0, 2, 0
};
/* 3. Panie Janie (C D E C | C D E C | E F G | E F G) */
static const uint8_t nuty_janie[] = {
    0, 1, 2, 0,  0, 1, 2, 0,  2, 3, 4,  2, 3, 4
};
/* 4. Sto lat (G E G E | G A G F E F | F D F D | F G) */
static const uint8_t nuty_stolat[] = {
    4, 2, 4, 2,  4, 5, 4, 3, 2, 3,  3, 1, 3, 1, 3, 4
};
/* 5. Super Mario Bros (E E E C E G C | C G E A B A G | E G A F G E C D B) */
static const uint8_t nuty_mario[] = {
    2, 2, 2, 0, 2, 4, 0,  0, 4, 2, 5, 6, 5, 4,  2, 4, 5, 3, 4, 2, 0, 1, 6
};
/* 6. Jingle Bells (E E E | E E E | E G C D E | F F F F | F E E E | E D D E D G) */
static const uint8_t nuty_jingle[] = {
    2, 2, 2,  2, 2, 2,  2, 4, 0, 1, 2,  3, 3, 3, 3,  3, 2, 2, 2,  2, 1, 1, 2, 1, 4
};
/* 7. Twinkle Twinkle / Gwiazdeczka (C C G G A A G | F F E E D D C) */
static const uint8_t nuty_gwiazdka[] = {
    0, 0, 4, 4, 5, 5, 4,  3, 3, 2, 2, 1, 1, 0
};
/* 8. Star Wars – Marsz Imperialny (E E E C G E C G E | B B B C G E C G E) */
static const uint8_t nuty_starwars[] = {
    2, 2, 2, 0, 4, 2, 0, 4, 2,  6, 6, 6, 0, 4, 2, 0, 4, 2
};
/* 9. Tetris – Korobeiniki (G D E F E D C | C E G F E D | D E F G E C C) */
static const uint8_t nuty_tetris[] = {
    4, 1, 2, 3, 2, 1, 0,  0, 2, 4, 3, 2, 1,  1, 2, 3, 4, 2, 0, 0
};
/* 10. Bella Ciao (C F G A F | C F G A F | C F G A G F A G F B B B) */
static const uint8_t nuty_bella[] = {
    0, 3, 4, 5, 3,  0, 3, 4, 5, 3,  0, 3, 4, 5, 4, 3, 5, 4, 3, 6, 6, 6
};

static const utwor_t BAZA_UTWOROW[LICZBA_UTWOROW] = {
    { " 1.Oda radosci >", nuty_oda,      sizeof(nuty_oda)      },
    { "< 2.Wlazl kotek>", nuty_kotek,    sizeof(nuty_kotek)    },
    { "< 3.Panie Janie>", nuty_janie,    sizeof(nuty_janie)    },
    { "<  4.Sto lat   >", nuty_stolat,   sizeof(nuty_stolat)   },
    { "< 5.Super Mario>", nuty_mario,    sizeof(nuty_mario)    },
    { "< 6.JingleBells>", nuty_jingle,   sizeof(nuty_jingle)   },
    { "< 7.Gwiazdeczka>", nuty_gwiazdka, sizeof(nuty_gwiazdka) },
    { "< 8.Star Wars  >", nuty_starwars, sizeof(nuty_starwars) },
    { "< 9.Tetris     >", nuty_tetris,   sizeof(nuty_tetris)   },
    { "<10.Bella Ciao  ", nuty_bella,    sizeof(nuty_bella)    }
};

/* czasy LED ON/OFF dla trzech prędkości ------------------------------------ */
static const uint16_t czas_on [3]  = {400, 280, 150};
static const uint16_t czas_off[3]  = {200, 120,  80};

/* timery miękkie ----------------------------------------------------------- */
static uint32_t deadline_splash  = 0;
static uint32_t deadline_pauza   = 0;
static uint32_t deadline_odtw    = 0;
static volatile uint32_t deadline_led_off[LICZBA_LED] = {0};
static volatile uint32_t deadline_buzzer  = 0;

/* zmienne pomocnicze dla trybu swobodnego grania (Freestyle) --------------- */
static uint32_t czas_trzymania_wyjscia = 0;
static int8_t   aktywna_nuta_freestyle = -1;

/* sterownik odtwarzacza ---------------------------------------------------- */
static uint8_t  idx_odtw = 0;      /* krok w sekwencji */
static uint8_t  faza_odtw = 0;     /* 0=OFF 1=ON       */

/* flagi przycisków (ustawiane w IRQ) -------------------------------------- */
volatile uint8_t flg_up = 0, flg_dn = 0, flg_ok = 0;
volatile uint8_t flg_stop_koncert = 0;
static volatile uint8_t prz_zablokowany[LICZBA_LED] = {0};

typedef struct { uint16_t freq; uint16_t ms; } nuta_t; //buzzer
/* przykład: początek motywu Mario (5. oktawa) */
static const nuta_t melodia_start[] = {
    {NUTA_E5,150},{NUTA_E5,150},{PAUZA,150},{NUTA_E5,150},
    {PAUZA,150},{NUTA_C5,150},{NUTA_E5,300},{NUTA_G5,300},
    {PAUZA,300},{NUTA_G5,300}
};
static const nuta_t *mel_ptr = 0;
static uint8_t  mel_len = 0, mel_idx = 0;
static uint32_t mel_deadline = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM2_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM16_Init(void);
/* USER CODE BEGIN PFP */
/* -------------------- PROTOTYPY FUNKCJI ----------------------------------- */
/* logika gry */
static void Gra_DodajKrok(void);
static void Gra_ZaladujUtwor(uint8_t idx_utworu);
static void Gra_ZgasWszystkieLED(void);
static void Gra_PokazKomunikat(const char *ln1, const char *ln2);
static void Gra_PokazPoziom(uint8_t poziom);
static void Gra_ObsluzStan(void);

/* narzędzia pomocnicze */
static uint32_t Pobierz_Ziarno_ADC(void);
static void     Dzwiek_Wlacz(uint8_t nr_led);
static void     Dzwiek_Wylacz(void);

/* rysowanie menu */
static void Menu_Rysuj_Tryb(void);
static void Menu_Rysuj_Akcja(void);
static void Menu_Rysuj_Utwor(void);
static void Menu_Rysuj_Poziom(void);
static void Menu_Rysuj_Predkosc(void);
static void Menu_Rysuj_Dzwiek(void);
static void Menu_Rysuj_Koniec(void);

/* sterownik LCD */
static void LCD_Zatrzask(uint8_t polbajt, uint8_t ster);
static void LCD_Kolejkuj(uint8_t bajt,   uint8_t ster);
static void LCD_Obsluz(void);

static void LCD_UstawKursor(uint8_t wiersz, uint8_t kol);
static void LCD_Wyczysc(void);
static void LCD_Wypisz(const char *txt);

static void Buzzer_Ton(uint16_t f);
static void Melodia_Start(const nuta_t *m, uint8_t n);
static void Melodia_Obsluz(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

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
  MX_TIM2_Init();
  MX_ADC1_Init();
  MX_TIM16_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start_IT(&htim2);   // start 1-ms tick
  HAL_TIM_PWM_Start(&htim16, TIM_CHANNEL_1);
  Buzzer_Ton(0);

  /* — inicjalizacja LCD sekwencją 4-bit — */
  lcd.krok  = LCD_WYSLIJ_POLBAJT;
  lcd.etap  = 0;

  LCD_Zatrzask(0x03, 0); HAL_Delay(5);
  LCD_Zatrzask(0x03, 0); HAL_Delay(5);
  LCD_Zatrzask(0x03, 0); HAL_Delay(1);
  LCD_Zatrzask(0x02, 0); HAL_Delay(1);

  LCD_Kolejkuj(0x28, 0);   // tryb 4-bit, 2 linie, znaki 5x8
  LCD_Kolejkuj(0x08, 0);   // wyłącz wyświetlacz
  LCD_Kolejkuj(0x01, 0);   // wyczyść ekran
  lcd.opoznienie_ms = 2;   // (czas oczekiwania po clear)
  LCD_Kolejkuj(0x06, 0);   // przesuwaj kursor w prawo, nie przesuwaj ekranu
  LCD_Kolejkuj(0x0C, 0);   // włącz wyświetlacz, kursor i miganie wyłączone


  /* ------------ start gry -------------------------------------------------- */
  uint32_t ziarno = Pobierz_Ziarno_ADC();       /* losowe ziarno z ADC */
  srand(ziarno ^ HAL_GetTick());

  Gra_PokazKomunikat("     Disco   ", "      w C   ");
  Melodia_Start(melodia_start, sizeof(melodia_start) / sizeof(nuta_t));
  deadline_splash = HAL_GetTick() + 2000U;      /* 2 s ekranu startowego */
  stan_gry        = ST_EKRAN_POWITALNY;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
      Melodia_Obsluz();
      Gra_ObsluzStan();          /* cała logika gry */

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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV1;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C1|RCC_PERIPHCLK_TIM16
                              |RCC_PERIPHCLK_ADC12|RCC_PERIPHCLK_TIM2;
  PeriphClkInit.Adc12ClockSelection = RCC_ADC12PLLCLK_DIV1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.Tim16ClockSelection = RCC_TIM16CLK_HCLK;
  PeriphClkInit.Tim2ClockSelection = RCC_TIM2CLK_HCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */
  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00201D2B;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7199;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 9;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  /* USER CODE BEGIN TIM16_Init 0 */

  /* USER CODE END TIM16_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM16_Init 1 */

  /* USER CODE END TIM16_Init 1 */
  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 71;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 999;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim16, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim16, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM16_Init 2 */

  /* USER CODE END TIM16_Init 2 */
  HAL_TIM_MspPostInit(&htim16);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PC4 PC5 PC6 PC7
                           PC8 PC9 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PB10 PB11 PB12 PB13
                           PB14 PB15 PB8 */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13
                          |GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* ================================================================== */
/*                    IMPLEMENTACJA  FUNKCJI                          */
/* ================================================================== */

/* ---------- 1.  NARZĘDZIA POMOCNICZE ------------------------------ */

/* losowe ziarno generowane z szumu ADC */
static uint32_t Pobierz_Ziarno_ADC(void)
{
    uint32_t seed = 0;
    for (uint8_t i = 0; i < 32; ++i) {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        seed = (seed << 1) | (HAL_ADC_GetValue(&hadc1) & 1U);
    }
    return seed;
}

/* krótki sygnał dźwiękowy – włącz i zaplanuj wyłączenie */
static void Dzwiek_Wlacz(uint8_t nr_led)
{
    if (!dzwiek_wlaczony) return;
    if (nr_led < LICZBA_LED) {
        Buzzer_Ton(LED_NUTY[nr_led]);
    }
    deadline_buzzer = HAL_GetTick() + 180U;
}

/* bezwarunkowe wyłączenie brzęczyka */
static void Dzwiek_Wylacz(void)
{
    Buzzer_Ton(0);
}

/* dodaje jeden (losowy) kolor do bufora sekwencji */
static void Gra_DodajKrok(void)
{
    if (dl_sekw < DL_MAX_SEKW)
        sekwencja[dl_sekw++] = rand() % LICZBA_LED;
}

/* ładuje wybrany utwór do bufora sekwencji */
static void Gra_ZaladujUtwor(uint8_t idx_utworu)
{
    if (idx_utworu >= LICZBA_UTWOROW) idx_utworu = 0;
    dl_docelowa = BAZA_UTWOROW[idx_utworu].dlugosc;
    if (dl_docelowa > DL_MAX_SEKW) dl_docelowa = DL_MAX_SEKW;
    memcpy(sekwencja, BAZA_UTWOROW[idx_utworu].nuty, dl_docelowa);
    dl_sekw = 1; /* zaczynamy naukę od pierwszej nuty */
}

/* gasi wszystkie 7 diod LED */
static void Gra_ZgasWszystkieLED(void)
{
    for (uint8_t i = 0; i < LICZBA_LED; ++i) {
        HAL_GPIO_WritePin(LED_PORTY[i], LED_PINY[i], GPIO_PIN_RESET);
        deadline_led_off[i] = 0;
    }
}

/* ---------- 2.  LCD  – NISKI  POZIOM ------------------------------ */

/* ====================================================================== */
/*                         S T E R O W N I K   L C D                      */
/* ====================================================================== */

/* wysyła pojedynczy półbajt + bajty sterujące (automat 4-bit) */
static void LCD_Zatrzask(uint8_t polbajt, uint8_t ster)
{
    uint8_t hi  = (polbajt & 0x0F) << 4;
    uint8_t val = hi | ster | LCD_BACKLIGHT;

    tx_buf[0] = val | LCD_EN;   /* zbocze E↑ */
    tx_buf[1] = val;            /* zbocze E↓ */

    HAL_I2C_Master_Transmit_IT(&hi2c1, LCD_I2C_ADDR,
                               (uint8_t*)tx_buf, sizeof(tx_buf));
}

/* dodaje cały bajt do kolejki; blokuje się do zakończenia poprzedniego */
static void LCD_Kolejkuj(uint8_t bajt, uint8_t ster)
{
    while (lcd.krok != LCD_BEZCZYN && lcd.krok != LCD_GOTOWE) { /* busy-wait */ }

    lcd.polbajt_hi = bajt >> 4;
    lcd.polbajt_lo = bajt & 0x0F;
    lcd.sterowanie = ster;
    lcd.etap       = 0;
    lcd.krok       = LCD_WYSLIJ_POLBAJT;
}

/* obsługa automatu – wywoływana co 1 ms z przerwania TIM2 */
static void LCD_Obsluz(void)
{
    switch (lcd.krok)
    {
        case LCD_WYSLIJ_POLBAJT:
            LCD_Zatrzask((lcd.etap == 0) ? lcd.polbajt_hi
                                         : lcd.polbajt_lo,
                         lcd.sterowanie);
            lcd.krok          = LCD_CZEKAJ_40US;
            lcd.opoznienie_ms = 1;          /* ≈ 40 µs zapasu */
            break;

        case LCD_CZEKAJ_40US:
            if (lcd.opoznienie_ms == 0)
            {
                if (lcd.etap == 0)
                {
                    lcd.etap = 1;
                    lcd.krok = LCD_WYSLIJ_POLBAJT;   /* drugi półbajt */
                }
                else
                {
                    lcd.krok = LCD_GOTOWE;           /* bajt gotowy   */
                }
            }
            break;

        case LCD_GOTOWE:
            lcd.krok = LCD_BEZCZYN;
            break;

        default:
            /* LCD_BEZCZYN – nic do roboty */
            break;
    }
}

/* -------- uproszczone API dla reszty gry -------------------------------- */

static void LCD_UstawKursor(uint8_t wiersz, uint8_t kol)
{
    LCD_Kolejkuj(0x80 | ((wiersz ? 0x40 : 0x00) + kol), 0);
}

static void LCD_Wyczysc(void)
{
    LCD_Kolejkuj(0x01, 0);      /* „clear display” */
    lcd.opoznienie_ms = 2;      /* 1,53 ms + zapas */
}

static void LCD_Wypisz(const char *txt)
{
    while (*txt) LCD_Kolejkuj((uint8_t)*txt++, LCD_RS);
}

/* -------- przerwanie I²C – nic nie robimy, automat steruje TIM2 -------- */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    (void)hi2c;
}

/* ---------- 3.  API  GRY  – WYŚWIETLACZ / MENU ------------------- */

static void Gra_PokazKomunikat(const char *ln1, const char *ln2)
{
    LCD_Wyczysc();
    LCD_UstawKursor(0,0); LCD_Wypisz(ln1);
    LCD_UstawKursor(1,0); LCD_Wypisz(ln2);
}

static void Gra_PokazPoziom(uint8_t poziom)
{
    char buf[17];
    if (tryb_gry == TRYB_MELODIE)
        snprintf(buf, sizeof(buf), "Nuta: %2u/%2u", poziom, dl_docelowa);
    else
        snprintf(buf, sizeof(buf), "Poziom: %2u", poziom);
    LCD_UstawKursor(1,0); LCD_Wypisz(buf);
}

/* ----------------------buzzer-------------------------------------*/

static void Buzzer_Ton(uint16_t f)
{
    if (f == 0) { __HAL_TIM_SET_COMPARE(&htim16, TIM_CHANNEL_1, 0); return; }
    uint32_t arr = 1000000UL / f - 1;
    __HAL_TIM_SET_AUTORELOAD(&htim16, arr);
    __HAL_TIM_SET_COMPARE(&htim16, TIM_CHANNEL_1, arr / 2);   /* wypełnienie 50% */
    if (__HAL_TIM_GET_COUNTER(&htim16) >= arr) {
        __HAL_TIM_SET_COUNTER(&htim16, 0);
    }
}

static void Melodia_Start(const nuta_t *m, uint8_t n)
{
    mel_ptr = m; mel_len = n; mel_idx = 0; mel_deadline = 0;
}

static void Melodia_Obsluz(void)
{
    if (!mel_ptr || HAL_GetTick() < mel_deadline) return;
    if (mel_idx >= mel_len) { Buzzer_Ton(0); mel_ptr = 0; return; }
    Buzzer_Ton(mel_ptr[mel_idx].freq);
    mel_deadline = HAL_GetTick() + mel_ptr[mel_idx].ms;
    mel_idx++;
}

/* ---------- rysowanie poszczególnych ekranów menu ---------------- */

static void Menu_Rysuj_Tryb(void)
{
    LCD_Wyczysc();
    LCD_UstawKursor(0,0); LCD_Wypisz("Wybierz gre:");
    LCD_UstawKursor(1,0);
    if (idx_menu_tryb == 0)
        LCD_Wypisz("  Memory Mix   >");
    else if (idx_menu_tryb == 1)
        LCD_Wypisz("<   Melodie    >");
    else if (idx_menu_tryb == 2)
        LCD_Wypisz("<  Odtwarzaj   >");
    else
        LCD_Wypisz("<  Freestyle    ");
}

static void Menu_Rysuj_Akcja(void)
{
    LCD_Wyczysc();
    LCD_UstawKursor(0,0);
    if (tryb_gry == TRYB_MEMORY_MIX)
        LCD_Wypisz("Tryb:Memory Mix");
    else if (tryb_gry == TRYB_MELODIE)
        LCD_Wypisz("Tryb: Melodie");
    else if (tryb_gry == TRYB_ODTWARZAJ)
        LCD_Wypisz("Tryb: Odtwarzaj");
    else
        LCD_Wypisz("Tryb: Freestyle");

    LCD_UstawKursor(1,0);
    if (idx_menu_akcja == 0)
        LCD_Wypisz("     Graj!     >");
    else if (idx_menu_akcja == 1)
        LCD_Wypisz("<  Ustawienia  >");
    else
        LCD_Wypisz("<  Zmien tryb   ");
}

static void Menu_Rysuj_Utwor(void)
{
    LCD_Wyczysc();
    LCD_UstawKursor(0,0); LCD_Wypisz("Wybierz utwor:");
    LCD_UstawKursor(1,0);
    LCD_Wypisz(BAZA_UTWOROW[idx_menu_utwor].nazwa_menu);
}

static void Menu_Rysuj_Poziom(void)
{
    LCD_Wyczysc();
    LCD_UstawKursor(0,0); LCD_Wypisz("Poziom trudnosci");
    LCD_UstawKursor(1,0);
    if (idx_menu_poziom == 0)
        LCD_Wypisz("     Latwy     >");
    else
        LCD_Wypisz("<    Trudny     ");
}

static void Menu_Rysuj_Predkosc(void)
{
    LCD_Wyczysc();
    LCD_UstawKursor(0,0); LCD_Wypisz("Predkosc:");
    LCD_UstawKursor(1,0);
    if (idx_menu_pred == 0)
        LCD_Wypisz("     Wolna     >");
    else if (idx_menu_pred == 1)
        LCD_Wypisz("<   Srednia    >");
    else
        LCD_Wypisz("<    Szybka     ");
}

static void Menu_Rysuj_Dzwiek(void)
{
    LCD_Wyczysc();
    LCD_UstawKursor(0,0); LCD_Wypisz("Dzwiek:");
    LCD_UstawKursor(1,0);
    if (idx_menu_dzwiek == 0)
        LCD_Wypisz("    Wlaczony   >");
    else
        LCD_Wypisz("<   Wylaczony    ");
}

static void Menu_Rysuj_Koniec(void)
{
    char buf[17];
    LCD_Wyczysc();
    snprintf(buf, sizeof(buf), "Twoj wynik: %2u", (dl_sekw-1));
    LCD_UstawKursor(0,0); LCD_Wypisz(buf);

    LCD_UstawKursor(1,0);
    if (idx_menu_koniec == 0)
        LCD_Wypisz("   Gram dalej  >");
    else
        LCD_Wypisz("<     Menu      ");
}

/* ---------- 4.  SILNIK  GRY  – MASZYNA  STANÓW ------------------- */

static void Gra_ObsluzStan(void)
{
    switch (stan_gry)
    {
        /* -------- ekran powitalny -------- */
        case ST_EKRAN_POWITALNY:
            if (HAL_GetTick() >= deadline_splash) {
                Gra_PokazKomunikat("Zasady:Powtarzaj", "    po mnie!    ");
                deadline_splash = HAL_GetTick() + 2000U;
                stan_gry = ST_ZASADY;
            }
            break;

        /* -------- ekran zasad gry -------- */
        case ST_ZASADY:
            if (HAL_GetTick() >= deadline_splash) {
                idx_menu_tryb = 0;
                Menu_Rysuj_Tryb();
                stan_gry = ST_MENU_TRYB_CZEKAJ;
            }
            break;

        /* -------- MENU – wybór trybu gry (4 tryby) -------- */
        case ST_MENU_TRYB_CZEKAJ:
            if (flg_up && idx_menu_tryb > 0)                   { --idx_menu_tryb; Menu_Rysuj_Tryb(); flg_up = 0; }
            if (flg_dn && idx_menu_tryb < (LICZBA_TRYBOW - 1)) { ++idx_menu_tryb; Menu_Rysuj_Tryb(); flg_dn = 0; }
            if (flg_ok) {
                flg_up = flg_dn = flg_ok = 0;
                tryb_gry = (tryb_gry_t)idx_menu_tryb;
                idx_menu_akcja = 0;
                Menu_Rysuj_Akcja();
                deadline_pauza = HAL_GetTick() + 250U;
                stan_gry = ST_MENU_AKCJA_CZEKAJ;
            }
            break;

        /* -------- MENU – Graj! / Ustawienia / Zmień tryb -------- */
        case ST_MENU_AKCJA_CZEKAJ:
            if (flg_up && idx_menu_akcja > 0) { --idx_menu_akcja; Menu_Rysuj_Akcja(); flg_up = 0; }
            if (flg_dn && idx_menu_akcja < 2) { ++idx_menu_akcja; Menu_Rysuj_Akcja(); flg_dn = 0; }
            if (flg_ok) {
                flg_up = flg_dn = flg_ok = 0;
                deadline_pauza = HAL_GetTick() + 250U;
                if (idx_menu_akcja == 0) {
                    /* GRAJ! */
                    if (tryb_gry == TRYB_MEMORY_MIX) {
                        Gra_PokazKomunikat("Gotow?", "Wcisnij przycisk");
                        stan_gry = ST_BEZCZYNNY;
                    } else if (tryb_gry == TRYB_MELODIE || tryb_gry == TRYB_ODTWARZAJ) {
                        Menu_Rysuj_Utwor();
                        stan_gry = ST_MENU_UTWOR_CZEKAJ;
                    } else {
                        /* TRYB_FREESTYLE – swobodna gra na 7 klawiszach */
                        Gra_ZgasWszystkieLED();
                        Dzwiek_Wylacz();
                        Gra_PokazKomunikat("  Freestyle!    ", "Przytrzymaj=Menu");
                        czas_trzymania_wyjscia = 0;
                        aktywna_nuta_freestyle = -1;
                        deadline_pauza = HAL_GetTick() + 300U; /* odczekaj puszczenie Entera */
                        stan_gry = ST_WOLNA_GRA;
                    }
                } else if (idx_menu_akcja == 1) {
                    /* USTAWIENIA */
                    if (tryb_gry == TRYB_MEMORY_MIX) {
                        Menu_Rysuj_Poziom();
                        stan_gry = ST_MENU_POZIOM_CZEKAJ;
                    } else if (tryb_gry == TRYB_FREESTYLE) {
                        idx_menu_dzwiek = dzwiek_wlaczony ? 0 : 1;
                        Menu_Rysuj_Dzwiek();
                        stan_gry = ST_MENU_DZW_CZEKAJ;
                    } else {
                        Menu_Rysuj_Predkosc();
                        stan_gry = ST_MENU_PRED_CZEKAJ;
                    }
                } else {
                    /* ZMIEŃ TRYB */
                    Menu_Rysuj_Tryb();
                    stan_gry = ST_MENU_TRYB_CZEKAJ;
                }
            }
            break;

        /* -------- MENU – wybór utworu (dla Melodie oraz Odtwarzaj) -------- */
        case ST_MENU_UTWOR_CZEKAJ:
            if (flg_up && idx_menu_utwor > 0)                  { --idx_menu_utwor; Menu_Rysuj_Utwor(); flg_up = 0; }
            if (flg_dn && idx_menu_utwor < (LICZBA_UTWOROW-1)) { ++idx_menu_utwor; Menu_Rysuj_Utwor(); flg_dn = 0; }
            if (flg_ok) {
                flg_up = flg_dn = flg_ok = 0;
                if (tryb_gry == TRYB_ODTWARZAJ) {
                    /* Od razu odtwarzaj wybraną melodię w pętli z diodami */
                    Gra_ZaladujUtwor(idx_menu_utwor);
                    Gra_PokazKomunikat("Odtwarzanie...  ", "Kliknij = stop  ");
                    idx_odtw = faza_odtw = 0;
                    flg_stop_koncert = 0;
                    deadline_pauza = HAL_GetTick() + 400U; /* blokada przypadkowego stopu przy puszczeniu Entera */
                    deadline_odtw  = HAL_GetTick() + 400U;
                    stan_gry = ST_KONCERT;
                } else {
                    Gra_PokazKomunikat("Gotow?", "Wcisnij przycisk");
                    deadline_pauza = HAL_GetTick() + 250U;
                    stan_gry = ST_BEZCZYNNY;
                }
            }
            break;

        /* -------- MENU – poziom trudności -------- */
        case ST_MENU_POZIOM_CZEKAJ:
            if (flg_up)  { idx_menu_poziom = 0; Menu_Rysuj_Poziom(); flg_up  = 0; }
            if (flg_dn)  { idx_menu_poziom = 1; Menu_Rysuj_Poziom(); flg_dn  = 0; }
            if (flg_ok)  {
                flg_up = flg_dn = flg_ok = 0;
                trudnosc = idx_menu_poziom ? TR_TRUDNY : TR_LATWY;
                Menu_Rysuj_Predkosc();
                deadline_pauza = HAL_GetTick() + 250U;
                stan_gry = ST_MENU_PRED_CZEKAJ;
            }
            break;

        /* -------- MENU – prędkość -------- */
        case ST_MENU_PRED_CZEKAJ:
            if (flg_up  && idx_menu_pred)      { --idx_menu_pred; Menu_Rysuj_Predkosc(); flg_up = 0; }
            if (flg_dn  && idx_menu_pred < 2 ) { ++idx_menu_pred; Menu_Rysuj_Predkosc(); flg_dn = 0; }
            if (flg_ok) {
                flg_up = flg_dn = flg_ok = 0;
                predkosc_odtw  = (predkosc_t)idx_menu_pred;
                idx_menu_dzwiek= dzwiek_wlaczony ? 0 : 1;
                Menu_Rysuj_Dzwiek();
                deadline_pauza = HAL_GetTick() + 250U;
                stan_gry = ST_MENU_DZW_CZEKAJ;
            }
            break;

        /* -------- MENU – dźwięk (po zatwierdzeniu wraca do Graj!/Ustawienia) -------- */
        case ST_MENU_DZW_CZEKAJ:
            if (flg_up) { idx_menu_dzwiek = 0; Menu_Rysuj_Dzwiek(); flg_up = 0; }
            if (flg_dn) { idx_menu_dzwiek = 1; Menu_Rysuj_Dzwiek(); flg_dn = 0; }
            if (flg_ok) {
                flg_up = flg_dn = flg_ok = 0;
                dzwiek_wlaczony = (idx_menu_dzwiek == 0);
                idx_menu_akcja = 0; /* ustaw domyślnie kursor na "Graj!" */
                Menu_Rysuj_Akcja();
                deadline_pauza = HAL_GetTick() + 250U;
                stan_gry = ST_MENU_AKCJA_CZEKAJ;
            }
            break;

        /* -------- tryb bezczynny – oczekiwanie startu -------- */
        case ST_BEZCZYNNY:
            if (HAL_GetTick() < deadline_pauza) break; /* odczekaj puszczenie Entera */
            if (PRZYCISK_START) {
                if (tryb_gry == TRYB_MELODIE) {
                    Gra_ZaladujUtwor(idx_menu_utwor);
                } else {
                    dl_sekw = 0;
                    Gra_DodajKrok();
                }
                deadline_pauza = HAL_GetTick() + 300U;
                stan_gry = ST_START_PAUZA;
            }
            break;

        /* -------- pauza po starcie -------- */
        case ST_START_PAUZA:
            if (HAL_GetTick() >= deadline_pauza) {
                Gra_PokazKomunikat("Zapamietaj:", "");
                Gra_PokazPoziom(dl_sekw);
                idx_odtw = faza_odtw = 0;
                deadline_odtw = HAL_GetTick() + 300U;
                stan_gry = ST_ODTWARZAJ;
            }
            break;

        /* -------- odtwarzanie sekwencji -------- */
        case ST_ODTWARZAJ:
            if (HAL_GetTick() < deadline_odtw) break;

            if (idx_odtw >= dl_sekw) {        /* koniec sekwencji */
                poz_wejscia = 0;
                stan_gry = ST_OCZEKUJ;        /* od razu odblokuj przyciski! */
                Gra_PokazKomunikat("Twoja kolej!", "");
                Gra_PokazPoziom(dl_sekw);
                break;
            }

            uint8_t led = sekwencja[idx_odtw];

            if (faza_odtw == 0) {             /* faza ON */
                HAL_GPIO_WritePin(LED_PORTY[led], LED_PINY[led], GPIO_PIN_SET);
                deadline_buzzer = 0;          /* graj przez cały czas świecenia diody */
                if (dzwiek_wlaczony) Buzzer_Ton(LED_NUTY[led]);
                faza_odtw   = 1;
                deadline_odtw = HAL_GetTick() + czas_on[predkosc_odtw];
            } else {                          /* faza OFF */
                HAL_GPIO_WritePin(LED_PORTY[led], LED_PINY[led], GPIO_PIN_RESET);
                Dzwiek_Wylacz();
                faza_odtw   = 0; ++idx_odtw;
                if (idx_odtw >= dl_sekw) {
                    deadline_odtw = HAL_GetTick(); /* po ostatniej nucie od razu kolej gracza */
                } else {
                    deadline_odtw = HAL_GetTick() + czas_off[predkosc_odtw];
                }
            }
            break;

        /* -------- sukces -------- */
        case ST_SUKCES:
            poz_wejscia = 0;
            if (tryb_gry == TRYB_MELODIE && dl_sekw >= dl_docelowa) {
                /* Cała melodia zagrana! Przechodzimy w zapętlony koncert! */
                Gra_PokazKomunikat("Brawo! Koncert!", "Kliknij = stop ");
                idx_odtw = faza_odtw = 0;
                flg_stop_koncert = 0;
                deadline_pauza = HAL_GetTick() + 500U; /* ochrona przed puszczeniem ostatniej nuty */
                deadline_odtw  = HAL_GetTick() + 500U;
                stan_gry = ST_KONCERT;
            } else {
                Gra_PokazKomunikat("Brawo!", "");
                Gra_PokazPoziom(dl_sekw);
                deadline_pauza = HAL_GetTick() + 1200U;
                stan_gry = ST_PAUZA_SUKCES;
            }
            break;

        case ST_PAUZA_SUKCES:
            if (HAL_GetTick() >= deadline_pauza) {
                if (tryb_gry == TRYB_MELODIE) {
                    if (dl_sekw < dl_docelowa) ++dl_sekw;
                } else {
                    if (trudnosc == TR_LATWY) {
                        Gra_DodajKrok();
                    } else {
                        ++dl_sekw;
                        for (uint8_t i = 0; i < dl_sekw; ++i)
                            sekwencja[i] = rand() % LICZBA_LED;
                    }
                }
                stan_gry = ST_POKAZ;
            }
            break;

        /* -------- prezentacja nowej sekwencji -------- */
        case ST_POKAZ:
            Gra_PokazKomunikat("Zapamietaj:", "");
            Gra_PokazPoziom(dl_sekw);
            idx_odtw = faza_odtw = 0;
            deadline_odtw = HAL_GetTick();
            stan_gry = ST_ODTWARZAJ;
            break;

        /* -------- KONCERT / ODTWARZACZ: zapętlone odgrywanie melodii + LED -------- */
        case ST_KONCERT:
            if (flg_stop_koncert || (HAL_GetTick() >= deadline_pauza && PRZYCISK_START)) {
                flg_stop_koncert = 0;
                flg_up = flg_dn = flg_ok = 0;
                Gra_ZgasWszystkieLED();
                Dzwiek_Wylacz();
                deadline_pauza = HAL_GetTick() + 300U;
                idx_menu_akcja = 0;
                Menu_Rysuj_Akcja();
                stan_gry = ST_MENU_AKCJA_CZEKAJ;
                break;
            }

            if (HAL_GetTick() < deadline_odtw) break;

            if (idx_odtw >= dl_docelowa) {
                idx_odtw = 0;
                deadline_odtw = HAL_GetTick() + 500U; /* krótka przerwa przed powtórzeniem pętli */
                break;
            }

            {
                uint8_t nuta_led = sekwencja[idx_odtw];
                if (faza_odtw == 0) {
                    HAL_GPIO_WritePin(LED_PORTY[nuta_led], LED_PINY[nuta_led], GPIO_PIN_SET);
                    deadline_buzzer = 0;
                    if (dzwiek_wlaczony) Buzzer_Ton(LED_NUTY[nuta_led]);
                    faza_odtw = 1;
                    deadline_odtw = HAL_GetTick() + czas_on[predkosc_odtw];
                } else {
                    HAL_GPIO_WritePin(LED_PORTY[nuta_led], LED_PINY[nuta_led], GPIO_PIN_RESET);
                    Buzzer_Ton(0);
                    faza_odtw = 0;
                    ++idx_odtw;
                    deadline_odtw = HAL_GetTick() + czas_off[predkosc_odtw];
                }
            }
            break;

        /* -------- FREESTYLE: swobodne granie na 7 przyciskach jak na pianinie -------- */
        case ST_WOLNA_GRA:
        {
            if (HAL_GetTick() < deadline_pauza) break;

            int8_t wcisniety = -1;
            uint8_t ile_wcisnietych = 0;
            for (uint8_t i = 0; i < LICZBA_LED; ++i) {
                if (HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[i]) == GPIO_PIN_RESET) {
                    HAL_GPIO_WritePin(LED_PORTY[i], LED_PINY[i], GPIO_PIN_SET);
                    if (wcisniety < 0) wcisniety = (int8_t)i;
                    ++ile_wcisnietych;
                } else {
                    HAL_GPIO_WritePin(LED_PORTY[i], LED_PINY[i], GPIO_PIN_RESET);
                }
            }

            /* Wyjście do menu: przytrzymanie przycisku przez 1,5 s LUB 2 przyciski naraz */
            if (wcisniety >= 0) {
                if (czas_trzymania_wyjscia == 0) {
                    czas_trzymania_wyjscia = HAL_GetTick();
                } else if ((HAL_GetTick() - czas_trzymania_wyjscia >= 1500U) || (ile_wcisnietych >= 2)) {
                    Gra_ZgasWszystkieLED();
                    Dzwiek_Wylacz();
                    aktywna_nuta_freestyle = -1;
                    flg_up = flg_dn = flg_ok = 0;
                    deadline_pauza = HAL_GetTick() + 400U;
                    idx_menu_akcja = 0;
                    Menu_Rysuj_Akcja();
                    stan_gry = ST_MENU_AKCJA_CZEKAJ;
                    break;
                }
            } else {
                czas_trzymania_wyjscia = 0;
            }

            /* Sterowanie dźwiękiem na żywo dopóki trzymasz przycisk */
            if (wcisniety >= 0) {
                if (aktywna_nuta_freestyle != wcisniety) {
                    aktywna_nuta_freestyle = wcisniety;
                    if (dzwiek_wlaczony) Buzzer_Ton(LED_NUTY[wcisniety]);
                }
            } else {
                if (aktywna_nuta_freestyle != -1) {
                    aktywna_nuta_freestyle = -1;
                    Buzzer_Ton(0);
                }
            }
            break;
        }

        /* -------- błąd -------- */
        case ST_PORAZKA:
            Gra_PokazKomunikat("Blad!", "");
            Gra_PokazPoziom(dl_sekw);
            deadline_pauza = HAL_GetTick() + 1200U;
            stan_gry = ST_PAUZA_PORAZKA;
            break;

        case ST_PAUZA_PORAZKA:
            if (HAL_GetTick() >= deadline_pauza) {
                idx_menu_koniec = 0;
                Menu_Rysuj_Koniec();
                stan_gry = ST_KONIEC_CZEKAJ;
            }
            break;

        /* -------- MENU końcowe -------- */
        case ST_KONIEC_CZEKAJ:
            if (flg_up) { idx_menu_koniec = 0; Menu_Rysuj_Koniec(); flg_up = 0; }
            if (flg_dn) { idx_menu_koniec = 1; Menu_Rysuj_Koniec(); flg_dn = 0; }
            if (flg_ok) {
                flg_up = flg_dn = flg_ok = 0;
                if (idx_menu_koniec == 0) {
                    if (tryb_gry == TRYB_MELODIE) {
                        Gra_ZaladujUtwor(idx_menu_utwor);
                    } else {
                        dl_sekw = 0; Gra_DodajKrok();
                    }
                    deadline_pauza = HAL_GetTick() + 300U;
                    stan_gry = ST_START_PAUZA;
                } else {
                    idx_menu_akcja = 0;
                    Menu_Rysuj_Akcja();
                    deadline_pauza = HAL_GetTick() + 250U;
                    stan_gry = ST_MENU_AKCJA_CZEKAJ;
                }
            }
            break;

        /* -------- czekamy na wejście gracza -------- */
        case ST_OCZEKUJ:
        default:
            break;
    }
}

/* ---------- 5.  PRZERWANIA  /  CALLBACKI ------------------------- */

/* tick 1 ms – timer TIM2 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        if (lcd.opoznienie_ms) --lcd.opoznienie_ms;
        LCD_Obsluz();

        uint32_t teraz = HAL_GetTick();
        for (uint8_t i = 0; i < LICZBA_LED; ++i) {
            if (deadline_led_off[i] && teraz >= deadline_led_off[i]) {
                HAL_GPIO_WritePin(LED_PORTY[i], LED_PINY[i], GPIO_PIN_RESET);
                deadline_led_off[i] = 0;
            }
            /* Blokada drgań przy puszczaniu: odliczaj dopiero po puszczeniu przycisku */
            if (HAL_GPIO_ReadPin(GPIOB, PRZ_PINY[i]) == GPIO_PIN_RESET) {
                if (prz_zablokowany[i] > 0) {
                    prz_zablokowany[i] = (stan_gry == ST_OCZEKUJ) ? 45U : 80U;
                }
            } else if (prz_zablokowany[i] > 0) {
                --prz_zablokowany[i];
            }
        }
    }
    if (deadline_buzzer && HAL_GetTick() >= deadline_buzzer) {
        Dzwiek_Wylacz();
        deadline_buzzer = 0;
    }
}

/* obsługa przycisków – EXTI 15-10 oraz EXTI 9-5 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* W trybie swobodnej gry (Freestyle) przyciski są czytane bezpośrednio w pętli */
    if (stan_gry == ST_WOLNA_GRA)
        return;

    /* Ignoruj drgania styków przy puszczaniu przycisku (gdy pin wrócił na stan wysoki) */
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_Pin) != GPIO_PIN_RESET)
        return;

    /* debounce dla wszystkich 7 przycisków */
    static uint32_t t_ost[LICZBA_LED] = {0};
    uint32_t teraz = HAL_GetTick();
    uint32_t odst  = (stan_gry == ST_OCZEKUJ) ? 120U : 220U;

    int8_t wcisniety = -1;
    for (uint8_t i = 0; i < LICZBA_LED; ++i) {
        if (GPIO_Pin == PRZ_PINY[i]) {
            if (prz_zablokowany[i] > 0) return;
            if ((teraz - t_ost[i]) < odst) return;
            wcisniety = i;
            break;
        }
    }
    if (wcisniety < 0) return;

    /* zatrzymanie zapętlonego koncertu dowolnym przyciskiem */
    if (stan_gry == ST_KONCERT) {
        if (teraz < deadline_pauza) return; /* ignoruj puszczenie przycisku z menu */
        t_ost[wcisniety] = teraz;
        prz_zablokowany[wcisniety] = 80U;
        flg_stop_koncert = 1;
        return;
    }

    /* przyciski w menu */
    if (stan_gry == ST_MENU_TRYB_CZEKAJ   ||
        stan_gry == ST_MENU_AKCJA_CZEKAJ  ||
        stan_gry == ST_MENU_UTWOR_CZEKAJ  ||
        stan_gry == ST_MENU_POZIOM_CZEKAJ ||
        stan_gry == ST_MENU_PRED_CZEKAJ   ||
        stan_gry == ST_MENU_DZW_CZEKAJ    ||
        stan_gry == ST_KONIEC_CZEKAJ) {
        if (teraz < deadline_pauza) return; /* blokada przeskakiwania dwóch ekranów menu */
        t_ost[wcisniety] = teraz;
        prz_zablokowany[wcisniety] = 80U;
        if (GPIO_Pin == GPIO_PIN_13) flg_up = 1; // 1. przycisk = Lewo (<)
        if (GPIO_Pin == GPIO_PIN_14) flg_dn = 1; // 2. przycisk = Prawo (>)
        if (GPIO_Pin == GPIO_PIN_15) flg_ok = 1; // 3. przycisk = Enter (OK)
        return;
    }

    /* --- jeżeli nie jesteśmy w fazie OCZEKUJ, ignoruj przycisk --- */
    if (stan_gry != ST_OCZEKUJ)
        return;

    /* zapisz czas debounce dopiero dla faktycznego kliknięcia w grze */
    t_ost[wcisniety] = teraz;
    prz_zablokowany[wcisniety] = 45U;

    /* przyciski w trakcie gry (0..6) */
    uint8_t i = (uint8_t)wcisniety;
    HAL_GPIO_WritePin(LED_PORTY[i], LED_PINY[i], GPIO_PIN_SET);
    deadline_led_off[i] = teraz + 120U;
    Dzwiek_Wlacz(i);

    if (i == sekwencja[poz_wejscia]) {
        if (++poz_wejscia >= dl_sekw) stan_gry = ST_SUKCES;
    } else {
        stan_gry = ST_PORAZKA;
    }
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
