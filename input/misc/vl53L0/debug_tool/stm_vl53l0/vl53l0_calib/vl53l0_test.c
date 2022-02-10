

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>
#include "vl53l0_def.h"

#define MODE_RANGE		0
#define MODE_XTAKCALIB	1
#define MODE_OFFCALIB	2
#define MODE_HELP		3
#define MODE_PARAMETER  6


//******************************** IOCTL definitions
#define VL53L0_IOCTL_INIT			_IO('p', 0x01)
#define VL53L0_IOCTL_XTALKCALB		_IOW('p', 0x02, unsigned int)
#define VL53L0_IOCTL_OFFCALB		_IOW('p', 0x03, unsigned int)
#define VL53L0_IOCTL_STOP			_IO('p', 0x05)
#define VL53L0_IOCTL_SETXTALK		_IOW('p', 0x06, unsigned int)
#define VL53L0_IOCTL_SETOFFSET		_IOW('p', 0x07, int8_t)
#define VL53L0_IOCTL_GETDATAS		_IOR('p', 0x0b, VL53L0_RangingMeasurementData_t)
#define VL53L0_IOCTL_PARAMETER		_IOWR('p', 0x0d, struct stmvl53l0_parameter)

#define VL53L0_IOCTL_ACTIVATE_USE_CASE  		_IOW('p', 0x08, uint8_t)
#define VL53L0_IOCTL_ACTIVATE_CUSTOM_USE_CASE			_IOW('p', 0x09, struct stmvl53l0_custom_use_case)



#define USE_CASE_LONG_DISTANCE   1
#define USE_CASE_HIGH_ACCURACY   2
#define USE_CASE_HIGH_SPEED 	 3
#define USE_CASE_CUSTOM 		 4

#define LONG_DISTANCE_TIMING_BUDGET  			26000
#define LONG_DISTANCE_SIGNAL_RATE_LIMIT 		(65536 / 10) /* 0.1 */
#define LONG_DISTANCE_SIGMA_LIMIT  				(60*65536)
#define LONG_DISTANCE_PRE_RANGE_PULSE_PERIOD 	18
#define LONG_DISTANCE_FINAL_RANGE_PULSE_PERIOD 	14



#define HIGH_ACCURACY_TIMING_BUDGET  			200000
#define HIGH_ACCURACY_SIGNAL_RATE_LIMIT 		(25 * 65536 / 100) /* 0.25 * 65536 */
#define HIGH_ACCURACY_SIGMA_LIMIT  				(18*65536)
#define HIGH_ACCURACY_PRE_RANGE_PULSE_PERIOD 	14
#define HIGH_ACCURACY_FINAL_RANGE_PULSE_PERIOD 	10



#define HIGH_SPEED_TIMING_BUDGET  				20000
#define HIGH_SPEED_SIGNAL_RATE_LIMIT 			(25 * 65536 / 100)
#define HIGH_SPEED_SIGMA_LIMIT  				(32*65536)
#define HIGH_SPEED_PRE_RANGE_PULSE_PERIOD 		14
#define HIGH_SPEED_FINAL_RANGE_PULSE_PERIOD 	10

//modify the following macro accoring to testing set up
#define OFFSET_TARGET		100//200
#define XTALK_TARGET		600//400
#define NUM_SAMPLES			20//20
#define OFFSET_VALUE 	   15000 //Change this value based on testing
#define XTALK_COMPENSATION_VALUE 	   8 //Change this value based on testing
//#define ACTIVATE_OFFSET_AND_XTALKCOMP


typedef enum {
	OFFSET_PAR = 0,
	XTALKRATE_PAR = 1,
	XTALKENABLE_PAR = 2,
	GPIOFUNC_PAR = 3,
	LOWTHRESH_PAR = 4,
	HIGHTHRESH_PAR = 5,
	DEVICEMODE_PAR = 6,
	INTERMEASUREMENT_PAR = 7,
	REFERENCESPADS_PAR = 8,
	REFCALIBRATION_PAR = 9,
} parameter_name_e;
/*
 *  IOCTL parameter structs
 */
struct stmvl53l0_parameter {
	uint32_t is_read; //1: Get 0: Set
	parameter_name_e name;
	int32_t value;
	int32_t value2;
	int32_t status;
};

struct stmvl53l0_custom_use_case {
	FixPoint1616_t 	signalRateLimit;
	FixPoint1616_t 	sigmaLimit;
	uint32_t		preRangePulsePeriod;
	uint32_t		finalRangePulsePeriod;
	uint32_t		timingBudget;
};


void *thread_fn(void *data);

