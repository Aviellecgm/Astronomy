/*
The stellarium telescope library helps building
telescope server programs, that can communicate with stellarium
by means of the stellarium TCP telescope protocol.
It also contains smaple server classes (dummy, Meade LX200).

Author and Copyright of this file and of the stellarium telescope library:
Johannes Gajdosik, 2006

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "Connection.hpp"
#include "Server.hpp"
#include "LogFile.hpp"

#include <iostream>
#include <iomanip>
using namespace std;

#include <math.h>

#ifdef DEBUG5
struct PrintRaDec
{
	PrintRaDec(const unsigned int ra_int, const int dec_int) : ra_int(ra_int), dec_int(dec_int) {}
	const unsigned int ra_int;
	const int dec_int;
};

static ostream &operator<<(ostream &o,const PrintRaDec &x)
{
	unsigned int h = x.ra_int;
	int d = (int)floor(0.5+x.dec_int*(360*3600*1000/4294967296.0));
	char dec_sign;
	if (d >= 0)
	{
		if (d > 90*3600*1000)
		{
			d =  180*3600*1000 - d;
			h += 0x80000000;
		}
		dec_sign = '+';
	}
	else
	{
		if (d < -90*3600*1000)
		{
			d = -180*3600*1000 - d;
			h += 0x80000000;
		}
		d = -d;
		dec_sign = '-';
	}
	h = (unsigned int)floor(0.5+h*(24*3600*10000/4294967296.0));
	const int ra_ms = h % 10000; h /= 10000;
	const int ra_s = h % 60; h /= 60;
	const int ra_m = h % 60; h /= 60;
	h %= 24;
	const int dec_ms = d % 1000; d /= 1000;
	const int dec_s = d % 60; d /= 60;
	const int dec_m = d % 60; d /= 60;
	o << "ra = "
	  << setfill(' ') << setw(2) << h << 'h'
	  << setfill('0') << setw(2) << ra_m << 'm'
	  << setfill('0') << setw(2) << ra_s << '.'
	  << setfill('0') << setw(4) << ra_ms
	  << " dec = "
	  << ((d<10)?" ":"") << dec_sign << d << 'd'
	  << setfill('0') << setw(2) << dec_m << 'm'
	  << setfill('0') << setw(2) << dec_s << '.'
	  << setfill('0') << setw(3) << dec_ms
	  << setfill(' ');
	return o;
}
#endif

Connection::Connection(Server &server, SOCKET fd) : Socket(server, fd)
{
	read_buff_end = read_buff;
	write_buff_end = write_buff;
	server_minus_client_time = 0x7FFFFFFFFFFFFFFFLL;
}

void Connection::prepareSelectFds(fd_set &read_fds,
                                  fd_set &write_fds,
                                  int &fd_max)
{
	if (!IS_INVALID_SOCKET(fd))
	{
		if (fd_max < (int)fd)
			fd_max = (int)fd;
		if (write_buff_end > write_buff)
			FD_SET(fd, &write_fds);
		FD_SET(fd, &read_fds);
	}
}

void Connection::handleSelectFds(const fd_set &read_fds,
                                 const fd_set &write_fds)
{
	if (!IS_INVALID_SOCKET(fd))
	{
		if (FD_ISSET(fd, const_cast<fd_set *>(&write_fds)))
		{
			performWriting();
		}
		if (!IS_INVALID_SOCKET(fd) && FD_ISSET(fd, const_cast<fd_set *>(&read_fds)))
		{
			performReading();
		}
	}
}


void Connection::performWriting(void)
{
	const int to_write = write_buff_end - write_buff;
	const int rc = writeNonblocking(write_buff, to_write);
	if (rc < 0)
	{
		if (ERRNO != EINTR && ERRNO != EAGAIN)
		{
			*log_file << Now() << "Connection::performWriting: writeNonblocking failed: "
			                   << STRERROR(ERRNO) << endl;
		hangup();
		}
	}
	else if (rc > 0)
	{
	#ifdef DEBUG5
		if (isAsciiConnection())
		{
			*log_file << Now() << "Connection::performWriting: writeNonblocking("
			                   << to_write << ") returned "
			                   << rc << "; ";
			for (int i = 0; i < rc; i++)
				*log_file << write_buff[i];
			*log_file << endl;
		}
	#endif
		if (rc >= to_write)
		{
			// everything written
			write_buff_end = write_buff;
		}
		else
		{
			// partly written
			memmove(write_buff, write_buff + rc, to_write - rc);
			write_buff_end -= rc;
		}
	}
}

void Connection::performReading(void)
{
	const int to_read = read_buff + sizeof(read_buff) - read_buff_end;
	const int rc = readNonblocking(read_buff_end, to_read);
	if (rc < 0)
	{
		if (ERRNO == ECONNRESET)
		{
			*log_file << Now() << "Connection::performReading: "
			                      "client has closed the connection" << endl;
			hangup();
		} 
		else if (ERRNO != EINTR && ERRNO != EAGAIN)
		{
			*log_file << Now() << "Connection::performReading: readNonblocking failed: "
			                   << STRERROR(ERRNO) << endl;
			hangup();
		}
	} 
	else if (rc == 0)
	{
		if (isTcpConnection())
		{
			*log_file << Now() << "Connection::performReading: "
			                      "client has closed the connection" << endl;
			hangup();
		}
	}
	else
	{
	#ifdef DEBUG5
		if (isAsciiConnection())
		{
			*log_file << Now() << "Connection::performReading: readNonblocking returned "
			                   << rc << "; ";
			for (int i = 0; i < rc; i++)
				*log_file << read_buff_end[i];
			*log_file << endl;
		}
	#endif
	
		read_buff_end += rc;
		const char *p = read_buff;
		dataReceived(p, read_buff_end);
		if (p >= read_buff_end)
		{
			// everything handled
			//*log_file << Now() << "Connection::performReading: everything handled" << endl;
			read_buff_end = read_buff;
		}
		else if (p > read_buff)
		{
			//*log_file << Now() << "Connection::performReading: partly handled: "
			//          << (p-read_buff) << endl;
			// partly handled
			memmove(read_buff, p, read_buff_end - p);
			read_buff_end -= (p - read_buff);
		}
	}
}

void Connection::dataReceived(const char *&p, const char *read_buff_end)
{
	while (read_buff_end - p >= 2)
	{
		const int size = (int)( ((unsigned char)(p[0])) |
		                        (((unsigned int)(unsigned char)(p[1])) << 8) );
		if (size > (int)sizeof(read_buff) || size < 4)
		{
			*log_file << Now() << "Connection::dataReceived: "
		                              "bad packet size: " << size << endl;
			hangup();
			return;
		}
		if (size > read_buff_end-p)
		{
			// wait for complete packet
			break;
		}
		const int type = (int)( ((unsigned char)(p[2])) |
		                        (((unsigned int)(unsigned char)(p[3])) << 8) );
		// dispatch:
		switch (type)
		{
			case 0:
			//A "go to" command
			{
				if (size < 12)
				{
					*log_file << Now() << "Connection::dataReceived: "
					                      "type 0: bad packet size: " << size << endl;
					hangup();
					return;
				}
				const long long int client_micros = (long long int)
				               (  ((unsigned long long int)(unsigned char)(p[ 4])) |
				                 (((unsigned long long int)(unsigned char)(p[ 5])) <<  8) |
				                 (((unsigned long long int)(unsigned char)(p[ 6])) << 16) |
				                 (((unsigned long long int)(unsigned char)(p[ 7])) << 24) |
				                 (((unsigned long long int)(unsigned char)(p[ 8])) << 32) |
				                 (((unsigned long long int)(unsigned char)(p[ 9])) << 40) |
				                 (((unsigned long long int)(unsigned char)(p[10])) << 48) |
				                 (((unsigned long long int)(unsigned char)(p[11])) << 56) );
				server_minus_client_time = GetNow() - client_micros;
				const unsigned int ra_int =
				                  ((unsigned int)(unsigned char)(p[12])) |
				                 (((unsigned int)(unsigned char)(p[13])) <<  8) |
				                 (((unsigned int)(unsigned char)(p[14])) << 16) |
				                 (((unsigned int)(unsigned char)(p[15])) << 24);
				const int dec_int =
				          (int)(  ((unsigned int)(unsigned char)(p[16])) |
				                 (((unsigned int)(unsigned char)(p[17])) <<  8) |
				                 (((unsigned int)(unsigned char)(p[18])) << 16) |
				                 (((unsigned int)(unsigned char)(p[19])) << 24) );
				#ifdef DEBUG5
				*log_file << Now() << "Connection::dataReceived: "
				                   << PrintRaDec(ra_int,dec_int)
				                   << endl;
				#endif
				
				server.gotoReceived(ra_int, dec_int);
			}
			break;
			
			default:
			//No other types of commands are acceptable at the moment
				*log_file << Now()
				          << "Connection::dataReceived: "
				             "ignoring unknown packet, type: "
				          << type
				          << endl;
			break;
		}
		
		p += size;
	}
}

void Connection::sendPosition(unsigned int ra_int, int dec_int, int status)
{
	if (!IS_INVALID_SOCKET(fd))
	{
	#ifdef DEBUG5
		*log_file << Now() << "Connection::sendPosition: "
		                   << PrintRaDec(ra_int, dec_int)
		                   << endl;
	#endif
	if (write_buff_end - write_buff + 24 < (int)sizeof(write_buff))
	{
		// length of packet:
		*write_buff_end++ = 24;
		*write_buff_end++ = 0;
		// type of packet:
		*write_buff_end++ = 0;
		*write_buff_end++ = 0;
		// server_micros:
		long long int now = GetNow();
		*write_buff_end++ = now; now>>=8;
		*write_buff_end++ = now; now>>=8;
		*write_buff_end++ = now; now>>=8;
		*write_buff_end++ = now; now>>=8;
		*write_buff_end++ = now; now>>=8;
		*write_buff_end++ = now; now>>=8;
		*write_buff_end++ = now; now>>=8;
		*write_buff_end++ = now;
		// ra:
		*write_buff_end++ = ra_int; ra_int>>=8;
		*write_buff_end++ = ra_int; ra_int>>=8;
		*write_buff_end++ = ra_int; ra_int>>=8;
		*write_buff_end++ = ra_int;
		// dec:
		*write_buff_end++ = dec_int; dec_int>>=8;
		*write_buff_end++ = dec_int; dec_int>>=8;
		*write_buff_end++ = dec_int; dec_int>>=8;
		*write_buff_end++ = dec_int;
		// status:
		*write_buff_end++ = status; status>>=8;
		*write_buff_end++ = status; status>>=8;
		*write_buff_end++ = status; status>>=8;
		*write_buff_end++ = status;
		}
		else
		{
			*log_file << Now() << "Connection::sendPosition: "
			                      "communication is too slow, I will ignore this command"
			                   << endl;
		}
	}
}

