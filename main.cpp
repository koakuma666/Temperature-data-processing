/*  Author MorseMeow
    Date   25th December 2019
*/

#include "mbed.h"
#include "N5110.h"
#include "system.h"

#define KEY1 0
#define KEY2 1

Timer debounce;             // Define anti-shake timer
Timer duration;             // Defines the  button duration timer 
Ticker sampling;            // Define the intermittent device (timer interrupt) and name it sampling
AnalogIn Ain(p15);   //lm35Vout connected to pin15  
DigitalIn button1(p16); 
DigitalIn button2(p17);
InterruptIn keyInt1(p16);   // Define and name interrupt inputs
InterruptIn keyInt2(p17);
DigitalOut menuLed(LED1);   // Menu key status indicator
DigitalOut moreFunc(LED2);  // Secondary function status indicator
LocalFileSystem local("local");
//    VCC,SCE,RST,D/C,MOSI,SCLK,LED
N5110 lcd(p7,p8,p9,p10,p11,p13,p21);  // LPC1768 - pwr from GPIO

union shiftBuff {
    uint64_t colData;
    unsigned char colImg[8];
} _shifBuff;
// Declared an enum type of menus
enum menus { INFO, GRAP, MENUS_COUNT };
// Declare a KeyState enumeration type
enum KeyState { IDLE, PRESSED, HOLD };
// Declare a BT structure type, save the digital input pin of each button, external interrupt pin
// And button status values
struct BT {
    DigitalIn *_key;
    InterruptIn *_keyInt;
    enum KeyState state;
};
// The declaration defines a BT type structure array, which is used to save the information of each key separately.
struct BT button[] = {
    {&button1, &keyInt1, IDLE},
    {&button2, &keyInt2, IDLE}
};
// button[i]._key -> read();
// button[i]._keyInt -> mode();
// button[i].state
enum KeyState * activeKey = NULL;   // Point to the status value of the currently active key
// Automatically count the number of keys
const uint8_t numButtons = sizeof(button) / sizeof(button[0]);
// If press the button several times before the LED reacts once, decrease the value.
// If press the button once, the LED responds multiple times, increase the value
const int debounceTime = 20;
int menuState = INFO;      // Menu page status number
bool pwrState = 0;         // Device switch status flag
bool moreFuncState = 0;    // Secondary function status flag
volatile unsigned int sampCount = 1;  // Sample count
volatile unsigned int grapCursor = 0; 
// Data point count to determine whether the number of data points on the screen exceeds the number of pixels in the screen width

void rizeISR();
void fallISR();
void tickerISR();
enum KeyState readKey(int keyIndex);
void displayMov(int x0, int y0);
// Functions with only different parameter lists are declared as overloaded functions
inline void displayImg(const unsigned char *img);
void displayImg(int x0, int y0, const unsigned char *img);
inline void showTemp(const unsigned int x,
                     const unsigned int y,
                     int temp);
float MV_Filter(AnalogIn *_Ain, unsigned int FILTER_N);
void info_update();
void Grap_update();

// Menu page transfer table
void (*menuPage[MENUS_COUNT])() = {info_update, Grap_update};

