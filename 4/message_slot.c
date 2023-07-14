#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>   
#include <linux/module.h>   
#include <linux/fs.h>       
#include <linux/uaccess.h>  
#include <linux/string.h>   
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include "message_slot.h"

MODULE_LICENSE("GPL");

/*================== DATA STRUCTURES ===========================*/

/* file's info struct (ifile):
	the device keep information about each file in this struct.
 */
typedef struct ifile{
	unsigned long minor;
	unsigned long channel_id;
}ifile;

/* channel's info & data struct (ichannel):
	This struct represent each channel as node in a linked list.
	for each channel node we keep it's information and data, 
	as well as a pointer to the next element(channel node) in the channels list. 
 */
typedef struct ichannel{
	unsigned long channel_id;
	unsigned int msg_len;
	char msg[BUF_LEN];
	struct ichannel *next;
}ichannel;

/* file's channels list struct (channels_lst):
	Each message slot file can keep up to 2^20 message channels,
	in a linked list of ichannel nodes.
 */
typedef struct channels_lst{
	ichannel *head;
}channels_lst;

/* device's files struct (devFiles):
	The device can keep up to 256 different message slots files
	in an array of 256 channels list (channels_lst). 
	(i.e, devFiles is an array of linked lists, where entry i in the array matches the file with minor number i).
*/
static channels_lst* devFiles[MAX_FILES];

/* used to know if a file was created on the device or not. */ 
static int file_created_flag = 0;

/*================== DEVICE FUNCTIONS ===========================*/

static int device_open( struct inode* inode,
                        struct file*  file )
{
  ifile* file_info;
  ichannel* channel;
  int minor = iminor(inode), i;
    
  // if file wasn't created - create an empty data structure for the file descriptor. 
  if(0 == file_created_flag) { 
    for(i=0; i < MAX_FILES ; i++){
    	if ((devFiles[i] = kmalloc(sizeof(channels_lst), GFP_KERNEL)) == NULL) 
    		return -ENOMEM;
    	devFiles[i]->head = NULL;
    }
    ++file_created_flag; // file created on the device - turn on flag 
  }
    
  //set file descriptor's info struct 
  if ((file_info = kmalloc(sizeof(ifile), GFP_KERNEL)) == NULL) 
  	return -ENOMEM;
  	
  file_info->minor = minor;
  file_info->channel_id = 0;
  file->private_data = (void*)file_info;
  
  // set file descriptor's channel struct 
  if ((channel = kmalloc(sizeof(ichannel), GFP_KERNEL))== NULL)
  	return -ENOMEM;
  	
  channel->msg_len = 0;
  channel->channel_id = 0;
  channel->next = devFiles[minor]->head; 
 
  devFiles[minor]->head = channel; //set the channel as head of the file's channels-list
    
  return SUCCESS;
}

//---------------------------------------------------------------
static int device_release( struct inode* inode,
                           struct file*  file)
{
  kfree(file->private_data);
  return SUCCESS;
}

//---------------------------------------------------------------

// Reads the last message written on the channel into the user’s buffer
static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            size_t       length,
                            loff_t*      offset )
{
  int minor = ((ifile*)(file->private_data))->minor; // get file's minor number 
  unsigned long channel_id=((ifile*)(file->private_data))->channel_id; // get channel id 
  ichannel* channel = devFiles[minor]->head; // get the head of channels-list
  if(channel_id == 0  || channel == NULL) 
  	return -EINVAL; // no channel has been set on the file descriptor / invalid id 
 
 // go through file's channels-list and search for the channel with the specified id 
  while(channel != NULL && channel->channel_id!=channel_id )
  	channel = channel->next;
  
  if(channel == NULL || channel->msg_len == 0  || channel->msg == NULL ) 
  	return -EWOULDBLOCK; //no message exists on the channel / channel id wasn't found
  	
  if(length < channel->msg_len) 
  	return -ENOSPC; // buffer length is too small to hold the last message written on the channel
 
  if (buffer == NULL || copy_to_user(buffer, channel->msg, channel->msg_len)!=0) // read message - copy data from the channel to the buffer
  	return -EFAULT;	  
  
  return channel->msg_len; // Return the number of bytes read
}

//---------------------------------------------------------------

// Writes a non-empty message of up to 128 bytes from the user’s buffer to the channel
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length,
                             loff_t*            offset)
{
  int minor = ((ifile*)(file->private_data))->minor;
  unsigned long channel_id=((ifile*)(file->private_data))->channel_id;
  ichannel* channel = devFiles[minor]->head; 
  ichannel* next_channel;
  
  if(channel_id == 0 || channel == NULL ) 
  	return -EINVAL; //no channel has been set on the file descriptor / invalid id
  
  // go through file's channels-list and search for the channel with the specified id 
  while(channel->next != NULL && channel->channel_id != channel_id )
  	channel = channel->next;
  	
  // channel id wasn't found - create a new channel and add it to the end of the file's channels-list 
  if (channel->channel_id!=channel_id){ 
  
  	if((next_channel = kmalloc(sizeof(ichannel), GFP_KERNEL)) == NULL) 
  		return -ENOMEM;
  	
  	next_channel->msg_len = 0;
  	next_channel->channel_id = channel_id;
  	next_channel->next = NULL ;
  	channel->next = next_channel;
  	channel = next_channel; 
  }	
  
  if(length == 0 || length > BUF_LEN) 
  	return -EMSGSIZE; // the passed message length is 0 or more than buffer's size
 
  if (buffer == NULL || copy_from_user(channel->msg, buffer, length)!=0) // write message - copy data from the buffer to the channel
  	return -EFAULT;	
  
  channel->msg_len = length;
  
  return length; //Return the number of bytes written
}

//----------------------------------------------------------------
static long device_ioctl( struct   file* file,
                          unsigned int   ioctl_command_id,
                          unsigned long  ioctl_param )
{
  // Switch according to the ioctl called
  if( MSG_SLOT_CHANNEL == ioctl_command_id && ioctl_param!=0) {
  	((ifile*)(file->private_data))->channel_id = ioctl_param; //set the file descriptor’s channel id 
    return SUCCESS;
  }

  return -EINVAL;
  
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops = {
  .owner	  = THIS_MODULE, 
  .read           = device_read,
  .write          = device_write,
  .open           = device_open,
  .unlocked_ioctl = device_ioctl,
  .release        = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the device
static int __init simple_init(void)
{
  if(register_chrdev( MAJOR_NUM, DEVICE_NAME, &Fops ) < 0 ) 
  	return -1;
  
  return 0;
}

//---------------------------------------------------------------

static void __exit simple_cleanup(void)
{
  int i;
  ichannel *channel;
  ichannel *tmp;
  
  unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
  
  // go through all files in the device and free all allocated memory  
  for(i=0; i < MAX_FILES; i++){
  	channel = devFiles[i]->head;
  	// go through file's channels-list and free each node  
  	while(channel!=NULL){
 	 	tmp=channel->next;	
  		kfree(channel);
  		channel=tmp;
  	}
  	kfree(devFiles[i]); // free file's channels-list
  }
  
  --file_created_flag; // file removed from the device - turn off flag 
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
