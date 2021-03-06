#include "ClientHandler.hpp"
#include "Validator.hpp"

ClientHandler::ClientHandler(const Client & client)
: _client(client),
_request(client.get_server_config()),
_response(_request, client.get_server_ip(), client.getip(), client.getport()),
_req_parser(_request),
_timer(CLIENT_TIMEOUT),
_event_flag(POLLIN)
{}

ClientHandler::ClientHandler(ClientHandler const & src)
: _client(src._client)
, _request(src._request)
, _response(src._response)
, _req_parser(src._req_parser)
, _timer(src._timer)
, _event_flag(src._event_flag)
{}

ClientHandler::~ClientHandler(void)
{
	delete &_client;
}

int
ClientHandler::get_clientfd(void) const
{
	return _client.getsockfd();
}

void 
ClientHandler::readable(void)
{
	char		read_buff[RECV_BUF_SIZE + 1] = {0};
	ssize_t		bytes_read;

	bytes_read = recv(_client.getsockfd(), read_buff, RECV_BUF_SIZE, 0);

	switch (bytes_read)
	{
		case -1 :
		{
			throw ClientSystemException("Unable to read from client socket", _client.getip(), _client.getsockfd());
		}
		case 0 :
		{
			throw ClientException("Connection closed by client", _client.getip(), _client.getsockfd());
		}
		default :
		{
			_req_parser.setbuffer(read_buff, bytes_read);
			handle_request();
			_timer.reset();
		}
	}
	Logger(basic_type, minor_lvl) << "Socket content (" << bytes_read << " byte read): " << read_buff;
}

void
ClientHandler::writable(void)
{
	if (_response.ready_to_send())
	{
		switch(_response.send_payload(_client.getsockfd()))
		{
			case -1 :
			{
				throw ClientSystemException("Unable to write to client socket", _client.getip(), _client.getsockfd());
			}
			case 0 :
			{
				_timer.reset();
				break ;
			}
			case 1 :
			{
				_timer.reset();
				if (_response.complete())
				{
					std::string status_line = _response.get_status_line();
					status_line.erase(status_line.size() - 2, 2); // Remove trailing "\r\n"
					Logger(basic_type, major_lvl) << get_req_line() << " << " << status_line << " (from " << _client.getip() << ')';
					if (_response.close_connection())
					{
						throw ClientException("Http error: closing connection", _client.getip(), _client.getsockfd());
					}
					_response.reset();
					_req_parser.next_request();
					if (!_request.complete())
					{
						_event_flag = POLLIN;
					}
				}
				break ;
			}
		}
	}
}

bool
ClientHandler::is_timeoutable(void) const
{
	return true;
}

bool
ClientHandler::is_timeout(void) const
{
	return _timer.expired();
}

int
ClientHandler::get_event_flag(void) const
{
	return _event_flag;
}

void
ClientHandler::handle_request(void)
{
	try {
		parse_request();
		if (_request.complete())
		{
			
			Validator::get_instance().validate_request_inputs(_request, _response);
			_request.method().handle(_request, _response);
		}
	}
	catch (HttpException & e)
	{
		Logger(basic_type, major_lvl) << "Http Exception: " << StatusCodes::get_code_msg_from_index(e.get_code_index());

		if (StatusCodes::get_code_value(e.get_code_index()) >= 400)
		{
			handle_http_error(e.get_code_index());
		}
		else if (StatusCodes::get_code_value(e.get_code_index()) >= 300)
		{
			_response.http_redirection(e.get_code_index(), e.get_location());
		}
	}
	catch (SystemException & e)
	{
		Logger(error_type, minor_lvl) << e.what() << " : " << _client.getip();
		handle_http_error(StatusCodes::INTERNAL_SERVER_ERROR_500);
	}
}

void	
ClientHandler::parse_request(void)
{
	_req_parser.parse();
	if (_request.complete())
	{
		_event_flag = POLLOUT;
	}
}

void
ClientHandler::handle_http_error(StatusCodes::status_index_t error_code)
{
	_response.http_error(error_code);
	_event_flag = POLLOUT;	
}

std::string
ClientHandler::get_req_line(void)
{
	std::string log = _req_parser.get_method() + ' ' + _request.uri().path;
	if (!_request.uri().query.empty())
	{
		log += '?' + _request.uri().query;
	}
	log += ' ' + _req_parser.get_version();

	return log;
}