static void help(void)
{
	fprintf(stderr,
		"Usage: vl53l0_test [-c] [-h]\n"
		" -h for usage\n"
		" -c for crosstalk calibration\n"
		" -o for offset calibration\n"
		" -u for testing dynamic use case change\n"
		" -O <low> <high> for testing interrupt threshold (out mode)\n"
		" -L <low> for testing interrupt threshold (low mode)\n"
		" -H <high> for testing interrupt threshold (high mode)\n"
		" default for ranging\n"
		);
	exit(1);
}

static int loop_break=0;

void sig_handler(int signum)
{
	if(signum == SIGINT)
	{
		loop_break=1;
	}
}


int main(int argc, char *argv[])
{
	int fd;
	unsigned long data;
	VL53L0_RangingMeasurementData_t range_datas;
	struct stmvl53l0_parameter parameter;
	int flags = 0;
	int mode = MODE_RANGE;
	unsigned int targetDistance=0;
	int i = 0;
	int loop_count = 0;
	pthread_t thread_id;
	int rc;
	int test_use_case_change = 0;

	unsigned int low_threshold = 0, high_threshold = 0;
	int configure_int_thresholds = 0;
	int gpio_functionnality_threshold = 0;


	if (signal(SIGINT, sig_handler) == SIG_ERR)
		fprintf(stderr, "Can't catch SIGINT");


	/* handle (optional) flags first */
	while (1+flags < argc && argv[1+flags][0] == '-') {
		switch (argv[1+flags][1]) {
		case 'c': mode= MODE_XTAKCALIB; break;
		case 'h': mode= MODE_HELP; break;
		case 'o': mode = MODE_OFFCALIB; break;
		case 'u': test_use_case_change = 1; break;
		case 'O':
				if (argc < 4) {
					fprintf(stderr, "USAGE: %s <low_threshold_value> <high_threshold_value>\n", argv[0]);
					exit(1);
				} 
				low_threshold = atoi(argv[2]);
				high_threshold = atoi(argv[3]);
				configure_int_thresholds = 1;

				fprintf(stderr, "Configuring Low threshold = %u, High threhold = %u\n", low_threshold, high_threshold);
				break;
		case 'L':
				if (argc < 3) {
					fprintf(stderr, "USAGE: %s <low_threshold_value> \n", argv[0]);
					exit(1);
				} 
				low_threshold = atoi(argv[2]);
				configure_int_thresholds = 2;

				fprintf(stderr, "Configuring Low threshold = %u, High threhold = %u\n", low_threshold, high_threshold);
				break;
		case 'H':
				if (argc < 3) {
					fprintf(stderr, "USAGE: %s <high_threshold_value>\n", argv[0]);
					exit(1);
				} 
				high_threshold = atoi(argv[2]);
				configure_int_thresholds = 3;

				fprintf(stderr, "Configuring Low threshold = %u, High threhold = %u\n", low_threshold, high_threshold);
				break;

		default:
			fprintf(stderr, "Error: Unsupported option "
				"\"%s\"!\n", argv[1+flags]);
			help();
			exit(1);
		}
		flags++;
	}
	if (mode == MODE_HELP)
	{
		help();
		exit(0);
	}

	fd = open("/dev/stmvl53l0_ranging",O_RDWR | O_SYNC);
	if (fd <= 0)
	{
		fprintf(stderr,"Error open stmvl53l0_ranging device: %s\n", strerror(errno));
		return -1;
	}
	//make sure it's not started
	if (ioctl(fd, VL53L0_IOCTL_STOP , NULL) < 0) {
		fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_STOP : %s\n", strerror(errno));
		close(fd);
		return -1;
	}	
	if (mode == MODE_XTAKCALIB)
	{
		unsigned int XtalkInt = 0;
		uint8_t XtalkEnable = 0;
		fprintf(stderr, "xtalk Calibrate place black target at %dmm from glass===\n",XTALK_TARGET);
		// to xtalk calibration 
		targetDistance = XTALK_TARGET;
		if (ioctl(fd, VL53L0_IOCTL_XTALKCALB , &targetDistance) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_XTALKCALB : %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		// to get xtalk parameter
		parameter.is_read = 1;
		parameter.name = XTALKRATE_PAR;
		if (ioctl(fd, VL53L0_IOCTL_PARAMETER , &parameter) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		XtalkInt = (unsigned int)parameter.value;
		parameter.name = XTALKENABLE_PAR;
		if (ioctl(fd, VL53L0_IOCTL_PARAMETER , &parameter) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		XtalkEnable = (uint8_t)parameter.value;
		fprintf(stderr, "VL53L0 Xtalk Calibration get Xtalk Compensation rate in fixed 16 point as %u, enable:%u\n",XtalkInt,XtalkEnable);

		FILE *fp_xtak = NULL;
		fp_xtak = fopen("/mnt/vendor/vl53l0_xtak_calibration.file","wb");
		if(NULL!=fp_xtak){
			fprintf(fp_xtak, "%d\n", XtalkInt);
			fprintf(fp_xtak, "%d\n", XtalkEnable);
			fclose(fp_xtak);
		}
		
		for(i = 0; i <= NUM_SAMPLES; i++)
		{
			usleep(30 *1000); /*100ms*/
					// to get all range data
			ioctl(fd, VL53L0_IOCTL_GETDATAS,&range_datas);	
		}
		fprintf(stderr," VL53L0 DMAX calibration Range Data:%d,  signalRate_mcps:%d\n",range_datas.RangeMilliMeter, range_datas.SignalRateRtnMegaCps);
		// get rangedata of last measurement to avoid incorrect datum from unempty buffer 
		//to close
		close(fd);
		return -1;
	}
	else if (mode == MODE_OFFCALIB)
	{
		int offset=0;
		uint32_t SpadCount=0;
		uint8_t IsApertureSpads=0;
		uint8_t VhvSettings=0,PhaseCal=0;


		// to xtalk calibration 
		targetDistance = OFFSET_TARGET;
		if (ioctl(fd, VL53L0_IOCTL_OFFCALB , &targetDistance) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_OFFCALB : %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		// to get current offset
		parameter.is_read = 1;
		parameter.name = OFFSET_PAR;
		if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		offset = (int)parameter.value;
		fprintf(stderr, "get offset %d micrometer===\n",offset);
		
		parameter.name = REFCALIBRATION_PAR;
		if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		VhvSettings = (uint8_t) parameter.value;
		PhaseCal=(uint8_t) parameter.value2;
		fprintf(stderr, "get VhvSettings is %u ===\nget PhaseCas is %u ===\n", VhvSettings,PhaseCal);
		
		parameter.name =REFERENCESPADS_PAR;
		if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		SpadCount = (uint32_t)parameter.value;
		IsApertureSpads=(uint8_t) parameter.value2;
		fprintf(stderr, "get SpadCount is %d ===\nget IsApertureSpads is %u ===\n", SpadCount,IsApertureSpads);

		FILE *fp_offset = NULL;
		fp_offset = fopen("/mnt/vendor/vl53l0_offset_calibration.file", "wb");
		if(NULL!=fp_offset){
			fprintf(fp_offset, "%d\n", offset);
			fprintf(fp_offset, "%d\n", VhvSettings);
			fprintf(fp_offset, "%d\n", PhaseCal);
			fprintf(fp_offset, "%d\n", SpadCount);
			fprintf(fp_offset, "%d\n", IsApertureSpads);
			fclose(fp_offset);
		}

		//to close
		close(fd);
		return -1;
	}	
	else
	{
		if (test_use_case_change) {
			//Create a pthread to test dynamic changing of use cases
			rc = pthread_create(&thread_id, NULL, thread_fn, &fd);
			printf("Thread create returned = %d\n", rc);
		}
		
		switch(configure_int_thresholds)
		{
		case 1:
			gpio_functionnality_threshold = VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_OUT;
			break;
		case 2:
			gpio_functionnality_threshold = VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_LOW;
			break;
		case 3:
			gpio_functionnality_threshold = VL53L0_GPIOFUNCTIONALITY_THRESHOLD_CROSSED_HIGH;
			break;
		default:
			gpio_functionnality_threshold = VL53L0_GPIOFUNCTIONALITY_NEW_MEASURE_READY;
		}

		if (configure_int_thresholds) {

			parameter.is_read = 0;
	

			parameter.name = DEVICEMODE_PAR;
			parameter.value = VL53L0_DEVICEMODE_CONTINUOUS_TIMED_RANGING;
			//parameter.value = VL53L0_DEVICEMODE_CONTINUOUS_RANGING;
			if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
				fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER(CONTINOUS_TIMED_RANGING) : %s\n", 
															strerror(errno));
				close(fd);
				return -1;
			}
	
			parameter.name = GPIOFUNC_PAR;
			parameter.value = gpio_functionnality_threshold;
			if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
				fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s, low_threshold = %u\n", 
															strerror(errno),
															low_threshold);
				close(fd);
				return -1;
			}
		
			if (configure_int_thresholds != 3)
			{
				parameter.name = LOWTHRESH_PAR;
				parameter.value = low_threshold;
				if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
					fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s, low_threshold = %u\n", 
																strerror(errno),
																low_threshold);
					close(fd);
					return -1;
				}
			}
			
			if (configure_int_thresholds != 2)
			{
				parameter.name = HIGHTHRESH_PAR;
				parameter.value = high_threshold;
				if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
					fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s, high_threshold = %u\n", 
																strerror(errno),
																high_threshold);
					close(fd);
					return -1;
				}
			}
	
		}
		else {
			parameter.name = DEVICEMODE_PAR;
			parameter.value = VL53L0_DEVICEMODE_CONTINUOUS_RANGING;

			if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
				fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER(CONTINUOUS_RANGING) : %s\n", 
															strerror(errno));
				close(fd);
				return -1;
			}
			parameter.name = GPIOFUNC_PAR;
			parameter.value = gpio_functionnality_threshold;
			if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
				fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s, low_threshold = %u\n", 
															strerror(errno),
															low_threshold);
				close(fd);
				return -1;
			}
		
		}
		
