
/* This code is sued for starting continous ranging on SRF02 sensor
 * 
 * (c) Red Pitaya  http://www.redpitaya.com
 *
 * This part of code is written in C programming language.
 * Please visit http://en.wikipedia.org/wiki/C_(programming_language)
 * for more details on the language used herein.
 */

 
 
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <math.h>

#include "rp.h"
#include "_kiss_fft_guts.h"
#include "kiss_fft.h"
#include "kiss_fftr.h"



#define I2C_SLAVE_FORCE 		   0x0706
#define I2C_SLAVE    			   0x0703    /* Change slave address            */
#define I2C_FUNCS    			   0x0705    /* Get the adapter functionality */
#define I2C_RDWR    			   0x0707    /* Combined R/W transfer (one stop only)*/


#define	V_SONIC_WAVE				343.2		// [m/s] Schallgeschwindigkeit in Luft
#define MAX_CHAR					25


#ifdef DEBUG
 #define DBG(x)			printf("DBG >> %-25s:\n", x)
 #define DBG_VAR(x)		printf("DBG >> %-25s%25.9f\n",#x, (double)x)
 #define DBG_STRING(x)	printf("DBG >> %-25s%-30s\n"  ,#x ,x )
#else
 #define DBG(x)			(void)0
 #define DBG_VAR(x)		(void)0
 #define DBG_STRING(x)	(void)0
#endif



typedef enum
{
	ERR_NO				=  0,
	ERR_LOGIC			= -1,
	ERR_FILE_CREATION	= -2,
	ERR_FILE_OPEN		= -3,
	ERR_DATA			= -4,
	ERR_COM				= -5,
	ERR_I2C				= -6,
	ERROR_FILE			= -7,
	ERR_FATAL			= -127
}error_t;


// global constant variables
const uint32_t	ADC_SAMPLE_FREQUENCY	= 125000000;						// [Hz]
const uint32_t	ADC_SAMPLE_DECIMATION	= 64;								// [-]
const uint32_t	ADC_SAMPLE_TIME			=  8;								// [ns]
const int32_t	ADC_DELAY_OFFSET_US		=  0;								// [µs]						--> new variable - has to be evaluated
const uint32_t	SONIC_SPEED_CM_per_SEC	= (uint32_t)(V_SONIC_WAVE * 100);	// [cm/s] --> 343,2 m/s * 100 = 34.200 cm/s
const uint32_t	MIN_DISTANCE_CM			= 30;								// [cm] 
const uint32_t	MIN_FFT_FREQ			= 20000;
const uint32_t	MAX_FFT_FREQ			= 60000;
const int32_t	FREQ_CORRECTION			= 0;
const char		FILE_NAME_FFT[]			= "measFFT_---.txt";
const char		FILE_NAME_ADC[]				= "data.txt";
const uint8_t	MEASURE_COUNTS			= 5;

// global variables
float	distance_faktor;



/*
*
*	LD_LIBRARY_PATH=/opt/redpitaya/lib ./iic
*
*/
void init_adc(void);
int translate_argument( char **argument);
int normalize_data( uint32_t length, int16_t *data_in, uint16_t *data_out_maximum );
int measure_distance( uint16_t *data_in, double *data_out );
int calc_fft( uint32_t length, int16_t *data_in, float *data_out );

//int save_fft( uint32_t length, float *data_in, uint8_t number );
int save_data( uint32_t length, int16_t *data_in, uint8_t number );

int create_file( const char* file_name, char* file_target, uint8_t number );
double				distance_mean			= 0.0;