int main() {

	set_time(1577836800);  // Set RTC time to 2020/1/1 00:00:00（UTC time without time zone）
    
    FILE* dataFile = fopen("/local/datafile.csv", "a");
	fputs("Date,Week,Time,Temperature\n", dataFile);
	fclose(dataFile);

    debounce.start();
    duration.start();
    for(int i = 0; i < numButtons; i++) {
        button[i]._keyInt -> mode(PullDown); // Set input pin mode
        button[i]._keyInt -> rise(rizeISR);  // Add the interrupt service routine address at the rising edge of the interrupt. The parameter can be written to ISR or & ISR
        button[i]._keyInt -> fall(fallISR);
    }

    while(1) {
        static bool prevPwrState = 0;   // Device's last on / off state
        static int preMenuState = INFO;
        enum KeyState _keyState = readKey(KEY1);
        if(_keyState == HOLD) pwrState = !pwrState;

        if(pwrState != prevPwrState) {
            if(pwrState) {        // Device boot
                sampCount = 1;    // Reset the sample count
                moreFuncState = 0;// Reset secondary function flag
                menuState = INFO; // Reset menu page
                preMenuState = INFO;          
                // Reset all key states to idle
                for(int i = 0; i < numButtons; i++)
                	readKey(i);
                // first need to initialise display
                lcd.init();
                // change set contrast in range 0.0 to 1.0
                lcd.setContrast(0.4);
                // Show boot animation
                __disable_irq(); // Disable all external interrupts
                lcd.clear();              
                for(int i = 0; i < NUMWELC; i++) {
                    displayImg(welcome[i]);
                    lcd.refresh();
                    if(i == 7) wait(2.0);
                    wait_ms(80);
                }
                tickerISR();
                __enable_irq();  // Enable all external interrupts
                sampling.attach(tickerISR, 0.5);
            }
            else {               // Device shutdown
                // Show shutdown animation
                menuLed = 0;     // Menu key status indicator is off
                moreFunc = 0;    // Secondary function status indicator is off
                sampling.detach();
                __disable_irq(); // Disable all external interrupts
                lcd.clear();
                for(int i = 0; i < NUMGBYE; i++) {
                    displayImg(goodbye[i]);
                    lcd.refresh();
                    if(i == 10) wait(2.0);
                    wait_ms(80);
                }
                lcd.turnOff();
                __enable_irq();  // Enable all external interrupts
            }
            prevPwrState = pwrState;
        }
        if(pwrState) {
        	if(_keyState == PRESSED) {
        		++menuState %= MENUS_COUNT;
        		menuLed = !menuLed;
            }
            if(readKey(KEY2) == PRESSED) {
            	moreFuncState = !moreFuncState;
            	moreFunc = !moreFunc;
            }
            if(menuState != preMenuState) {

            	sampCount = 1;    // Reset the sample count
            	grapCursor = 0;
            	moreFuncState = 0;// Reset secondary function flag
            	moreFunc = 0;     // Secondary function status indicator is off
            	sampling.detach();
            	__disable_irq();  // Disable interrupt
            	lcd.clear();
            	if(menuState == INFO) {	                            
	                for(int i = 0; i < NUMINFO; i++) {
	                    displayImg(info[i]);
	                    lcd.refresh();
	                    wait_ms(80);
	                }
	                tickerISR();
            	}
            	else if(menuState == GRAP) {      
	                for(int i = 0; i < NUMGRAP; i++) {
	                    displayImg(graph[i]);
	                    lcd.refresh();
	                    wait_ms(80);
	                }	               
            	}
            	__enable_irq();  // Enable interrupt
            	sampling.attach(tickerISR, 0.5);
            	preMenuState = menuState;
            }
        }
    }
}

/** Rising edge interrupt service routine
 *	
 *  Calibrate active button, reset button duration timer and anti-shake timer
 */
void rizeISR() {            // This function defines the interrupt response, that is, the interrupt service routine
    if(debounce.read_ms() >= debounceTime) {
        int j = 0;
        for(int i = 0; i < numButtons; i++) { // Read the level of each button
            if(button[i]._key -> read()) {    // The high level of a key will point the enum pointer to the state value corresponding to this key, ready to modify
                activeKey = &button[i].state;
                j++;        // Record several high-level keys with the same rising edge interrupt
            }
            if(j > 1) activeKey = NULL;  // The same rising edge interrupt allows only one key to be pressed
        }
        duration.reset();
    }
    debounce.reset();
}

/** Button falling edge interrupt service routine
 *	
 *  Recognize short and long presses of keys and record status flags
 */
void fallISR() {            // This function defines the interrupt response, that is, the interrupt service routine
    if(debounce.read_ms() >= debounceTime) {
        if(activeKey != NULL) {
            if(duration.read_ms() >= 1000) {
                *activeKey = HOLD;
            }
            else {
                *activeKey = PRESSED;
            }
        }
    }
    debounce.reset();
}

/** Ticker interrupt device interrupt service routine
 *	
 *  Run the correct menu page transfer table
 */
void tickerISR() {
	menuPage[menuState]();
}

/** Read key status
 *  
 *  @param  keyIndex - the index number of key
 *  @return          - the state of key
 */
enum KeyState readKey(int keyIndex) 
{
    enum KeyState state = button[keyIndex].state;
    button[keyIndex].state = IDLE;
    return state;
}

/** Move the picture being displayed
 * 	
 *  @param x0 - The x co-ordinate of the picture (0 to 83)
 *  @param y0 - The y co-ordinate of the picture (0 to 47)
 */