#ifdef ACTIVATE_OFFSET_AND_XTALKCOMP //Define this set calibration values
		parameter.is_read = 0;
		parameter.name  = OFFSET_PAR;
		parameter.value = OFFSET_VALUE; //Change this value based on coverglass
		printf("Set Offset value = %u\n", parameter.value);
		if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
				fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s\n", strerror(errno));
				close(fd);
				return -1;
		}
		
		parameter.is_read = 0;
		parameter.name = XTALKENABLE_PAR;
		parameter.value = 1;
		if (ioctl(fd, VL53L0_IOCTL_PARAMETER , &parameter) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s\n", strerror(errno));
			close(fd);
			return -1;
		}

		parameter.is_read = 0;
		parameter.name  = XTALKRATE_PAR;
		parameter.value = XTALK_COMPENSATION_VALUE;//Change this value based on coverglass
		printf("Set Xtalk value = %u\n", parameter.value);

		if (ioctl(fd, VL53L0_IOCTL_PARAMETER, &parameter) < 0) {
				fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_PARAMETER : %s\n", strerror(errno));
				close(fd);
				return -1;
		}
#endif
		// to init 
		if (ioctl(fd, VL53L0_IOCTL_INIT , NULL) < 0) {
			fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_INIT : %s\n", strerror(errno));
			close(fd);
			return -1;
		}
	}
	// get data testing
	while (1)
	{
		usleep(30 *1000); /*100ms*/
		// to get all range data
		ioctl(fd, VL53L0_IOCTL_GETDATAS,&range_datas);

		fprintf(stderr,"  VL53L0 Range Data:%4d, error status:0x%x, signalRate_mcps:%7d, Amb Rate_mcps:%7d\r",
				range_datas.RangeMilliMeter, range_datas.RangeStatus, range_datas.SignalRateRtnMegaCps, range_datas.AmbientRateRtnMegaCps);
		loop_count++;

		if(loop_break)
			break;
	}

	fprintf(stderr, "\n");

	fprintf(stderr, "Stop driver\n");

	if (ioctl(fd, VL53L0_IOCTL_STOP , NULL) < 0) {
		fprintf(stderr, "Error: Could not perform VL53L0_IOCTL_STOP : %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

void *thread_fn(void *data) 
{
	int fd = (int) data;
	uint8_t test_use_case = 2;
	struct stmvl53l0_custom_use_case customUseCase;

	printf("\n\nTest Use Case Change Thread starting\n");
	while (1) {
		sleep(20);
		printf("Thread setting test case = %u\n\n\n", test_use_case);
		if (test_use_case == 2) {

			ioctl(fd, VL53L0_IOCTL_ACTIVATE_USE_CASE, &test_use_case);
			test_use_case = 3;

		} else if (test_use_case == 3) {

			ioctl(fd, VL53L0_IOCTL_ACTIVATE_USE_CASE, &test_use_case);
			test_use_case = 4;

		} else if (test_use_case == 4) {

			customUseCase.signalRateLimit 		= HIGH_SPEED_SIGNAL_RATE_LIMIT; 
			customUseCase.sigmaLimit			= HIGH_SPEED_SIGMA_LIMIT;
			customUseCase.preRangePulsePeriod  	= HIGH_SPEED_PRE_RANGE_PULSE_PERIOD;
			customUseCase.finalRangePulsePeriod = HIGH_SPEED_FINAL_RANGE_PULSE_PERIOD;
			customUseCase.timingBudget  		= HIGH_SPEED_TIMING_BUDGET;		
			ioctl(fd, VL53L0_IOCTL_ACTIVATE_CUSTOM_USE_CASE, &customUseCase);
			test_use_case = 2;
		}
	}
	return NULL;

}
