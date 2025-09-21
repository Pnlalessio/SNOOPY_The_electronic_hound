#define BLYNK_TEMPLATE_ID "TMPL4pdRtiE9E"
#define BLYNK_TEMPLATE_NAME "Electronic Nose 2"
#define BLYNK_FIRMWARE_VERSION "0.1.0"
#define BLYNK_PRINT Serial
#define APP_DEBUG


#include <Wire.h> // This library enables communication with I2C/TWI devices
#include "Multichannel_Gas_GMXXX.h" // Multichannel_Gas_GMXXX.h is a library used to integrate the Multichannel Gas Sensor with Arduino. This sensor has the ability to detect a variety of gases, including Carbon monoxide (CO), Nitrogen dioxide (NO2), Ethanol (C2H5CH), Volatile Organic Compounds (VOC).
#include "seeed_bme680.h" // seeed_bme680.h is a library used to integrate the multiple sensor bme680 with Arduino. It is used to measure temperature, pressure and humidity.
#include "sensirion_common.h" //This library provides common definitions, constants, and functions useful for the development of software that interacts with sensors.
#include "sgp30.h" // This line includes a library for the SGP30 gas sensor. The SGP30 is a gas sensor that can measure various air quality parameters like CO2 and VOC (volatile organic compounds).
#include "TFT_eSPI.h" // This library is used to display information on our Wio Terminal's LCD screen.                         
#include "Seeed_FS.h" // This line includes a library for reading and writing files to an SD card or other storage devices.
#include "RawImage.h" // This library is used to load and display images from the SD card to the TFT LCD screen on Wio Terminal. 


#include "electronic_nose6_inferencing.h"             // Name of Edge Impulse library which contains the trained model

#define DEBUG               0                         // 1 to print out debugging info
#define DEBUG_NN            false                     // Print out Edge Impulse debugging info
#define ANOMALY_THRESHOLD   0.3                       // Scores above this are an "anomaly"
#define SAMPLING_FREQ_HZ    4                         // Sampling frequency (Hz)
#define SAMPLING_PERIOD_MS  1000 / SAMPLING_FREQ_HZ   // Sampling period (ms)
#define NUM_SAMPLES         EI_CLASSIFIER_RAW_SAMPLE_COUNT  // 4 samples at 4 Hz
#define READINGS_PER_SAMPLE EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME // 8 samples
#define BME680_I2C_ADDR     uint8_t(0x76)             // I2C address of BME680
#define PA_IN_KPA           1000.0                    // Convert Pa to KPa
#define LCD_BACKLIGHT (72Ul)

#include "BlynkEdgent.h" //This library is used to connect Blynk to the Arduino Wio Terminal device.

// Our dataset is in CSV format. It consists of 8 columns of data, where each column represents a feature 
// (captured through 3 different sensors) that we use to train our odor classifier. The 8 captured components 
// are: temperature, humidity, CO2, VOC1, VOC2, NO2, ethanol, and CO.

float mins[] = {
  27.08, 15.23, 400.0, 0.0, 1.22, 0.23, 0.6, 0.35 // mins[] represents the array of minimum values found for each of the 8 respective features that make up our detection dataset.
};
float ranges[] = {
  10.3, 56.59, 56930.0, 60000.0, 1.98, 2.73, 2.53, 2.93 // Ranges[] is an array that contains 8 values, each of which represents the difference between the maximum and minimum values captured for each of the 8 features used to construct the dataset. It provides a measure of the spread or variation in the data.
};

GAS_GMXXX<TwoWire> gas;               // Multichannel gas sensor v2
Seeed_BME680 bme680(BME680_I2C_ADDR); // Environmental sensor


TFT_eSPI tft;                         // Wio Terminal LCD

int sound_on = 1; // sound_on is a variable used as a flag to activate or deactivate the alarm that sounds as soon as a sufficiently high quantity of lighter_gas is detected in the air. The default state is 1 (sound on)

void beep(unsigned int hold = 1000) // This function triggers a sound very similar to what can be considered an alarm.
{
    analogWrite(WIO_BUZZER, 128);
    delay(hold);
    analogWrite(WIO_BUZZER, 0);
}

int sound(int sound_on){ 
  /* sound(...) is a function that sets the 'sound_on' variable to 1 or 0. 
  (This function is triggered when we press and hold the round blue button located on the bottom left 
  of the Wio Terminal's front panel). If the variable's state was 1, pressing the button will change the variable's state to 0, 
  which means the alarm sound will be deactivated. If the variable's state was 0 and it changes to 1, the alarm sound will be activated. 
  Feedback indicating whether the sound is activated or deactivated is provided by two icons representing the 'on' and 'off' states of the sound.
  */
  if (digitalRead(WIO_5S_PRESS) == LOW){
    if (sound_on == 1) {
      sound_on = 0;
      drawImage<uint8_t>("no-sound_neg.bmp", 280, 0);
    }else{
      sound_on = 1;
      drawImage<uint8_t>("volume_neg.bmp", 280, 0);
    }
  }else{
    if (sound_on == 1) {
      drawImage<uint8_t>("volume_neg.bmp", 280, 0);
    }else{
      drawImage<uint8_t>("no-sound_neg.bmp", 280, 0);
    }
  }
  return sound_on;
}