void displayMov(int x0, int y0)
{
    unsigned char imgBuff[84][6] = {};
    int k = 0, imgBuffIndex, bufferIndex;
    
    if(x0 < -WIDTH || x0 > WIDTH) x0 = 0;      //Limit x0 position
    if(y0 < -HEIGHT || y0 > HEIGHT) y0 = 0;    //Limit y0 position

    for(int i = abs(x0); i < WIDTH; i++) {     //
        if(x0 >= 0) { imgBuffIndex = i; bufferIndex = k++; }
        else { imgBuffIndex = k++; bufferIndex = i; }

        for(int j = 0; j < BANKS; j++) { // Traverse all pixels from 0 to 5, y direction
            _shifBuff.colImg[j] = lcd.buffer[bufferIndex][j]; // Store a list of images into the column image buffer and wait for shift
        }
        // Column image buffer for each column (84 times per frame)
        y0 >= 0 ? _shifBuff.colData >>= y0 : _shifBuff.colData <<= abs(y0);
        for(int j = 0; j < BANKS; j++) { // Traverse all pixels from 0 to 5, y direction
            imgBuff[imgBuffIndex][j] = _shifBuff.colImg[j];   // Store the shifted column image buffer into imgBuff and wait for display
        }
        // Without this statement, the last line of the picture is displayed incorrectly when the picture is moved from 0 to 48.
        _shifBuff.colData = 0; //Clear column image buffer data
    }
    for(int i = 0; i < BANKS; i++) {     // be careful to use correct order (j,i) for horizontal addressing
        for(int j = 0; j < WIDTH; j++) { // First traverse 0 ~ 83, X-direction pixel horizontal addressing
            lcd.buffer[j][i] = imgBuff[j][i];  // Store input image in buffer and wait for display
        }
    }
}

/** Display picture array
 *	
 * 	Write picture directly to image buffer
 * 	@param img - Modulo array of pictures
 */
inline void displayImg(const unsigned char *img)
{
    for(int i = 0; i < BANKS; i++) {     // be careful to use correct order (j,i) for horizontal addressing
        int numRows = i * WIDTH;
        for(int j = 0; j < WIDTH; j++) { // First traverse 0 ~ 83, X-direction pixel horizontal addressing
            lcd.buffer[j][i] = img[numRows + j];  // Store input image in buffer and wait for display
        }
    }
}

/** Display picture array
 *	
 * 	Write the picture directly to the image buffer and move the picture in the image buffer
 *  @param x0  - The x co-ordinate of the picture (0 to 83)
 *  @param y0  - The y co-ordinate of the picture (0 to 47)
 *  @param img - Modulo array of pictures
 */
void displayImg(int x0, int y0, const unsigned char *img)
{
    displayImg(img);        // Store the input image in the buffer and wait for processing
    displayMov(x0, y0);     // Moving image
}

/** Display custom digital font in temperature information interface
 *  
 *  @param x    - The x co-ordinate of the picture (0 to 83)
 *  @param y    - The y co-ordinate of the picture (0 to 47)
 *  @param temp - Temperature value
 */
inline void showTemp(const unsigned int x,
                     const unsigned int y,
                     int temp)
{
    if(temp >= 0 && temp < 100) {
        lcd.drawSprite(x, y, 7, 7, infoNum[temp / 10][0]);
        lcd.drawSprite(x + 8, y, 7, 7, infoNum[temp % 10][0]);
    }
}

float MV_Filter(AnalogIn *_Ain, unsigned int FILTER_N)  // Median filtering
{
    float filter_temp;
    float filter_buf[FILTER_N];
    for(int i = 0; i < FILTER_N; i++) {
        filter_buf[i] = _Ain->read();
        wait_ms(1);
    }
    // Sampling values are arranged from small to large (bubble method)
    for(int j = 0; j < FILTER_N - 1; j++) {
        for(int i = 0; i < FILTER_N - 1 - j; i++) {
            if(filter_buf[i] > filter_buf[i + 1]) {
                filter_temp = filter_buf[i];
                filter_buf[i] = filter_buf[i + 1];
                filter_buf[i + 1] = filter_temp;
            }
        }
    }
    return filter_buf[(FILTER_N - 1) / 2];
}

/** Temperature information display page
 *
 * 	Display Celsius, Fahrenheit, Max Min and Min Temperature
 */
