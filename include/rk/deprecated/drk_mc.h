/*
 * Copyright (C) 2000 TimeSys Corporation
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 * This file is derived from software distributed under the following terms:
 *
 * Real-Time and Multimedia Systems Laboratory
 * Copyright (c) 2000-2011 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Real-Time and Multimedia Systems Laboratory
 *  Attn: Prof. Raj Rajkumar
 *  Electrical and Computer Engineering, and Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 *  or via email to raj@ece.cmu.edu
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */


/*

 * drk.h : Master header file for distributed RK
 *	As of now the communication protocol has a simple scheme:
 *	
 *	_________________________________________
 *	|         |         |        |              
 *      | Message | Version | Length |	Message ........
 *	|  Type   |	    |        |
 *      |_________|_________|________|____________
 *
 *	The message type is 8 bits and it is used to identify the type of message 
 *	The next part is the version number of the message
 *	The version is used to discard duplicates and this is 8 bits long.
 *      This is followed by a length field that is 16 bits long
 * 	This protocol will be modified later on to accomodate the 
 *	security aspects and will develop as needed.
 *	
 */

#ifndef	RK_DRK_H
#define RK_DRK_H
#include <rk/rk_mc.h>

struct	rk_distributed_resource_container;
typedef struct rk_distributed_resource_container * rk_distributed_resource_container_t;

struct  rk_distributed_resource_set;
typedef struct rk_distributed_resource_set * 	rk_distributed_resource_set_t;

struct  rk_distributed_reserve;
typedef struct rk_distributed_reserve * 	rk_distributed_reserve_t;

struct  rk_node_id;
typedef struct rk_node_id* rk_node_id_t;

extern  rk_distributed_resource_container_t rk_distributed_resource_container_create(char *);

extern  int rk_distributed_resource_container_destroy(rk_distributed_resource_container_t);

extern  rk_distributed_resource_set_t rk_distributed_resource_set_create(rk_distributed_resource_container_t, char *, char *);
extern  int rk_distributed_resource_set_destroy(rk_distributed_resource_container_t, rk_distributed_resource_set_t);
extern  rk_distributed_reserve_t rk_distributed_cpu_reserve_create(rk_distributed_resource_set_t, cpu_reserve_attr_t);

#ifdef __KERNEL__
#include <net/ip.h>
#include <net/sock.h>
#include <linux/hash.h>

#define MAX_RK_MSG_SIZE					256
#define MAX_RK_BUF_SIZE					(MAX_RK_MSG_SIZE + 4)

/* The standard ports in which we are going to run */
#define	RK_LOCAL_RM_PORT				6987
#define RK_NAME_SERV_PORT				6988

/* The different types of messages are here. Remember no duplicates allowed.
	Must convert this to a cleaner enum later				*/

#define RK_MSG_ADVERTISE				0x01
#define RK_MSG_DRK_HANDLE_REGISTER_REQUEST		0x02
#define	RK_MSG_DRK_HANDLE_REGISTER_SUCCESS		0x03
#define RK_MSG_DRK_HANDLE_REGISTER_ERROR		0x04
#define RK_MSG_DRK_HANDLE_UNREGISTER_REQUEST		0x05
#define RK_MSG_DRK_HANDLE_UNREGISTER_RESPONSE		0x06
#define RK_MSG_DRK_RESOURCE_SET_CREATE_REQUEST		0x07
#define RK_MSG_DRK_RESOURCE_SET_CREATE_SUCCESS		0x08
#define RK_MSG_DRK_RESOURCE_SET_CREATE_ERROR		0x09
#define RK_MSG_DRK_RESOURCE_SET_DESTROY_REQUEST		0x0A
#define RK_MSG_DRK_RESOURCE_SET_DESTROY_RESPONSE	0x0B	
#define RK_MSG_DRK_CPU_RESERVE_CREATE_REQUEST		0x0C
#define RK_MSG_DRK_CPU_RESERVE_CREATE_SUCCESS		0x0D
#define RK_MSG_DRK_CPU_RESERVE_CREATE_ERROR		0x0E
#define RK_MSG_DRK_RESOURCE_SET_ATTACH_PROCESS_REQUEST	0x0F
#define RK_MSG_DRK_RESOURCE_SET_ATTACH_PROCESS_SUCCESS	0x10
#define RK_MSG_DRK_RESOURCE_SET_ATTACH_PROCESS_ERROR	0x11