void setup()
{
  Serial.begin(115200);
  delay(100);

  BlynkEdgent.begin(); // Initialize the connection with Blynk.

  int16_t sgp_err;
  uint16_t sgp_eth;
  uint16_t sgp_h2;

  //Initialize SD card
  if(!SD.begin(SDCARD_SS_PIN, SDCARD_SPI)) {
    while (1);
  }

  // Configure LCD
  tft.begin();
  tft.setRotation(3);
  tft.setFreeFont(&FreeMonoBoldOblique24pt7b);
  tft.setTextColor(TFT_GREEN);
  tft.fillScreen(TFT_BLACK);

  // Initialize Multichannel gas sensor
  gas.begin(Wire, 0x08);

  // Initialize environmental sensor
  while (!bme680.init()) {
    Serial.println("Trying to initialize BME680...");
    delay(1000);
  }

  // Initialize VOC and eCO2 sensor
  while (sgp_probe() != STATUS_OK) {
    Serial.println("Trying to initialize SGP30...");
    delay(1000);
  }

  // Perform initial read
  sgp_err = sgp_measure_signals_blocking_read(&sgp_eth, &sgp_h2);
  if (sgp_err != STATUS_OK) {
    Serial.println("Error: Could not read signal from SGP30");
    while (1);
  }

  //Configure the 5-way switch button as input
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);
}

