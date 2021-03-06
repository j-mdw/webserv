#include "ServerHandler.hpp"
#include <fcntl.h>

ServerHandler::ServerHandler(const Server * server) :
_server(*server),
_event_flag(POLLIN)
{}


ServerHandler::ServerHandler(ServerHandler const & src) :
_server(src._server),
_event_flag(src._event_flag)
{}

ServerHandler::~ServerHandler(void)
{
	delete &_server;
}

void
ServerHandler::readable(void)
{
	socklen_t sockaddr_size = sizeof(struct sockaddr);

	while (1)
	{
		struct sockaddr * address = new (struct sockaddr);
		int fd = accept(get_serverfd(), address, &sockaddr_size);

		if (fd >= 0)
		{
			if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
			{
				Logger(error_type, error_lvl) << "Unable to set nonblocking flag on client fd: " << inet_ntoa((reinterpret_cast<sockaddr_in *>(address))->sin_addr);
				delete address;
				close(fd);
			}
			else
			{
				Client * client = new Client(fd, address, _server.getip(), _server.get_server_config());
				Logger(basic_type, minor_lvl) << "A new connection has been accepted on fd : " << fd << " for client " << client->getip();
				InitiationDispatcher::get_instance().add_client_handle(*client);
			}
		}
		else
		{
			delete address;
			if (errno != EWOULDBLOCK)
				throw ServerSystemException("Accept error", _server.getip(), _server.getport());
			Logger(basic_type, debug_lvl) << "Accept backlog of " << _server.getsockfd() << " is empty";
			break ;
		}
	}
}

// No writable action can be detected on a server socket, hence this function does not do anything
void
ServerHandler::writable(void)
{}

bool
ServerHandler::is_timeoutable(void) const
{
	return false;
}

bool
ServerHandler::is_timeout(void) const
{
	return false;
}

int
ServerHandler::get_event_flag(void) const
{
	return _event_flag;
}

int
ServerHandler::get_serverfd(void) const
{
    return _server.getsockfd();
}