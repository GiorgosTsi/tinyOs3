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

	if(listener_scb->type != SOCKET_LISTENER || PORT_MAP[listener_scb->port] != listener_scb)
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
	while(is_rlist_empty(&(listener_scb->listener_s.queue)))
	{
		kernel_wait(&(listener_scb->listener_s.req_available), SCHED_IO);
	}

	// check if the port is still valid as the socket may have been closed while we were sleeping
	if(PORT_MAP[listener_scb->port] != listener_scb)
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

    // signal the connect side
    kernel_signal(&(request_admitted->connected_cv));

    // decrease the listener's refcount
    listener_scb->refcount -= 1;

    return client_peer;

}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	return -1;
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
	return -1;
}

/*We don't use open to create the socket.Instead we use sys_Socket! */
void* socket_open(uint minor)
{
	return NULL;

}