void loop() {
  BlynkEdgent.run(); // BlynkEdgent.run() is a main Blynk routine responsible for keeping connection alive, sending data, receiving data
  // The following portion of code declares several variables to store sensor readings, error codes, and other information.
  float gm_no2_v; // This variable is for Multichannel gas sensor readings
  float gm_eth_v; // This variable is for Multichannel gas sensor readings
  float gm_voc_v; // This variable is for Multichannel gas sensor readings
  float gm_co_v; // This variable is for Multichannel gas sensor readings
  float bme_temp; // This variable is for temperature sensor readings
  float bme_hum; // This variable is for humidity sensor readings
  float bme_press; // This variable is for pressure sensor readings
  int16_t sgp_err; // This variable is for capturing SGP30 sensor error codes
  uint16_t sgp_tvoc; // This variable is for SGP30 sensor readings
  uint16_t sgp_co2; // This variable is for SGP30 sensor readings
  unsigned long timestamp; // This variable is for storing the current time
  static float raw_buf[NUM_SAMPLES * READINGS_PER_SAMPLE]; // This is for storing raw sensor data
  static signal_t signal; // This variable is for signal processing
  float temp; // This variable is for temporary value storage
  int max_idx = 0; // This variable is for the index of the maximum prediction
  float max_val = 0.0; // This variable is for the maximum prediction value
  char str_buf[40]; // str_buf is for storing strings

  /* With the following line of code, we begin to lay the foundation for creating our dataset with all the measurements obtained from the various sensors 
  integrated with the Wio Terminal. The first step is to construct the header (the names of the different features) for our CSV file, 
  which will contain the dataset. To accomplish this, the Wio Terminal will send the constructed header to the serial port. This header 
  will then be used by the Python script 'collect_csv_from_serial_port.py,' which is responsible for the final dataset construction.
  */
  Serial.println("timestamp,temp,humd,pres,co2,voc1,voc2,no2,eth,co");
  
  // Collect samples and perform inference
  for (int i = 0; i < NUM_SAMPLES; i++) {

    // Take timestamp so we can hit our target frequency
    timestamp = millis();

    // Read from Multichannel sensor
    gm_no2_v = gas.calcVol(gas.getGM102B());
    gm_eth_v = gas.calcVol(gas.getGM302B());
    gm_voc_v = gas.calcVol(gas.getGM502B());
    gm_co_v = gas.calcVol(gas.getGM702B());
  
    // Read BME680 environmental sensor
    if (bme680.read_sensor_data()) {
      Serial.println("Error: Could not read from BME680");
      return;
    }

    bme_temp = bme680.sensor_result_value.temperature;
    bme_hum = bme680.sensor_result_value.humidity;
    bme_press = bme680.sensor_result_value.pressure;


    // Read SGP30 sensor
    sgp_err = sgp_measure_iaq_blocking_read(&sgp_tvoc, &sgp_co2);
    if (sgp_err != STATUS_OK) {
      Serial.println("Error: Could not read from SGP30");
      return;
    }

    /* With the following lines of code, the values detected for each component that can be captured by our three sensors are sent from the 
    Wio Terminal to the serial port. This allows the Python script 'collect_csv_from_serial_port.py' to listen on that port and capture real-time 
    readings, which will be used to compose the final dataset.
    */
    Serial.print(timestamp);
    Serial.print(",");
    Serial.print(bme_temp);
    Serial.print(",");
    Serial.print(bme_hum);
    Serial.print(",");
    Serial.print(bme_press / PA_IN_KPA);
    Serial.print(",");
    Serial.print(sgp_co2);
    Serial.print(",");
    Serial.print(sgp_tvoc);
    Serial.print(",");
    Serial.print(gm_voc_v);
    Serial.print(",");
    Serial.print(gm_no2_v);
    Serial.print(",");
    Serial.print(gm_eth_v);
    Serial.print(",");
    Serial.print(gm_co_v);
    Serial.println();


    // Store raw data into the buffer
    raw_buf[(i * READINGS_PER_SAMPLE) + 0] = bme_temp;
    raw_buf[(i * READINGS_PER_SAMPLE) + 1] = bme_hum;
    raw_buf[(i * READINGS_PER_SAMPLE) + 2] = sgp_co2;
    raw_buf[(i * READINGS_PER_SAMPLE) + 3] = sgp_tvoc;
    raw_buf[(i * READINGS_PER_SAMPLE) + 4] = gm_voc_v;
    raw_buf[(i * READINGS_PER_SAMPLE) + 5] = gm_no2_v;
    raw_buf[(i * READINGS_PER_SAMPLE) + 6] = gm_eth_v;
    raw_buf[(i * READINGS_PER_SAMPLE) + 7] = gm_co_v;

    // Perform preprocessing step (normalization) on all readings in the sample.
    // After collecting data, the code normalizes the readings by subtracting minimum values and dividing by ranges for each sensor reading. 
    // This preprocessing step is essential for preparing data for further analysis or classification.
    for (int j = 0; j < READINGS_PER_SAMPLE; j++) {
      temp = raw_buf[(i * READINGS_PER_SAMPLE) + j] - mins[j];
      raw_buf[(i * READINGS_PER_SAMPLE) + j] = temp / ranges[j];
    }

    // Wait just long enough for our sampling period
    while (millis() < timestamp + SAMPLING_PERIOD_MS);
  }

  // Print empty line to transmit termination of recording
  Serial.println();

  // Print out our preprocessed, raw buffer
#if DEBUG
  for (int i = 0; i < NUM_SAMPLES * READINGS_PER_SAMPLE; i++) {
    Serial.print(raw_buf[i]);
    if (i < (NUM_SAMPLES * READINGS_PER_SAMPLE) - 1) {
      Serial.print(", ");
    }
  }
  Serial.println();
#endif

  // Turn the raw buffer in a signal which we can classify
  int err = numpy::signal_from_buffer(raw_buf, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
  if (err != 0) {
      ei_printf("ERROR: Failed to create signal from buffer (%d)\r\n", err);
      return;
  }

  // Run inference
  // The code then creates a signal from the preprocessed data and runs inference using a machine learning classifier. 
  ei_impulse_result_t result = {0};
  err = run_classifier(&signal, &result, DEBUG_NN);
  if (err != EI_IMPULSE_OK) {
      ei_printf("ERROR: Failed to run classifier (%d)\r\n", err);
      return;
  }

  // Find maximum prediction
  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > max_val) {
      max_val = result.classification[i].value;
      max_idx = i;
    }
  }

  /*
  Display and Blynk Integration: Depending on the classification results, the following code displays information on the TFT screen. 
  If an anomaly is detected below a certain threshold and the classification is not "lighter_gas," it displays the label and value in green. 
  If the classification is "lighter_gas" and the value is less than 0.80, it displays similar information in green. 
  If the classification is "lighter_gas" and the value is greater than or equal to 0.80, it displays the label and value in red. 
  Additionally, a warning about a potential gas leak is displayed, and a notification is sent to the smartphone using the line "Blynk.logEvent("lighter_gas")".
  If none of these conditions are met, it displays "Unknown" in green.
  
  In addition to the commands described earlier, integration with Blynk primarily occurs through the following methods:
  Blynk.virtualWrite(V0, bme_temp): this line updates a Blynk virtual pin (V0) with the current temperature reading obtained from the BME680 environmental sensor.
  Blynk.virtualWrite(V3, bme_hum): similarly, this line updates a Blynk virtual pin (V3) with the current humidity reading from the BME680 sensor. 
  Blynk.virtualWrite(V4, sgp_co2): this line updates a Blynk virtual pin (V4) with the CO2 concentration reading obtained from the SGP30 sensor. 
  Blynk.virtualWrite(V5, gm_no2_v): this line updates a Blynk virtual pin (V5) with the nitrogen dioxide (NO2) concentration reading from one of the Multichannel gas sensor(gm_no2_v). 
  Blynk.virtualWrite(V6, gm_eth_v): similarly, this line updates a Blynk virtual pin (V6) with the ethanol concentration reading obtained from one of the Multichannel gas sensor(gm_eth_v).
  Blynk.virtualWrite(V7, gm_co_v): this line updates a Blynk virtual pin (V7) with the carbon monoxide (CO) concentration reading from one of the Multichannel gas sensor(gm_co_v).
  
  These lines essentially feed sensor data into the Blynk platform, allowing you to visualize and track sensor real-time readings remotely via the Blynk app. 

  To give the user the ability to enable or disable the audio at any time, the sound_on variable is used, which is linked to the sound() function that manages and allows the current audio state to be stored.
  If the classification is "lighter_gas" and the prediction value is greater than or equal to 0.80 and sound_on is equal to 1, the beep() function is invoked. 
  */
  
  tft.fillScreen(TFT_BLACK);
  if (result.anomaly < ANOMALY_THRESHOLD && result.classification[max_idx].label != "lighter_gas") {
    tft.setFreeFont(&FreeMonoBoldOblique24pt7b);
    tft.setTextColor(TFT_GREEN);
    tft.drawString(result.classification[max_idx].label, 12, 60);
    sprintf(str_buf, "%.3f", max_val);
    tft.drawString(str_buf, 60, 120);
    sound_on = sound(sound_on);
    Blynk.setProperty(V1, "color", "#01DF01"); //green HTML color code
    Blynk.virtualWrite(V1, result.classification[max_idx].label);
    Blynk.setProperty(V2, "color", "#23C48E"); //green HTML color code
    Blynk.virtualWrite(V2, result.classification[max_idx].value * 100);
    Blynk.virtualWrite(V8, " ");
    }
  else if (result.anomaly < ANOMALY_THRESHOLD 
  && result.classification[max_idx].label == "lighter_gas" 
  && result.classification[max_idx].value < 0.80){
    tft.setFreeFont(&FreeMonoBoldOblique24pt7b);
    tft.setTextColor(TFT_GREEN);
    tft.drawString(result.classification[max_idx].label, 12, 60);
    sprintf(str_buf, "%.3f", max_val);
    tft.drawString(str_buf, 60, 120);;
    sound_on = sound(sound_on);
    Blynk.setProperty(V1, "color", "#01DF01"); // green HTML color code
    Blynk.virtualWrite(V1, result.classification[max_idx].label);
    Blynk.setProperty(V2, "color", "#23C48E"); //green HTML color code
    Blynk.virtualWrite(V2, result.classification[max_idx].value * 100);
    Blynk.virtualWrite(V8, " ");
  }
  else if (result.anomaly < ANOMALY_THRESHOLD 
  && result.classification[max_idx].label == "lighter_gas" 
  && result.classification[max_idx].value >= 0.80){
    tft.setFreeFont(&FreeMonoBoldOblique24pt7b);
    tft.setTextColor(TFT_RED);
    sound_on = sound(sound_on);
    tft.drawString(result.classification[max_idx].label, 12, 60);
    sprintf(str_buf, "%.3f", max_val);
    tft.drawString(str_buf, 60, 120);
    tft.setFreeFont(&FreeSansOblique12pt7b);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Attention, there could be", 25, 180);
    tft.drawString("a potential gas leak.", 45, 210);
    if (sound_on == 1) {
      beep(1000);
    }
    Blynk.setProperty(V1, "color", "#FF0000"); // red HTML color code
    Blynk.virtualWrite(V1, result.classification[max_idx].label);
    Blynk.setProperty(V2, "color", "#D3435C"); // red HTML color code
    Blynk.virtualWrite(V2, result.classification[max_idx].value * 100);
    Blynk.setProperty(V8, "color", "#EFFBF8"); // white HTML color code
    Blynk.virtualWrite(V8, "Attention, there could be a potential gas leak.");
    Blynk.logEvent("lighter_gas");

  } else {
    tft.setFreeFont(&FreeMonoBoldOblique24pt7b);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Unknown", 20, 60);
    sprintf(str_buf, "%.3f", result.anomaly);
    tft.drawString(str_buf, 60, 120);
    sound_on = sound(sound_on);
  }

  Blynk.virtualWrite(V0, bme_temp);
  Blynk.virtualWrite(V3, bme_hum);
  Blynk.virtualWrite(V4, sgp_co2);
  Blynk.virtualWrite(V5, gm_no2_v);
  Blynk.virtualWrite(V6, gm_eth_v);
  Blynk.virtualWrite(V7, gm_co_v);
}
