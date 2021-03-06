#include "GetMethod.hpp"
#include "Request.hpp"
#include "Response.hpp"
#include "InitiationDispatcher.hpp"
#include "Mime.hpp"
#include <dirent.h>
#include <sstream>
#include <fcntl.h>

GetMethod::GetMethod(void) {}

GetMethod::~GetMethod(void) {}

void
GetMethod::handle(Request & req, Response & resp)
{
	if (resp.path_is_dir())
	{
		if (req.get_config()->is_autoindex_on() == false)
		{
				throw (HttpException(StatusCodes::NOT_FOUND_404));
		}
		else
		{
			handle_autoindex(resp);
			return ;
		}
	}
	else if (resp.need_cgi())
	{
		add_cgi_handle(req, resp);
		return ;
	}

	resp.set_http_code(StatusCodes::OK_200);
	set_content_length_header(resp);
	set_content_type_header(resp);

	int fd = open(resp.get_path().c_str(), O_RDONLY);
	if (fd < 0)
	{
		throw SystemException("Error opening file");
	}
	resp.set_handler_fd(fd);
	InitiationDispatcher::get_instance().add_read_handle(get_file_size(resp.get_path()), resp);
	(void)req;
}

bool
GetMethod::has_body(void)
{
	return false;
}

void
GetMethod::set_content_length_header(Response & resp)
{
	off_t file_size = get_file_size(resp.get_path());
	std::stringstream ss;
	ss << file_size;
	resp.add_header("Content-Length", ss.str());
}

off_t
GetMethod::get_file_size(const std::string & path)
{
	struct stat stat_buf;

	int ret = stat(path.c_str(), &stat_buf);
	if (ret != 0)
	{
		throw SystemException("Error on stat call");
	}
	return stat_buf.st_size;
}

void
GetMethod::set_content_type_header(Response & resp)
{
	const std::string path = resp.get_path();

	int i = path.size() - 1;
	for(; i >= 0; i--)
	{
		if (path[i] == '.')
			break;
	}
	if (i >= 0)
	{
		std::string file_ext = path.substr(i + 1);
		const std::string * mime_ext = Mime::get_content_type(file_ext);
		if (mime_ext != NULL)
			resp.add_header("Content-Type", *mime_ext);
	}
}

void
GetMethod::handle_autoindex(Response & resp)
{
	DIR * dirp = opendir(resp.get_path().c_str());

	if (dirp == NULL)
	{
		throw (HttpException(StatusCodes::INTERNAL_SERVER_ERROR_500));
	}

	std::string autoindex_content;
	autoindex_content += "<html>";
	autoindex_content += "<head><meta charset=\"UTF-8\" /></head>";

	struct dirent * dir_entry;
	
	while ((dir_entry = readdir(dirp)) != NULL)
	{
		if (dir_entry->d_name[0] == '.' && !dir_entry->d_name[1])
		{
			continue;
		}
		add_autoindex_line(autoindex_content, dir_entry->d_name, resp.get_path(), (dir_entry->d_type & DT_DIR));
	}
	autoindex_content += "</html>";

	if (closedir(dirp) != 0)
	{
		throw (SystemException("Trying to close directory failed"));
	}
	std::stringstream ss;
	ss << autoindex_content.size();
	resp.set_http_code(StatusCodes::OK_200);
	resp.add_header("Content-Length", ss.str());
	resp.add_header("Content-Type", "text/html");
	resp.set_payload(autoindex_content.c_str(), autoindex_content.size());
	resp.ready_to_send() = true;
	resp.complete() = true;
}

void
GetMethod::add_autoindex_line(std::string & dest, const std::string & ressource_name, const std::string & path, bool isdir)
{
		dest += "<a href=\"";
		(void)path;
		dest += ressource_name;
		if (isdir)
			dest += '/';
		dest += "\">";
		dest += ressource_name;
		if (isdir)
			dest += '/';
		dest += "</a><br>";
}

void
GetMethod::add_cgi_handle(Request & req, Response & resp)
{
	int pipe_fd[2];
	int ret = pipe(pipe_fd);
	if (ret < 0)
	{
		Logger(error_type, error_lvl) << "Pipe: " << std::strerror(errno);
		throw HttpException(StatusCodes::INTERNAL_SERVER_ERROR_500);
	}
	resp.set_handler_fd(pipe_fd[1]); // Setting handler to write end of the pipe, as CGI handler will first need to write to the pipe
	InitiationDispatcher::get_instance().add_cgi_handle(req, resp, pipe_fd, "GET");
}