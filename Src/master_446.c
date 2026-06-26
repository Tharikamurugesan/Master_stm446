#include "main.h"
#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart1; // Communication with BMS Slave
UART_HandleTypeDef huart6; // Output to PC PuTTY Terminal

char putty_buffer[500];
uint8_t master_tx_frame[13];
uint8_t master_rx_frame[13];

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART6_UART_Init(void);

// Helper function to send logs directly to PuTTY
void LogToPuTTY(char *msg) {
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), 200);
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART6_UART_Init();

  while (1)
  {
    /* --- STEP 1: CONSTRUCT DALY PROTOCOL REQUEST (0x90) --- */
    master_tx_frame[0] = 0xA5; // Start Flag
    master_tx_frame[1] = 0x40; // Host Address
    master_tx_frame[2] = 0x90; // Data ID (SOC, Voltage, Current)
    master_tx_frame[3] = 0x08; // Fixed Data Length
    // Bytes 4 to 11 are reserved / 0x00
    for(int i = 4; i <= 11; i++) {
        master_tx_frame[i] = 0x00;
    }

    // Calculate Local Tx Checksum
    uint8_t tx_checksum = 0;
    for(int i = 0; i < 12; i++) {
        tx_checksum += master_tx_frame[i];
    }
    master_tx_frame[12] = tx_checksum;

    /* --- PUTTY OUTPUT 1: SHOW WHAT REQUEST THE MCU SENDS --- */
    snprintf(putty_buffer, sizeof(putty_buffer),
             "\r\n[1] MCU Outgoing Request: ");
    LogToPuTTY(putty_buffer);
    for(int i = 0; i < 13; i++) {
        snprintf(putty_buffer, sizeof(putty_buffer), "0x%02X ", master_tx_frame[i]);
        LogToPuTTY(putty_buffer);
    }
    LogToPuTTY("\r\n");

    // Transmit request frame to BMS Slave
    HAL_UART_Transmit(&huart1, master_tx_frame, 13, 100);

    /* --- STEP 2: AWAIT 13-BYTE RESPONSE FROM BMS SLAVE --- */
    if (HAL_UART_Receive(&huart1, master_rx_frame, 13, 500) == HAL_OK)
    {
        /* --- PUTTY OUTPUT 2: SLAVE MASTER VERIFICATION NOTE --- */
        LogToPuTTY("[2] Master Checksum Verification Status: Processing incoming payload...\r\n");

        /* --- PUTTY OUTPUT 3: SHOW RAW DATA BYTES OF BMS --- */
        snprintf(putty_buffer, sizeof(putty_buffer), "[3] Raw BMS Frame Received: ");
        LogToPuTTY(putty_buffer);
        for(int i = 0; i < 13; i++) {
            snprintf(putty_buffer, sizeof(putty_buffer), "0x%02X ", master_rx_frame[i]);
            LogToPuTTY(putty_buffer);
        }
        LogToPuTTY("\r\n");

        /* --- STEP 3: VERIFY SLAVE-TO-MASTER CHECKSUM --- */
        uint8_t calc_rx_checksum = 0;
        for(int i = 0; i < 12; i++) {
            calc_rx_checksum += master_rx_frame[i];
        }
        uint8_t received_checksum = master_rx_frame[12];

        /* --- PUTTY OUTPUT 4: SHOW CHECKSUM VERIFICATION FROM SLAVE TO MASTER --- */
        if (calc_rx_checksum == received_checksum)
        {
            snprintf(putty_buffer, sizeof(putty_buffer),
                     "[4] Checksum Verification: PASSED (Calculated: 0x%02X == Received: 0x%02X)\r\n",
                     calc_rx_checksum, received_checksum);
            LogToPuTTY(putty_buffer);

            /* --- STEP 4: PARSE AND DECODE METRICS (FIXED INTEGRAL BACKUP) --- */
            // Data bytes are positioned from index 4 to 11 inside a 13-byte frame
            uint16_t raw_cum_volt = (master_rx_frame[4] << 8) | master_rx_frame[5];
            uint16_t raw_gat_volt = (master_rx_frame[6] << 8) | master_rx_frame[7];
            uint16_t raw_current  = (master_rx_frame[8] << 8) | master_rx_frame[9];
            uint16_t raw_soc      = (master_rx_frame[10] << 8) | master_rx_frame[11];

            // Extract Whole and Fractional parts for Cumulative Voltage (0.1V resolution)
            int cum_volt_whole = raw_cum_volt / 10;
            int cum_volt_frac  = raw_cum_volt % 10;

            // Extract Whole and Fractional parts for Gather Voltage (0.1V resolution)
            int gat_volt_whole = raw_gat_volt / 10;
            int gat_volt_frac  = raw_gat_volt % 10;

            // Handle Signed Current calculations (30000 offset, 0.1A resolution)
            int32_t true_current_scaled = (int32_t)raw_current - 30000;
            int current_whole = true_current_scaled / 10;
            int current_frac  = true_current_scaled % 10;

            // Keep the fractional element positive for aesthetic printing
            if (current_frac < 0) {
                current_frac = -current_frac;
            }

            // Extract Whole and Fractional parts for SoC (0.1% resolution)
            int soc_whole = raw_soc / 10;
            int soc_frac  = raw_soc % 10;

            /* --- PUTTY OUTPUT 5: SHOW CALCULATED ORIGINAL VALUES --- */
            snprintf(putty_buffer, sizeof(putty_buffer),
                     "[5] ======= CALCULATED ORIGINAL VALUES =======\r\n"
                     "    Cumulative Total Volt: %d.%d V\r\n"
                     "    Gather Total Volt:     %d.%d V\r\n"
                     "    Current:               %d.%d A\r\n"
                     "    State of Charge (SoC): %d.%d %%\r\n"
                     "    ==========================================\r\n",
                     cum_volt_whole, cum_volt_frac,
                     gat_volt_whole, gat_volt_frac,
                     current_whole,  current_frac,
                     soc_whole,       soc_frac);
            LogToPuTTY(putty_buffer);
        }
        else
        {
            snprintf(putty_buffer, sizeof(putty_buffer),
                     "[4] Checksum Verification: FAILED! (Calc: 0x%02X != Recv: 0x%02X)\r\n",
                     calc_rx_checksum, received_checksum);
            LogToPuTTY(putty_buffer);
        }
    }
    else
    {
        LogToPuTTY("[BMS Error]: Response timeout. No reply from Slave BMS board.\r\n");
    }

    HAL_Delay(2000); // Poll every 2 seconds
  }
}

static void MX_USART1_UART_Init(void) {
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  HAL_UART_Init(&huart1);
}

static void MX_USART6_UART_Init(void) {
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 115200;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  HAL_UART_Init(&huart6);
}

static void MX_GPIO_Init(void) { __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE(); }
void SystemClock_Config(void) { SystemCoreClockUpdate(); }
