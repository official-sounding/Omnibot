#include"posix_ircio.h"
#include <strings.h>
#include<errno.h>

extern int errno;
bool posix_ircio::open(std::string server, int port)
{
	//get a file descriptor
	socket = ::socket(AF_INET, SOCK_STREAM, 0);
	if(socket < 0)
	{
		return false;
	}

	//look up the host
	hostent* serverhost = gethostbyname(server.c_str());
	if(serverhost == NULL)
	{
		return false;
	}

	//setup the sock address struct
	sockaddr_in sockadder;
	bzero((char*) &sockadder, sizeof(sockadder));

	sockadder.sin_family = AF_INET;

	bcopy((char*)serverhost->h_addr,
			(char*)&sockadder.sin_addr.s_addr, serverhost->h_length);

	sockadder.sin_port = htons(port);

	//open trhe socket
	sockaddr* temp = (sockaddr*) &sockadder;
	int result = connect(socket, temp, sizeof(sockadder));
	if(result < 0){
		return false;
	}

	//start a p thread running listening on the socket
	isOpen = true;

	pthread_create(&listenerThread, NULL, &posix_ircio::startListening, this);
	return true;

}

void posix_ircio::close()
{
	isOpen = false;
}

bool posix_ircio::write(std::string& str)
{
	std::cout << "ircio: using file discriptor: " << socket << std::endl;

	int written =(int) ::write(socket, str.c_str(), str.size());

	if(written <0)
	{
		std::cout << "ircio: errno is: " << errno << std::endl;
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
	std::string leftovers = "";
	while(isOpen)
	{

		char buf[BUFSIZE] = {0};

		//read on the socket
		std::cout << "gonna hang on the socket again" << std::endl;
		int chars = (int)::read(socket, buf, BUFSIZE);	
		if(chars < 0)
		{	std::cout << "ircio: errno is: " << errno << std::endl;

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
					onReceive("ERROR  Connection brokent \r\n");
					return;
				
			}

			continue;
		}

		//this shouldn't be needed if
		//the buffer is zeroed
		//buf[chars] = '\0';
		
		std::string buffer(buf);
		std::string temp = leftovers + buffer;
		size_t split = temp.find_last_of("\r\n");
		if(split == std::string::npos)
		{
			split = 0;
		}
		else
		{
			split += 2;
		}
		std::cout<< "ircio: temp size: " << temp.size() << " split: " << split << std::endl;

		if(split > temp.size())
		{
			leftovers = "";
		}
		else
		{
			leftovers = temp.substr(split);
			temp = temp.substr(0, split);

			std::cout << "Leftovers: " << leftovers << std::endl << "temp: " << temp << std::endl;
		}

		//call on receive
		onReceive(temp);

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
	callbacks.push_back(callback);	
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
