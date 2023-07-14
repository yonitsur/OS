#ifndef MESSAGE_SLOT_H
#define MESSAGE_SLOT_H

#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, int)
#define MAJOR_NUM 235
#define DEVICE_NAME "msg_slot"
#define BUF_LEN 128
#define SUCCESS 0
#define MAX_FILES 256


#endif


// gcc -O3 -Wall -std=c11 message_sender.c -o message_sender
// gcc -O3 -Wall -std=c11 message_reader.c -o message_reader
