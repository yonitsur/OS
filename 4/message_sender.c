#include "message_slot.h"    
#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

int main(int argc, char *argv[]){

	if (argc!=4){
		fprintf (stderr, "%ss\n", strerror(EINVAL));
    	exit(1);
	} 
	
	char* device_file_name = argv[1];
	unsigned int channel_id = atoi(argv[2]);
	char *msg = argv[3];
	int file_desc;
	
  	if((file_desc = open( device_file_name, O_WRONLY )) < 0 ) { // Open the specified message slot device file
    	fprintf (stderr, "ERROR while open device file: %s (%s)\n", device_file_name, strerror(errno));
    	exit(1);
  	}

  	if(ioctl(file_desc, MSG_SLOT_CHANNEL, channel_id)<0 ){ // Set the channel id to the specified id  
  		fprintf (stderr, "ERROR while open channel: %d (%s)\n", channel_id, strerror(errno));
    	exit(1);
  	}
  	
  	if(write(file_desc, msg, strlen(msg))!= strlen(msg)){ // Write the specified message to the message slot file
  		fprintf (stderr, "ERROR while writing message: %s (%s)\n", msg, strerror(errno));
    	exit(1);
  	}
  	 
  	close(file_desc); 
  	
  	return SUCCESS;
}