int main(int argc, char **argv)
{
	
	// variables for IIC
	int						address		= 0x70;					// Address of the SRF08 shifted right 1 bit
	unsigned char			buf[10];							// Buffer for data being read/ written on the i2c bus
	char					*fileName	= "/dev/i2c-0";			// Name of the port we will be using	
	int fd;														// File descriptor
	
	
	
	// variables for measurement
	uint32_t			adc_sample_time_ns		= ( ADC_SAMPLE_DECIMATION * ADC_SAMPLE_TIME );
	uint32_t			adc_meas_delay_us		= (uint32_t)(( adc_sample_time_ns * ADC_BUFFER_SIZE ) / 1e3);
	uint32_t			adc_start_delay_us		= ADC_DELAY_OFFSET_US + (uint32_t)(( MIN_DISTANCE_CM * 2 * 1e6 ) / SONIC_SPEED_CM_per_SEC );
	uint32_t			adc_buf_size			= (uint32_t)ADC_BUFFER_SIZE;		// 16.384
	
	float				max_distance			= ( (adc_start_delay_us + adc_meas_delay_us) * V_SONIC_WAVE ) / (2*1e6);
	double				distance				= 0.0;
	double				distance_max			= 0.0;
	double				distance_min			= 1e6;

	double				distance_delta			= 0.0;
	int8_t				n						= 0;
	
	
	
	distance_faktor = (float)( ( (float)(V_SONIC_WAVE) / 2.0) * (float)( adc_sample_time_ns ) ) / 1e9 ;	// [ m / sample]
	
	
	// Arguments will be evaluated later
	if( argc > 1){
		translate_argument(argv);
	}
	else{
		// if no arguments are present, the default
		// values will be chosen
	}
	
	
	if ((fd = open(fileName, O_RDWR)) < 0) {						// Open port for reading and writing
		printf("Cannot open i2c port\n");
		exit(1);
	}

	if (ioctl(fd, I2C_SLAVE_FORCE, address) < 0) {					// Set the port options and set the address of the device we wish to speak to
		printf("Unable to get bus access to talk to slave\n");
		exit(1);
	}
	
	
	// Proof all relevant variables in debug output
	DBG_VAR( V_SONIC_WAVE );
	DBG_VAR( SONIC_SPEED_CM_per_SEC );
	DBG_VAR( MIN_DISTANCE_CM );
	DBG_VAR( ADC_DELAY_OFFSET_US );
	DBG_VAR( adc_sample_time_ns );
	DBG_VAR( adc_start_delay_us );
	DBG_VAR( adc_meas_delay_us );
	DBG_VAR( max_distance );
	DBG_VAR( distance_faktor );
	
	remove("data.txt");
	remove("distance.txt");
	init_adc();
	
	while( n < MEASURE_COUNTS )
	{
		uint16_t	maximum;
		float		*fft_data = (float *)calloc( (adc_buf_size/2 + 1), sizeof(float));
		int16_t		*adc_data = (int16_t *)calloc( adc_buf_size, sizeof(int16_t));
		
		buf[0] = 0;
		buf[1] = 81;

		
		// I²C programming - start ultrasonic
		if ((write(fd, buf, 2)) != 2) {
			printf("Error writing to i2c slave\n");
			exit(ERR_I2C);
		}
		
		
		// wait for sonic to pass by 
		// minimum distance
		usleep( adc_start_delay_us );
		
		
		// ADC is started
		rp_AcqStart();
		
		
		// wait until buffer is full
		usleep( adc_meas_delay_us );
		
		
		// ADC is stopped
		rp_AcqStop();
		
		
		// get data out of ADC buffer.
		rp_AcqGetLatestDataRaw( RP_CH_1, &adc_buf_size, adc_data );
		
		
		if( normalize_data( adc_buf_size, adc_data, &maximum ) < 0 ){
			printf("No reflected signal detected!\n");
			continue;
		}
		
		
		if( measure_distance( &maximum, &distance ) < 0 ){
			printf("No maximum found!\n");
		}
		else{
			n++;
			printf("%03i: %.3f m - adc_max: %i\n", n, distance, (uint16_t)(abs(adc_data[maximum])) );
			if( distance > distance_max ){
				distance_max = distance;
			}
			else if( distance < distance_min ){
				distance_min = distance;
			}
			distance_mean += distance;
		}
		
		
		if( calc_fft( adc_buf_size, adc_data, fft_data ) != ERR_NO ){
			printf("Error in calculation!\n");
			exit( ERR_DATA );
		}
		
		
		if( save_data( adc_buf_size, adc_data, n ) != ERR_NO ){
			printf("Error saving data adc\n");
			exit( ERROR_FILE );
		}
		
		
/*		if( save_fft( adc_buf_size, fft_data, n)  != ERR_NO ){
			printf("Error saving data fft\n");
		}
*/		
		
		free( fft_data );
		free( adc_data );
		
	}
	
	distance_mean /= n;
	distance_delta = distance_max - distance_min;
	
	printf("\nStats from %i measurement(s):\n----------------------------\n", MEASURE_COUNTS);
	
	DBG_VAR( distance_min );
	DBG_VAR( distance_mean );
	DBG_VAR( distance_max );
	DBG_VAR( distance_delta );
	
	rp_Release();
	
    return( EXIT_SUCCESS );
}