void info_update() 
{
    // If the display interface is switched, temp_sum and temp_buf do not need to be cleared separately.
	float temp_sum = 0;
	static float temp_buf[60 + 1] = {};
	static int min = 40, max = 0;
    float value = MV_Filter(&Ain, 31) * 3.3 * 100.0;
    int tempCels = (int)value;
    int tempFahr = value * 1.8 + 32.0;
	
    showTemp(17, 5, tempCels);
    lcd.drawSprite(9, 16, 7, 7, infoNum[tempFahr / 100][0]);
    lcd.drawSprite(17, 16, 7, 7, infoNum[(tempFahr % 100) / 10][0]);
    lcd.drawSprite(25, 16, 7, 7, infoNum[(tempFahr % 100) % 10][0]);
    // Average display section
    // 0 ~ 9，10 ~ 19，20 ~ 29...Put a temperature value into the moving average buffer every 1s (sampling 10 times)
    // The initial value of sampCount is 0 and 1 does not affect. If it is 0, the temperature is placed 61 times in 1 min. If it is 1, it is placed 60 times in 1 min.
    if(!(sampCount % 2)) {  // Determine if the time is up to 1s
    	
    	temp_buf[60] = value;
    	for(int i = 0; i < 60; i++) {
    		temp_buf[i] = temp_buf[i + 1]; // All data is shifted to the left, the lower bits are still dropped
			temp_sum += temp_buf[i];       // Sum the entire array every 1s
    	}
    }
    // Maximum value display section
    if(moreFuncState) {     // If the maximum value is enabled
		if(tempCels > max) max = tempCels;
		if(tempCels < min) min = tempCels;		
		showTemp(11, 38, max);
		showTemp(51, 38, min);
	}
	else {
		min = 40; max = 0;  // Reset the minimum value variable after turning off the minimum value display function
		showTemp(11, 38, 0);
		showTemp(51, 38, 0);
	}
	// Determine if the time is up to 1min
	if(sampCount == 120) {
		sampCount = 1;
		int tempAver = temp_sum / 60.0;
		showTemp(51, 16, tempAver);
	}
	else sampCount++;
    lcd.refresh();
}

/** Temperature image display page
 *
 * 	Update a temperature data point every 500ms and record a temperature log file every minute
 */
void Grap_update() 
{
    float value = MV_Filter(&Ain, 31) * 3.3 * 100.0;

    // Limit temperature to 0 ~ 45 degrees, can record up to 46 kinds of temperature
    if(value > 45.0) value = 45.0;
    // If the data on the first page of the screen is not full, the first page is full and the screen is not scrolled
    if(grapCursor < WIDTH) {
        lcd.setPixel(grapCursor, 45 - (int)value, true);
        grapCursor++;
    }
    else {
        // Draw the image for the column x = 83
        displayMov(-1, 0);
        for(int y = 0; y < 5; y++) {   // Y-axis digital headroom
            lcd.setPixel(1, 4 + y, false);
            lcd.setPixel(2, 4 + y, false);
            lcd.setPixel(1, 24 + y, false);
            lcd.setPixel(2, 24 + y, false);
        }
        for(int y = 0; y < 45; y++) {  // Redraw Y axis
            lcd.setPixel(0, y, true);
        }
        lcd.setPixel(1, 6, true);   // Redraw two points on the Y axis
        lcd.setPixel(1, 26, true);  // Redraw two points on the Y axis
        lcd.setPixel(83, 45, true); // Complete the last point of the X axis
        lcd.drawSprite(3, 4, 5, 7, grapNum[1][0]);
        lcd.drawSprite(3, 24, 5, 7, grapNum[0][0]);
        lcd.setPixel(83, 45 - (int)value, true);  // Draw temperature point 0 ~ 45 degrees, can record up to 46 kinds of temperature
        // Draw a coordinate point on the horizontal axis every 10 samples
        // The initial value of sampCount is 0 and 1 will affect the position of the first abscissa on the screen representing 10
        // sampCount is 0, the first abscissa point representing 10 is at (10, 44), and 1 is at (9, 44)
        if(!(sampCount % 10)) {     // After 10 sampling times
            lcd.setPixel(83, 44, true); // Draw a coordinate point on the X axis
            // lcd.drawSprite(3, 4, 5, 7, grapNum[1][0]);
            // lcd.drawSprite(3, 24, 5, 7, grapNum[0][0]);
        }
    }
    // Determine if the time is up to 1min
    if(sampCount == 120) {      // After 120 sampling times, the value should be a multiple of 10.
        if(moreFuncState) {     // If logging is enabled
        	time_t seconds = time(NULL);
	        // printf("Time as seconds since January 1, 1970 = %d\n", seconds);  
	        // printf("Time as a basic string = %s", ctime(&seconds));
	 
	        char dateBuffer[24] = {};
	        char dataBuffer[32] = {};
	        strftime(dateBuffer, 24, "%F,%a,%T", localtime(&seconds));
	        sprintf(dataBuffer, "%s,%.2f\n", dateBuffer, value);
	        printf("Time as a custom formatted string = %s\n", dateBuffer);
	        printf("%s", dataBuffer);

	        FILE* dataFile = fopen("/local/datafile.csv", "a");
	        fputs(dataBuffer, dataFile);
	        fclose(dataFile);
        }
        sampCount = 1;
    }
    else sampCount++;
    lcd.refresh();
}