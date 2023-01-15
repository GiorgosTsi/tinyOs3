#include "kernel_socket.h"

static file_ops socket_file_ops = {
	.Open = socket_open,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close

};

Fid_t sys_Socket(port_t port)
{
	if(port < NOPORT || port > MAX_PORT){
		return NOFILE;
	}

	Fid_t fid; //fids
	FCB* fcb; //fcbs

	if(! FCB_reserve( 1, &fid, &fcb))
		return NOFILE; /*No fcbs left */
	
	socket_cb* scb = xmalloc(sizeof(socket_cb)); 

	// initialize the socket control block's attributes
	scb->refcount = 0;
	scb->fcb = fcb;
	scb->type = SOCKET_UNBOUND;
	scb->port = port;

	//initialize the fcb's attributes
	fcb->streamobj = scb;
	fcb->streamfunc = &socket_file_ops;

	return  fid;
	
}

int sys_Listen(Fid_t sock)
{	
	// check if the file id is not legal
	if(sock < 0 || sock > MAX_FILEID-1)
		return -1;

	FCB* socket_listener_fcb = get_fcb(sock); // get the fcb

	// if the fcb does not exist
	if(!socket_listener_fcb)
		return -1;

	// cast to socket control block
	socket_cb* listener_scb = (socket_cb*)socket_listener_fcb->streamobj;

	// if the socket scb does not exist
	if(!listener_scb)
		return -1;

	// if the socket is not bound to a port
	if(listener_scb->port == NOPORT)
		return -1;

	// if the type of socket is not unbound
	if(listener_scb->type != SOCKET_UNBOUND)
		return -1;

	// if the port bound to the socket is occupied by another listener
	if(PORT_MAP[listener_scb->port] != NULL)
		return -1;

	// initialize the socket listener

	listener_scb->type = SOCKET_LISTENER; // make the type of socket a listener

	rlnode_init(&listener_scb->listener_s.queue, NULL); // initialize the waiting to be accepted queue of listener

	listener_scb->listener_s.req_available = COND_INIT; // initialize the condition variable of the listener

	PORT_MAP[listener_scb->port] = listener_scb; // insert the listener to the port map array


	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	// check if the file id is not legal
	if(lsock < 0 || lsock > MAX_FILEID-1)
		return NOFILE;
	
	FCB* socket_listener_fcb = get_fcb(lsock); // get the fcb of the listener

	// if the fcb does not exist
	if(!socket_listener_fcb)
		return NOFILE;

	// cast to socket control block
	socket_cb* listener_scb = (socket_cb*)socket_listener_fcb->streamobj;

	// check if listener is null
	if(!listener_scb)
		return NOFILE;

	int port = listener_scb->port ;

	if(listener_scb->type != SOCKET_LISTENER || PORT_MAP[port] != listener_scb)
		return NOFILE;

	int i = 0;

	for(; i< MAX_FILEID; i++)
	{
		if(!(CURPROC->FIDT[i]))
			break;
	}

	if(i == MAX_FILEID)
		return NOFILE;

	// increase the listener's refcount as we are using it
	listener_scb->refcount += 1;

	// if the request queue is empty make the listener sleep on the req_available condition variable until a request is sent
	//if the listener socket closes when listener is sleeping, wake up
	while(is_rlist_empty(&(listener_scb->listener_s.queue)) && PORT_MAP[port] != NULL)
	{
		kernel_wait(&(listener_scb->listener_s.req_available), SCHED_IO);
	}

	// check if the port is still valid as the socket may have been closed while we were sleeping
	if(PORT_MAP[port] == NULL)
		return NOFILE;

	connection_request* request_admitted = rlist_pop_front(&(listener_scb->listener_s.queue))->connection_request;

	// get the socket who made the request
	socket_cb* client_socket = request_admitted->peer;

	// check if the client is an unbound socket
	if(client_socket->type != SOCKET_UNBOUND){
		return NOFILE;
	}

	// create a peer socket for the client to communicate with
	Fid_t client_peer = sys_Socket(client_socket->port);

	if(!client_peer)
		return NOFILE;

	request_admitted->admitted = 1; // make the request admitted
	client_socket->type = SOCKET_PEER; // change the requesting socket's type to SOCKET_PEER

	FCB* client_peer_fcb = get_fcb(client_peer); // get the fcb of the client's peer

	socket_cb* client_peer_scb = (socket_cb*)client_peer_fcb->streamobj; // get the scb of the client's peer

	client_peer_scb->type = SOCKET_PEER; // change the type of the client's peer to SOCKET_PEER

	// make the peer connections
	client_socket->peer_s.peer = client_peer_scb; 
	client_peer_scb->peer_s.peer = client_socket;

	// make the two pipes
	pipe_cb* pipe_cb1 = xmalloc(sizeof(pipe_cb));
	pipe_cb* pipe_cb2 = xmalloc(sizeof(pipe_cb));

	//initialize the first pipe
	pipe_cb1->has_space = COND_INIT;
    pipe_cb1->has_data = COND_INIT;
    pipe_cb1->w_position = 0;
    pipe_cb1->r_position = 0; 
    pipe_cb1->remaining_space = PIPE_BUFFER_SIZE;
    pipe_cb1->reader = client_peer_fcb;
    pipe_cb1->writer = client_socket->fcb;

    //initialize the second pipe
	pipe_cb2->has_space = COND_INIT;
    pipe_cb2->has_data = COND_INIT;
    pipe_cb2->w_position = 0;
    pipe_cb2->r_position = 0; 
    pipe_cb2->remaining_space = PIPE_BUFFER_SIZE;
    pipe_cb2->reader = client_socket->fcb;
    pipe_cb2->writer = client_peer_fcb;

    // make the client's pipe connections
    client_socket->peer_s.read_pipe = pipe_cb2;
    client_socket->peer_s.write_pipe = pipe_cb1;

    // make the client's peer pipe connections
    client_peer_scb->peer_s.read_pipe = pipe_cb1;
    client_peer_scb->peer_s.write_pipe = pipe_cb2;

    // signal the client, because the connection has been established.
    kernel_signal(&(request_admitted->connected_cv));

    // decrease the listener's refcount
    listener_scb->refcount -= 1;

    return client_peer;

}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{	

	//if file id is not legal return -1
	if(sock < 0 || sock > MAX_FILEID-1)
		return -1;

	FCB* client_fcb = get_fcb(sock);

	//check if the port is legal
	if(port <= NOPORT || port > MAX_PORT){
		return -1;
	}

	//check if there is a listener on that port
	if(!PORT_MAP[port]){
		return -1;
	}

	socket_cb* client_scb = (socket_cb*) client_fcb->streamobj;

	//check if the socket is an unbound socket
	if(client_scb->type != SOCKET_UNBOUND){
		return -1;
	}

	//pointer to the listener
	socket_cb* listener_scb = PORT_MAP[port];

	//increase refcount
	client_scb->refcount++;

	//Build Request
	connection_request* new_request = xmalloc(sizeof(connection_request));

	//initiallize variables
	new_request->admitted = 0;
	new_request->peer = client_scb;
	new_request->connected_cv = COND_INIT;
	rlnode_init(&(new_request->queue_node),new_request);

	//add the new request to the listeners queue
	rlist_push_back(&(listener_scb->listener_s.queue),&(new_request->queue_node));

	//signal the listener as a new request is available
	kernel_signal(&(listener_scb->listener_s.req_available));

	//while request is not admitted, the client will block for a specified ammount of time
	while(new_request->admitted == 0){

		//store the return value of kernel_timedwait
		int return_value = kernel_timedwait(&(new_request->connected_cv), SCHED_PIPE, timeout);

		//if return_value is 0 we timed out so break from the loop
		if(!return_value){
			break;
		}

	}

	//decrease the refcount
	client_scb->refcount--;
	//remove the request from the listener's queue
	rlist_remove(&(new_request->queue_node));

	//if the request was admitted, then the connect was successfull
	if(new_request->admitted){
		return 0;
	}


	return -1;	// if we reach here the request was not admitted!
	
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{	

	//if file id is not legal return -1
	if(sock < 0 || sock > MAX_FILEID-1)
		return -1;

	FCB* socket_fcb = get_fcb(sock);

	// if the fcb does not exist
	if(!socket_fcb)
		return -1;

	// cast to socket control block
	socket_cb* socket_scb = (socket_cb*)socket_fcb->streamobj;

	//check if the cause is to shutdown the reader
	if(how == SHUTDOWN_READ)
		return pipe_reader_close(socket_scb->peer_s.read_pipe);
	

	//check if the cause is to shutdown the writer
	if(how == SHUTDOWN_WRITE)
		return pipe_writer_close(socket_scb->peer_s.write_pipe);
	

	//check if the cause is to shutdown both reader and writer
	if(how == SHUTDOWN_BOTH){

		//store the return values
		int close_writer_return = pipe_writer_close(socket_scb->peer_s.write_pipe);
		int close_reader_return = pipe_reader_close(socket_scb->peer_s.read_pipe);

		//check if both pipes closed successfully
		if(close_writer_return == 0 && close_reader_return == 0)
			return 0;	
		
	}

	return -1;	// if we reach here something went wrong !
}

int socket_write(void* socketcb_t,const char *buf , unsigned int size)
{
	socket_cb* scb = (socket_cb*)socketcb_t; // cast to socket control block

	if(!scb) // check if socket control block does not exist
		return -1;

	// if this socket is a peer socket and the writer is not null then do pipe write and return the amount of bytes it wrote 
	if(scb->type == SOCKET_PEER && scb->peer_s.write_pipe != NULL)
	{
		return pipe_write(scb->peer_s.write_pipe, buf, size);
	}
	
	return -1; // if we reach here something went wrong !

}

int socket_read(void* socketcb_t, char* buf , unsigned int size)
{
	socket_cb* scb = (socket_cb*)socketcb_t; // cast to socket control block

	if(!scb) // check if socket control block does not exist
		return -1;

	// if this socket is a peer socket and the reader is not null then do pipe read and return the amount of bytes it read
	if(scb->type == SOCKET_PEER && scb->peer_s.read_pipe != NULL)
	{
		return pipe_read(scb->peer_s.read_pipe, buf, size);
	}
	
	return -1; // if we reach here something went wrong !
}

int socket_close(void* _socketcb)
{	
	socket_cb* socket_scb = (socket_cb*) _socketcb;

	if(!socket_scb){
		return -1;
	}

	if(socket_scb->type == SOCKET_PEER){
		
		if(socket_scb->peer_s.peer){
			pipe_writer_close(socket_scb->peer_s.write_pipe);
			pipe_reader_close(socket_scb->peer_s.read_pipe);
			socket_cb* peer= socket_scb->peer_s.peer;
			peer->peer_s.peer = NULL;
		}
		if(socket_scb->refcount == 0) free(socket_scb);
		return 0;
	}

	else if(socket_scb->type == SOCKET_LISTENER){
		PORT_MAP[socket_scb->port] = NULL;
		//if listener is sleeping in his condVar while waiting for a request, wake him up.
		kernel_signal(&(socket_scb->listener_s.req_available));
		if(socket_scb->refcount == 0) free(socket_scb);
		return 0;
	}

	else{//SOCKET_UNBOUND type
		free(socket_scb);
		return 0;
	}
}

/*We don't use open to create the socket.Instead we use sys_Socket! */
void* socket_open(uint minor)
{
	return NULL;

}


