#ifndef _KERNEL_PIPE_H
#define _KERNEL_PIPE_H

#include "util.h"

#define PIPE_BUFFER_SIZE 8200

int sys_Pipe(pipe_t* pipe);
int disable_write(void* pipecb_t,const char *buf , unsigned int n);
int disable_read(void* pipecb_t, char* buf , unsigned int n);
void* open_pipe(uint minor);
int pipe_write(void* pipecb_t,const char *buf , unsigned int size);
int pipe_read(void* pipecb_t, char* buf , unsigned int size);
int pipe_writer_close(void* _pipecb);
int pipe_reader_close(void* _pipecb);


typedef struct pipe_control_block {

	FCB* reader,*writer;

	CondVar has_space; /*For blocking writer if no space is available */

	CondVar has_data; /*For blocking reader until data are available */

	int w_position, r_position , remaining_space ; /*write, read position in buffer, and how many bytes are free to write*/

	char BUFFER[PIPE_BUFFER_SIZE]; /*bounded cyclic byte buffer */ 


}pipe_cb;

#endif