void init_adc(void)
{
	rp_Init();
	rp_DpinSetDirection( RP_DIO0_P, RP_IN);							// DIO0 as input
	rp_DpinSetState( RP_DIO0_P, RP_LOW);							// DIO0 set to low
	//rp_AcqSetDecimation( RP_DEC_64 );								// Decimation 64 --> 64 * 8 ns = 512 ns / sample
	rp_AcqSetSamplingRate( RP_SMP_1_953M );							// Sample rate 1.953Msps; Buffer time length 8.388ms; Decimation 64
	rp_AcqSetTriggerSrc( RP_TRIG_SRC_DISABLED );					// Trigger disabled
	rp_AcqSetArmKeep( false );
	rp_AcqSetTriggerDelay( 0 );
	rp_AcqStart();
	usleep( 10 );
	rp_AcqStop();
}



/*
* @brief	This function evaluates the external parameters
*
* @details	
* 
*
*
*/
int translate_argument( char **argument)
{
	int ret = -1;
	volatile char *p;
	//char *p = (char *)argument[1];
	
	p = memchr( argument[1], '-', strlen( argument[1] ) );
	
	if( p != NULL ){
		
		*(p++);
		
		
		if( *p >= 'A' ){
			if( ( *p >= 'a' ) && ( *p <='z' ) ){
				// set upper case letter
				*p -= ( 'a' - 'A' ); 
			}
			else if( *p > 'Z' ){
				// some char between 'Z' and 'a' --> invalid character
				ret = -3;
			}
		}
		
		
		switch(*(++p))
		{
			case 'D':
			break;
			default:
				// invalid parameter
				ret = -2;
			break;
		}
		ret = 0;
	}
	
	
	return(ret);
}




int normalize_data( uint32_t length, int16_t *data_in, uint16_t *data_out_maximum )
{
	int16_t maximum = 0;
	*data_out_maximum = 0;
	
	for( uint16_t ix=0; ix < length; ix++){
		if( abs(data_in[ix]) > maximum ){
			maximum = abs(data_in[ix]);
			*data_out_maximum = ix;
		}
	}
	
	
	return( ( maximum == 0)?(-1):(0) );
}



/*
* @brief	This function measures the distance to an object
*
* @details	The function searches the point where the echo 
* 			is above 200 mV.
*
*
*/
int measure_distance( uint16_t *data_in, double *data_out )
{
	/*
	if( *data_in > 430 ){
		*data_in -= 430;
	}
	else{
		*data_in  = 0;
	}
	*/
	
	// calculate the distance of the measured object
	// and write back to data_out pointer
	*data_out = (double)( distance_faktor * *data_in) +  (double)( MIN_DISTANCE_CM ) / 100 ;
	
	return(ERR_NO);
	
}



/*
* @brief	Function for executing FFT
* 
* @details	The funntion needs values suitable for int32_t
*
* @param	length: the amount of data, which will be analized
* @param	*data_in: values between 2^0...2^31
* @param	*data_out: pointer to array of the length (length/2+1)
*
*/
int calc_fft( uint32_t length, int16_t *data_in, float *data_out )
{
	// create an output buffer for FFT
	// create a config variable for FFT
	kiss_fft_cpx *fft_out = (kiss_fft_cpx *)calloc( (length/2+1) , sizeof(kiss_fft_cpx));
	kiss_fftr_cfg	fft_cfg;
	double re, im;
	
	
	// allocate temporary space for calculation
	fft_cfg = kiss_fftr_alloc(length,0,NULL,NULL);
	
	
	// execute the FFT
	kiss_fftr( fft_cfg, (kiss_fft_scalar *) data_in, fft_out );
	
	
	// build the sum of imaginary and real part of the FFT
	for( uint16_t ix = 0; ix < (length/2+0); ix++ ){
		re = fft_out[ix].r, im = fft_out[ix].i;
		data_out[ix] = sqrtf( re*re + im*im );
	}
	
	free( fft_out );
	return(ERR_NO);
}



