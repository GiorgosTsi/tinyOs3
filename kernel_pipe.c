#include "util.h"
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_dev.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"


/*File ops struct for the reader FCB */
static file_ops reader_file_ops = {
	.Open  = open,
	.Read  = pipe_read,
	.Write = disable_write,/*Reader end cannot write in the buffer */
	.Close = pipe_reader_close
};

/*FIle ops struct for the Writer FCB */
static file_ops writer_file_ops = {
	.Open  = open,
	.Read  = disable_read,/*Writer end cannot read from the buffer */
	.Write = pipe_write,
	.Close = pipe_writer_close
};



int sys_Pipe(pipe_t* pipe)
{

	Fid_t fid[2]; //fids
	FCB* fcb[2]; //fcbs

	if(! FCB_reserve( 2, fid, fcb ))
		return -1; /*No fcbs left */

	//update the read,write fid of pipe
	pipe->read = fid[0];
	pipe->write = fid[1];


	pipe_cb* picb = xmalloc(sizeof(pipe_cb));

	/*Initialize pipe control block */

	picb->reader = fcb[0];
	picb->writer = fcb[1];  

	picb->has_space = COND_INIT;
	picb->has_data = COND_INIT;

	picb->w_position = 0;
	picb->r_position = 0;

	picb->remaining_space = PIPE_BUFFER_SIZE;

	/*Initialize the fcb's attributes: */

	fcb[0]->streamfunc = &reader_file_ops;
	fcb[1]->streamfunc = &writer_file_ops;

	fcb[0]->streamobj = picb;
	fcb[1]->streamobj = picb;
	//no need to increase refcounter of the above fcbs , because it is increased in the FCB_reserve function.


	return 0;
}


int pipe_write(void* pipecb_t,const char *buf , unsigned int size)
{
	pipe_cb* picb = (pipe_cb*) pipecb_t ; // cast to pipe control block
	if(!picb) return -1;  //pipe control block does not exist.

	//check if writer end is closed!
	if(!picb->writer) return -1;

	//if reader end is closed, there is no reason to write on the pipe.
	if(!picb->reader) return -1;

	int written_bytes_counter; // total bytes written on the pipe.

	/*block in condition variable, until there is at least 1 free space to write or reader end is closed */
	while(picb->remaining_space == 0 && picb->reader != NULL )
		kernel_wait( &(picb->has_space),SCHED_PIPE);

	//check if reader or writer ends are closed.
	if(!picb->reader || !picb->writer)
		return -1;

	//to be here, there is free remaining space to write and both ends are open.
	for( written_bytes_counter = 0 ; written_bytes_counter < picb->remaining_space ; written_bytes_counter++ ){

		//check if 'size' amount of bytes have been written.
		if(written_bytes_counter == size)
			break;

		picb->BUFFER[picb->w_position] = buf[written_bytes_counter];
		picb->w_position = (picb->w_position + 1) % PIPE_BUFFER_SIZE; // next write position.
		picb->remaining_space--;
	}

	/*Wake up reader threads, if they are waiting for data to be written. */
	kernel_broadcast(&picb->has_data);

	return written_bytes_counter;
}

int pipe_read(void* pipecb_t, char* buf , unsigned int size)
{
	pipe_cb* picb = (pipe_cb*) pipecb_t ; // cast to pipe control block
	if(!picb) return -1;  //pipe control block does not exist.

	//check if reader end is closed!
	if(!picb->reader) return -1;

	int read_bytes_counter; // total bytes read from the pipe.

	//if remaining space is buffer size , then the pipe is empty!We have no bytes to read.
	/*Sleep until remaining space <buffer size or the writer end close*/
	while(picb->remaining_space == PIPE_BUFFER_SIZE && picb->writer != NULL)
		kernel_wait( &picb->has_data , SCHED_PIPE);

	if(picb->remaining_space == PIPE_BUFFER_SIZE) // means that writer is null
		return 0; //there are no bytes to read, and writer end is closed.

	int total_bytes_to_read = PIPE_BUFFER_SIZE - picb->remaining_space;

	for(read_bytes_counter = 0 ; read_bytes_counter < total_bytes_to_read ; read_bytes_counter++){

		/*Check if we have read 'size' amount of bytes: */
		if(read_bytes_counter == size)
			break;

		buf[read_bytes_counter] = picb->BUFFER[picb->r_position];
		picb->r_position = (picb->r_position + 1)% PIPE_BUFFER_SIZE; // next read position.
		picb->remaining_space++;
	}
	
	/*wake up all writer threads, if they wait space to be free */
	kernel_broadcast(&picb->has_space);

	return read_bytes_counter;
}

int pipe_writer_close(void* _pipecb)
{

	pipe_cb* picb =  (pipe_cb*)(_pipecb);

	if(!picb)
		return -1;

	/*If writer end close,make the writer attribute null */
	picb->writer = NULL;

	//if reader end is also closed, then the pipe is useless, so free it
	if(!picb->reader)
		free(picb);

	return 0;
}

int pipe_reader_close(void* _pipecb)
{

	pipe_cb* picb =  (pipe_cb*)(_pipecb);

	if(!picb)
		return -1;

	/*If reader end close,make the reader attribute null */
	picb->reader = NULL;

	//if writer end is also closed, then the pipe is useless, so free it
	if(!picb->writer)
		free(picb);

	return 0;

}

/*Reader End of pipe cannot write in the buffer! Return error */
int disable_write(void* pipecb_t,const char *buf , unsigned int n)
{
	return -1;
}


/*Writer End of pipe cannot read from the buffer! Return error  */
int disable_read(void* pipecb_t, char* buf , unsigned int n)
{
	return -1;
}

/*We don't use open to create the pipe.Instead we use sys_Pipe! */
void* open(uint minor)
{
	return NULL;

}





