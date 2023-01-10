#ifndef KERNEL_SOCKETS_H
#define KERNEL_SOCKETS_H

#include "bios.h"
#include "tinyos.h"
#include "util.h"
#include "kernel_pipe.h"
#include "kernel_streams.h"
#include "kernel_proc.h"
#include "kernel_cc.h"

typedef struct socket_control_block socket_cb;

socket_cb* PORT_MAP[MAX_PORT]; // the array that houses all the ports

Fid_t sys_Socket(port_t port);

int sys_Listen(Fid_t sock);

int sys_Connect(Fid_t sock, port_t port, timeout_t timeout);

int sys_ShutDown(Fid_t sock, shutdown_mode how);

void* socket_open(uint minor);

int socket_write(void* socketcb_t,const char *buf , unsigned int size);

int socket_read(void* socketcb_t, char* buf , unsigned int size);

int socket_close(void* _socketcb);


typedef enum 
{
	SOCKET_LISTENER, 
	SOCKET_UNBOUND,
	SOCKET_PEER
}socket_type;

typedef struct listener_socket
{
	rlnode queue; // the queue of all requested sockets
	CondVar req_available; // the condition
	
}listener_socket;

typedef struct unbound_socket
{
	rlnode unbound_socket; 

}unbound_socket;

typedef struct peer_socket
{
	socket_cb* peer;	// pointer to the other connected socket
	pipe_cb* write_pipe;
	pipe_cb* read_pipe;

}peer_socket;

typedef struct connection_request
{
	int admitted; // 0 if not admitted 1 otherwise
	socket_cb* peer; // pointer to the socket that has admitted a request
	CondVar connected_cv; // edw koimatai o client
	rlnode queue_node;
}connection_request;

typedef struct socket_control_block
{
	uint refcount;

	FCB* fcb;

	socket_type type; //the type of socket

	port_t port; // the port to be used

	union
	{
		listener_socket listener_s;
		unbound_socket unbound_s;
		peer_socket peer_s;
	};

}socket_cb;

#endif