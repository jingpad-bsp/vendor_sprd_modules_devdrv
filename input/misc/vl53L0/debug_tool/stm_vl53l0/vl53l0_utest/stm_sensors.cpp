/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <cutils/log.h>

#include <hardware/sensors.h>
#include <utils/Timers.h>
#include <stdlib.h>

struct sensors_module_t* module;
sensors_poll_device_1_t *device;
struct sensor_t const* list;
int count;
uint32_t prev_usec,  cur_usec;

void *flush_thread_fn(void *data);


void handle_signal(int signal) 
{
    int err;
	printf("Signal Handler!\n");
	if (signal == SIGINT) {
		printf("Signal is SIGINT\n");
		for (int i = 0; i < count; i++) {
			if (list[i].type == 40) {		
				err = device->activate(&device->v0, list[i].handle, 0);
				if (err != 0) {
					printf("deactivate() for '%s'failed (%s)\n",
											list[i].name, strerror(-err));
					return;
				}
			}
		}
		device->common.close((hw_device_t *) &device->common);
		//dlclose(sensor_lib);
	}
	exit(0);
}




static uint32_t get_time_diff_msec(void)
{
	if (cur_usec < prev_usec) {
		return ((1000000 - prev_usec) + cur_usec) / 1000;
	} else {
		return (cur_usec - prev_usec) / 1000;
	}
	
}

