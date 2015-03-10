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


#ifndef RK_NTP_H
#define RK_NTP_H


#define	RK_LOCAL_NTPD_PORT	4214
#define RK_NTPD_SERVER_PORT	123

#define NTPD_TAU		0	
#define NTPD_POLL_RATE		(1<<NTPD_TAU) 
#define NTPD_SERVER_ADDR	"128.118.25.3" /*gps at gps1.otc.psu.edu */	



/* 
The current implementation of SNTP in RK does not care about
the extension fields or the authentication headers. Support for
these will be added in the future if deemed necessary
*/

struct rk_ntp_message
{
	unsigned char		li_vn_mode;
	unsigned char		stratum;
	unsigned char 		poll;
	unsigned char 		precision;
	unsigned int		root_delay;
	unsigned int		root_dispersion;
	unsigned int		reference_identifier;
	unsigned int		reference_timestamp_seconds;
	unsigned int		reference_timestamp_fraction;
	unsigned int		originate_timestamp_seconds;
	unsigned int		originate_timestamp_fraction;
	unsigned int		receive_timestamp_seconds;
	unsigned int		receive_timestamp_fraction;
	unsigned int		transmit_timestamp_seconds;
	unsigned int		transmit_timestamp_fraction;
};


#endif
