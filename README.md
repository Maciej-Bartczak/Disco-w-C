# Disco w C – Elektroniczne pianino i gra pamięciowa (STM32)

Projekt gry muzyczno-pamięciowej oraz elektronicznego pianina na mikrokontrolerze **STM32 Nucleo-F303RE** (ARM Cortex-M4), wyposażonego w **7 diod LED**, **7 przycisków**, **pasywny buzzer PWM** oraz **wyświetlacz LCD 2×16 (I²C)**.

Projekt został przygotowany i zaprezentowany m.in. na **Nocy Politechniki Wrocławskiej**.

---

## Tryby działania (4 tryby w menu)

1. **Memory Mix** – klasyczna gra pamięciowa z losowymi sekwencjami:
   - *Poziom Łatwy:* sekwencja zostaje ta sama, w każdej rundzie dochodzi 1 nowy krok na końcu.
   - *Poziom Trudny:* w każdej rundzie cała sekwencja jest losowana od nowa + 1 nowy krok.
2. **Melodie** – nauka wybranej znanej piosenki nuta po nucie (`1..N`), zakończona zapętlonym koncertem w nagrodę.
3. **Odtwarzaj** – szafa grająca odtwarzająca wybraną melodię w pętli z synchronizacją 7 diod LED (zatrzymanie dowolnym przyciskiem).
4. **Freestyle** – swobodne granie na 7 przyciskach jak na pianinie (wyjście do menu przez przytrzymanie przycisku przez ok. `1,5 s` lub wciśnięcie 2 przycisków naraz).

### 10 wgranych melodii
1. Oda do radości
2. Wlazł kotek na płotek
3. Panie Janie
4. Sto lat
5. Super Mario Bros
6. Jingle Bells
7. Gwiazdeczka (*Twinkle Twinkle Little Star*)
8. Star Wars – Marsz Imperialny
9. Tetris (*Korobeiniki*)
10. Bella Ciao

---

## Mapa pinów (STM32 Nucleo-F303RE)

### Przyciski i diody LED (od lewej `1 → 7`)
| Nr | Funkcja w menu | Pin przycisku (`EXTI`, `Pull-Up`) | Pin diody LED | Nuta (`OKTAWA_BUZZERA 45`) |
| :-: | :--- | :-: | :-: | :-: |
| 1 | Lewo (`◀`) | `PB13` | `PC6` | G4 (`392 Hz`) |
| 2 | Prawo (`▶`) | `PB14` | `PC7` | A4 (`440 Hz`) |
| 3 | Enter (`✓`) | `PB15` | `PC8` | H4 (`494 Hz`) |
| 4 | — | `PB12` | `PC9` | C5 (`523 Hz`) |
| 5 | — | `PB11` | `PC5` | D5 (`587 Hz`) |
| 6 | — | `PB10` | `PC4` | E5 (`659 Hz`) |
| 7 | — | `PB8` | `PB9` | F#5 (`740 Hz`) |

### Wyświetlacz LCD i buzzer
- **LCD 2×16 (HD44780 + konwerter I²C `PCF8574`):** `SDA → PB7`, `SCL → PB6`, `VCC → 5V`, `GND → GND`
- **Moduł buzzera pasywnego (`TIM16_CH1` PWM):** `I/O → PA6` (`D12`), `VCC → 3.3V`, `GND → GND`
- **Generator ziarna losowości (`ADC1_IN1`):** `PA0`

---

## Dokumentacja (do druku / edycji w Word i Google Docs)

- [Instrukcja obsługi i opis budowy (Noc PWr)](Dokumentacja/Instrukcja_obslugi_Disco_w_C_Noc_PWr.docx)
- [Protokół szybkiej naprawy dla prowadzących](Dokumentacja/Protokol_szybkiej_naprawy_Disco_w_C_Noc_PWr.docx)