/* Status of the distributed reserves */
#define RK_DRK_RESERVE_IS_NULL				0x00
#define RK_DRK_RESERVE_IS_ONLINE			0x01
#define RK_DRK_RESERVE_IS_ATTACHED			0x02

asmlinkage rk_distributed_resource_container_t sys_rk_distributed_resource_container_create(char *);
asmlinkage int sys_rk_distributed_resource_container_destroy(rk_distributed_resource_container_t);

struct rk_message
{
	__u8	msg_type;
	__u8	msg_version;
	__u16	msg_length;
	char 	payload[MAX_RK_MSG_SIZE];
};

/* The size of the registry table that we are going to use (As of now say 1024)	*/
#define RK_REGISTRY_INDEX_SIZE			10
#define	RK_REGISTRY_SIZE			(1<<RK_REGISTRY_INDEX_SIZE)

struct rk_local_registry_entry;

struct rk_registry_entry
{
	struct	list_head	rk_registry_list						;
	char 			distributed_container_name[RSET_NAME_LEN]			;
	struct 	sockaddr_in	distributed_container_owner					;
	
};	

struct rk_node_id
{
	struct sockaddr_in	resource_set_location						;
};

struct rk_distributed_reserve
{
	int status;
};			


struct rk_distributed_resource_set
{
	struct list_head			rk_distributed_resource_set_list		;
	rk_distributed_resource_container_t	resource_container				;
	char					resource_set_name[RSET_NAME_LEN]		;
	rk_distributed_reserve_t		reserve						;
	rk_node_id_t				node_id						;
};
 
struct	rk_distributed_resource_container
{
	struct rk_local_registry_entry *local_registry_link					;
	char 				distributed_container_name[RSET_NAME_LEN]		;
	rk_distributed_resource_set_t	distributed_resource_set				;
};


struct rk_local_registry_entry
{
	struct list_head			rk_local_registry_list				;
	rk_distributed_resource_container_t	distributed_container				;
	char					distributed_container_name[RSET_NAME_LEN]	;
	struct task_struct			*owner_process					;
	wait_queue_head_t			registration_queue				;
	int					request_outstanding				;	
};

static unsigned long inline rk_hash_name(char *name, unsigned int bits)
{
	unsigned long ret;
	int i;
	ret = 0;
	for(i=0; name[i]!='\0' && i<=RSET_NAME_LEN; i++)
	{
		/* Assumes a char is a Byte */
		ret *= (0xff);
		ret += name[i];
		/* Overflowing is not an issue, since we are only going to hash */
	}
	if(i==RSET_NAME_LEN)
	{
		printk("rk_hash_name: The length of name is greater than RSET_NAME_LEN so truncating\n");
	}
	return hash_long(ret, bits);	

}


/* Socket prototypes 		*/
struct socket * rk_create_socket(void);
int rk_bind_socket(struct socket *rk_sock, int port);
void rk_release_socket(struct socket *rk_sock);
int rk_recv(struct socket *rk_sock, void *data, int length, int flags, struct sockaddr_in *addr);
int rk_sendto(struct socket *rk_sock, void *data, int length, struct sockaddr_in *addr);

/* Registration prototypes 	*/
void rk_register_name_locally(struct rk_local_registry_entry *node);
struct rk_local_registry_entry * rk_local_registry_lookup(char *name);
#endif  /* __KERNEL__ */


#endif