int main(int argc, char** argv)
{
    int err;
    int i ,rc;
	void *sensor_lib = NULL;
	int handle_index=0;
	int disable_count =1;
	pthread_t thread_id;
	int test_flush = 0;
	int delay_ms = 5;

	if (argc > 1) { //quick test to force flush
		printf("Test Flush functionality\n");
		test_flush = 1;
	}
	sensor_lib = dlopen("hw/sensors.hal.tof.so", RTLD_LAZY);
	if (NULL == sensor_lib)
	{
		printf("Failing to open sensor library! Error: %s\n", dlerror());
		return 0;
	}
	dlerror();
	module = (struct sensors_module_t*)dlsym(sensor_lib, HAL_MODULE_INFO_SYM_AS_STR);
	if (NULL != dlerror())
	{
		printf("Failing to get sensor module handle!");
		dlclose(sensor_lib);
		return 0;
	}
	err = module->common.methods->open(&module->common, SENSORS_HARDWARE_POLL, (hw_device_t **) &device);
	if (err !=0)
	{
		printf("Failing to open sensor module! Error:%s!\n", strerror(-err));
		//dlcolose(sensor_lib);
		return 0;
	}


    count = module->get_sensors_list(module, &list);
    printf("V-1.1.20 : %d sensors found:\n", count);
    for (i = 0; i < count; i++) {
        printf("%s\n"
                "\tvendor: %s\n"
                "\tversion: %d\n"
                "\thandle: %d\n"
                "\ttype: %d\n"
                "\tmaxRange: %f\n"
                "\tresolution: %f\n"
                "\tpower: %f mA\n",
                list[i].name,
                list[i].vendor,
                list[i].version,
                list[i].handle,
                list[i].type,
                list[i].maxRange,
                list[i].resolution,
                list[i].power);
    }

    static const size_t numEvents = 16; //8;
    sensors_event_t events[numEvents];

    /* For debug, limit sensors */
//    count = 4;

    for (i = 0; i < count; i++) {
		if (list[i].type ==  40 ) { //SENSOR_TYPE_TIME_OF_FLIGHT
        	err = device->activate(&device->v0, list[i].handle, 0);
			printf("Deactivated sensor %s\n", list[i].name);
			handle_index = i;
        	if (err != 0) {
            	printf("deactivate() for '%s'failed (%s)\n",
            	        list[i].name, strerror(-err));
            	return 0;
        	}
		}
    }

    for (int i = 0; i < count; i++) {
		if (list[i].type == 40 ) { 
			printf("Activiating sensor %s\n",  list[i].name);
       	 	err = device->activate(&device->v0, list[i].handle, 1);
        	if (err != 0) {
        	    printf("activate() for '%s'failed (%s)\n",
        	            list[i].name, strerror(-err));
        	    return 0;
        	}
			printf("Change the rate via Batch\n");
			err = device->batch(device, list[i].handle,0,(delay_ms*1000000),0); //30ms
			printf("Set Delay_ms = %d\n", delay_ms);
      		if (err != 0) {
        	    printf("batch() for '%s'failed (%s)\n",
        	            list[i].name, strerror(-err));
        	    return 0;
        	}
		}
    }

	if (signal(SIGINT, handle_signal) == SIG_ERR) {
		printf("Unable to register signal_handler for SIGINT\n");
	}

	if (test_flush) {
		rc = pthread_create(&thread_id, NULL, flush_thread_fn, NULL);
		printf("Thread create returned = %d\n", rc);
	}
	
    printf("Starting to poll\n"); 
    do {
		disable_count++;
        int n = device->poll(&device->v0, events, numEvents);
		printf("\nSensor Poll returned %d events\n", n);
        if (n < 0) {
            printf("poll() failed (%s)\n", strerror(-err));
            break;
        }

	
       	printf("read %d \n", n);
        for (int i = 0; i < n; i++) {
            sensors_event_t *event = &events[i];
			printf("i:%d, event type as:%d, count = %d\n",i,event->type, n);
			if (event->type == SENSOR_TYPE_PROXIMITY)
				printf("i: %d distance: %f cm\n",i, event->data[0]);
			else if (event->type == SENSOR_TYPE_LIGHT)
				printf("i: %d lux: %f Lux++++++++++++++++++++\n",i, event->data[0]);
			else if (event->type == SENSOR_TYPE_META_DATA && 
			    event->meta_data.what == META_DATA_FLUSH_COMPLETE) {
				printf("Sensors : Flush Complete event recieved\n");
			} 
			else if (event->type == 40)
			{
				//dump event data
#ifdef ORIGINAL_HAL
				printf("i: %d distance: %f cm\n",i, event->data[0]);
				printf("tv_sec: %f	", event->data[1]);
				printf("tv_usec: %f	", event->data[2]);
				printf("distance: %f mm	", event->data[3]);
				printf("error code: %f	\n", event->data[4]);
				printf("signalRate_mcps(9.7 format): %f \n", event->data[5]);
				printf("rtnAmbRate (kcps): %f	", event->data[6]);
				printf("rtnConvTIme: %f	", event->data[7]);
				printf("DMAX: %f\n", event->data[8]);
#else
				printf("\ni: %d\n",i);
				printf("tv_sec: %f	", event->data[0]);
				printf("tv_usec: %f	", event->data[1]);
				printf("distance: %f mm	\n", event->data[2]);
				cur_usec = event->data[1];
				/* The seconds value is getting truncated during conversion to float. 
				 * Ignore it for now 
				 */
				printf("Rate(msec) : %u\n", get_time_diff_msec());
				prev_usec = cur_usec;
							
				printf("Confidence: %f	\n", event->data[3]);
				printf("Near Range: %f \n", event->data[4]);
				printf("Far Range: %f	\n", event->data[5]);
				printf("signalRate : %f \n", (event->data[6])/ 65536);
				printf("rtnAmbRate : %f	\n", (event->data[7])/65536);
				printf("rtnConvTIme: %f	\n", event->data[8]);
				printf("Sigma      : %f	\n", event->data[9]/65536);
				printf("Spad Count : %d \n", (int)event->data[10]/256);
				printf("ErrorCode  : %f	\n", event->data[11]);
					
#endif
			} 
			else 
			{
				printf("Unknown Event type = %d\n", event->type);
			}
	
        }
#if 0 //only enable for disable/enable stress testing and verify flush
		//to disable
		if (disable_count %20 ==0)
		{
			err = device->activate(&device->v0, list[handle_index].handle, 0);
			printf("Deactivated sensor %s\n", list[handle_index].name);
       		if (err != 0) {
         	  	printf("deactivate() for '%s'failed (%s)\n",
      	     	        list[handle_index].name, strerror(-err));
       	    	return 0;
       		}
			printf("Sensors : Try Flush after Deactivate\n");
			device->flush(device, list[0].handle);

			//to enable
			err = device->activate(&device->v0, list[handle_index].handle, 1);
			printf("Activated sensor %s\n", list[handle_index].name);
    	   	if (err != 0) {
    	       	printf("activate() for '%s'failed (%s)\n",
    	       	        list[handle_index].name, strerror(-err));
    	       	return 0;
    	   	}
			disable_count = 1;
		}
#endif
    } while (1);


    for (int i = 0; i < count; i++) {
		if (list[i].type == 40) {		
	        err = device->activate(&device->v0, list[i].handle, 0);
    	    if (err != 0) {
    	        printf("deactivate() for '%s'failed (%s)\n",
    	                list[i].name, strerror(-err));
    	        return 0;
    	    }
		}
    }
	device->common.close((hw_device_t *) &device->common);
	//dlclose(sensor_lib);

    return 0;
}


void *flush_thread_fn(void *data) 
{
	int loop_count = 0;
	while (loop_count++ < 3) {
		sleep(4);
		printf("Sensor : Flush \n");
		sleep(1);
		device->flush(device, list[0].handle);
	}
	return NULL;
}

