#include "posix_ircio.h"
#include <strings.h>
#include <errno.h>
#include <sys/select.h>

#include <iostream>

#include "ircLog.h"

std::string FILENAME = "posix_ircio.cpp";

extern int errno;
bool posix_ircio::open(std::string server, int port)
{
	if(isOpen)
	{
		close();
	}
	//get a file descriptor
	socket = ::socket(AF_INET, SOCK_STREAM, 0);
	if(socket < 0)
	{
		return false;
	}
	//std::cout << "ircio: got the fd" << std::endl;
	ircLog::instance()->logf(FILENAME, "got the fd");

	//look up the host
	hostent* serverhost = gethostbyname(server.c_str());
	if(serverhost == NULL)
	{
		return false;
	}
	//std::cout << "ircio: found the host entry" << std::endl;
	ircLog::instance()->logf(FILENAME, "found the host entry");

	//setup the sock address struct
	sockaddr_in sockadder;
	bzero((char*) &sockadder, sizeof(sockadder));

	sockadder.sin_family = AF_INET;

	bcopy((char*)serverhost->h_addr,
			(char*)&sockadder.sin_addr.s_addr, serverhost->h_length);

	sockadder.sin_port = htons(port);
	//std::cout << "ircio: set up the socket address struct" << std::endl;
	ircLog::instance()->logf(FILENAME, "set up the socket address struct");

	//open trhe socket
	sockaddr* temp = (sockaddr*) &sockadder;
	int result = connect(socket, temp, sizeof(sockadder));
	if(result < 0){
		return false;
	}
	//std::cout << "ircio: open the socket" << std::endl;
	ircLog::instance()->logf(FILENAME, "open the socket");

	//start a p thread running listening on the socket
	isOpen = true;

	pthread_create(&listenerThread, NULL, &posix_ircio::startListening, this);
	return true;

}

void posix_ircio::close()
{
	if(isOpen)
	{
		isOpen = false;
		usleep(SELECT_NSECS / 1000);
		::close(socket);
	}
}
bool posix_ircio::write(std::string& str)
{
	//std::cout << "ircio: using file discriptor: " << socket << std::endl;
	ircLog::instance()->logf(FILENAME, "using a file discriptor");

	int written =(int) ::write(socket, str.c_str(), str.size());

	if(written <0)
	{
		std::cout << "ircio: errno is: " << errno << std::endl;
		ircLog::instance()->logf(FILENAME, "errno is: %d", errno);
		return false;
	}
	else
	{
		return true;
	}
}

bool posix_ircio::read(std::string& temp)
{
	char buf[BUFSIZE] = {0};
	//read on the socket
	int chars = (int)::read(socket, buf, BUFSIZE);
	if(chars < 0)
		return false;
	 temp.assign(buf);
	return true;
}

void posix_ircio::listen()
{
//	std::string leftovers = "";
	ircBuffer buffer;



	while(isOpen)
	{
		timeval timeout;
		timeout.tv_sec = SELECT_SECS;
		timeout.tv_usec = SELECT_NSECS;


		fd_set selectSet;
		FD_ZERO(&selectSet);
		FD_SET(socket, &selectSet);

		char buf[BUFSIZE] = {0};

		//read on the socket
		//std::cout << "gonna hang on the socket again" << std::endl;
		//std::cout << "ircio: timespec seconds = " << timeout.tv_sec << " timespec nsec = " << timeout.tv_usec << std::endl;
		int selectval = select(socket + 1, &selectSet ,NULL, NULL, &timeout);
		//if select fails or times out we want to continue;
		//std::cout << "ircio: hanging on socket, selectval was " << selectval << std::endl;
		if(selectval < 1)
		{
			continue;
		}
		int chars = (int)::read(socket, buf, BUFSIZE);	
		if(chars < 0)
		{	//std::cout << "ircio: errno is: " << errno << std::endl;
			ircLog::instance()->logf(FILENAME, "errno is: %d", errno);

			switch(errno)
			{
				case EINTR:
				case EAGAIN:
					continue;
					break;
				case EBADF:
				case EINVAL:
				case EFAULT:
				case EIO:
				case EISDIR:
					//should brobably break responsibly here 
					//and lt the uper levles know what happened;
					onReceive("IRCERROR  Connection broken\r\n");
					return;
				
			}

			continue;
		}

		if(chars == 0)
		{
			close();	
			onReceive("IRCERROR zero lenth read\r\n");
		}

		//this shouldn't be needed if
		//the buffer is zeroed
		//buf[chars] = '\0';
		
		buffer.addBytes(buf, BUFSIZE);

/*		std::string buffer(buf);
		std::string temp = leftovers + buffer;
		size_t split = temp.rfind("\r\n");
		if(split == std::string::npos)
		{
			split = 0;
		}
		else
		{
			split += 2;
		}
		//std::cout<< "ircio: temp size: " << temp.size() << " split: " << split << std::endl;
		ircLog::instance()->logf(FILENAME, "temp size: %d\tsplit: %d", temp.size(), split);

		if(split > temp.size())
		{
			leftovers = "";
		}
		else
		{
			leftovers = temp.substr(split);
			temp = temp.substr(0, split);

			//std::cout << "Leftovers: " << leftovers << std::endl << "temp: " << temp << std::endl;
			ircLog::instance()->logf(FILENAME, "Leftovers: %s", leftovers.c_str());
			ircLog::instance()->logf(FILENAME, "temp: %s", temp.c_str());
		}

		//call on receive
		onReceive(temp);
		*/

		while(buffer.hasNextMessage())
		{
			onReceive(buffer.nextMessage());
		}

	}
}

void posix_ircio::onReceive(std::string msg){

	std::vector<ircioCallBack*>::iterator iter;
	for(iter = callbacks.begin(); iter < callbacks.end(); iter++)
	{
		(*iter)->onMessage(msg);
	}
}

void posix_ircio::registerCallBack(ircioCallBack* callback)
{
	bool found = false;

	for(size_t i = 0; i < callbacks.size(); ++i)
	{
		if(callbacks[i] == callback)
		{
			//this instance is already registered
			found = true;	
		}
	}

	//add the call back if we didn't find 
	if(!found){
		
		callbacks.push_back(callback);	
	}
}

void* posix_ircio::startListening(void* ptr){
	posix_ircio* io = (posix_ircio*) ptr;

	io->listen();

	return NULL;

}

unsigned int posix_ircio::sleep(unsigned int seconds)
{
	return ::sleep(seconds);
}