/*
* @brief	This function is used to save data into RAM of RedPitaya
*
* @details	The function creates a new file with an index n and
*			stores the input *data_in into that file
*
* @return	0: success; -1: not defined; -2: file creation error; -3: file open error
*
*/
/*int save_fft( uint32_t length, float *data_in, uint8_t number )
{
	FILE *file = NULL;
	FILE *dist = NULL;
	char file_name[MAX_CHAR];
	uint32_t ix_min, ix_max;
	double freq_fac; // = (double)(( ADC_SAMPLE_FREQUENCY / ADC_SAMPLE_DECIMATION ) / (length*2));
	double max = 0;
	
	
	freq_fac = (double)(( ADC_SAMPLE_FREQUENCY / ADC_SAMPLE_DECIMATION ) / length ) / 2;
	
	if( create_file( FILE_NAME_FFT, file_name, number ) != 0 ){
		return(ERR_FILE_CREATION);
	}
	
	if( (file = fopen(file_name,"w+")) < 0 ){
		return(ERR_FILE_OPEN);
	}
	
	// Information for existing data
	fprintf(file, "ID\tFrequenz\tAmplitude\n");
	
	ix_max = ( MAX_FFT_FREQ / freq_fac) + 1;
	ix_min = ( MIN_FFT_FREQ / freq_fac) - 1;
	
	
	for( uint32_t ix=ix_min; ix <= ix_max; ix++ ){
		if( data_in[ix] > max ){
			max = data_in[ix];
		}
	}
	
	if( max == 0 ){
		fclose(file);
		return(ERR_LOGIC);
	}
	
	// write every line into file
	for( uint32_t ix=ix_min; ix <= ix_max; ix++ ){
		fprintf( file, "%d\t%i\t%i\n", number, (int32_t)(ix * freq_fac), (uint32_t)(data_in[ix]/max * 1000) );
	}
	
	fclose(file);
	
	return(ERR_NO);

}
*/
int save_data( uint32_t length, int16_t *data_in, uint8_t number )
{
	int16_t maximum = 0;
	FILE *file = NULL;
	FILE *dist = NULL;
	
	
	if( (file = fopen("data.txt","a+")) < 0 ){
		return(ERR_FILE_OPEN);
	}
	if( (dist = fopen("distance.txt","a+")) < 0 ){
		return(ERR_FILE_OPEN);
	}
	if ( number == 1 )
	{
		fprintf(file,"id\tsample\tadc\n");
		fprintf(dist,"id\tdistance\n");
		for (int i = 0;i<5;i++){
			fprintf(dist,"%d\t%f\n", number+i, distance_mean);
		}	
	}
   	
		
	// Information for existing e,data
	for( uint16_t ix=0; ix < length; ix++ ){
		if( data_in[ix] > maximum ){
			maximum = data_in[ix];
		}
	}
	
	// write every line into file
	for( uint16_t ix; ix < length; ix++ ){
		fprintf( file, "%d\t%d\t%i\n",number, ix, (int32_t)(data_in[ix]));
	}
	fclose(file);
	fclose(dist);
	return(ERR_NO);
}



/*
* @brief	This function is used to create a name for a new file
*
* @details	The function looks up for 
*
*
*/
int create_file( const char* file_name, char* file_target, uint8_t number )
{
	char *p = NULL;
	memcpy( (void *)file_target, (const void *)file_name, strlen( file_name ));
	p = file_target + strlen(file_name);// + strlen( file_target ) + 0;
	*p = '\0';
	p = memchr( file_target, '-', strlen( file_name ) );
	
	
	
	if( p != NULL ){
		for( uint8_t i = 100; i>=1; i/=10 )
		{
			
			*p	= '0' + (char)( number / i );
			number = number - ( number / i ) * i;
			
			p++;
		}
		return(0);
	}
	
	
	return(-1);
	
}



