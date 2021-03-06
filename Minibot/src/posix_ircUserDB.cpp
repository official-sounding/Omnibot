#include "posix_ircUserDB.h"

#include <iostream>
#include <sstream>
posix_ircUserDB::posix_ircUserDB()
{

	//initialize log file
	dbLog.open("userDb.log", std::ios_base::out | std::ios_base::app | std::ios_base::ate);

	//initialize the random number generator
	srand(time(NULL));

	//initilize the mutexes
	pthread_mutex_init(&theBigLock, NULL);
	pthread_mutex_init(&userLock, NULL);

	//load data on persistant nicks
	loadData();
}

posix_ircUserDB::~posix_ircUserDB()
{
	//save current state of registered users
	saveData();

	//clean up mutexes
	pthread_mutex_destroy(&theBigLock);
	pthread_mutex_destroy(&userLock);
}

void posix_ircUserDB::addUser(std::string nick, bool registered = false, bool authenticated = false)
{
	//look for nick in all users 
	usersByNick_it iter = _usersByNick.find(nick);

	//check to see if the nick wasn't in the database or it had no associated ircUser Object
	if(iter == _usersByNick.end() || (*iter).second == NULL)
	{
		dbLog << "posix_ircUserDB::addUser the user wasn't found in the database." << std::endl;

		//create new user object
		ircUser* temp = new ircUser();
		temp->_nick= nick;

		//check to see if the nick is in the registered users table
		registeredId_it idIter = _registeredNicksToIDs.find(nick);

		//if we already no from the args or if it was in the table 
		//then we want to use the existing id for that user
		if(registered || idIter != _registeredNicksToIDs.end())
		{
			temp->_userId = (*idIter).second;
		}
		//if there is no evidence that the nick is registered
		//we need to assign a new id number
		else
		{
			//generate a new id
			temp->_userId = getNextAvailableID();
		}

		//apparently for now everyone is authenticated
		temp->_isAuthenticated = authenticated;

		dbLog << "posix_ircUserDB::addUser: user object created, adding to database..." << std::endl;

		//add it to both maps
		_usersByNick[nick] = temp;
		_usersByID[temp->userId()] = temp;

		/*{
			ircUser* ohai = _usersByNick[nick];

			dbLog << "posix_ircUserDB: the new nick object in usersbynick: " << std::hex << ohai << std::dec << std::endl;
		}*/
	}

	//if the nick is in the database we don't need to do anything
	else
	{
		dbLog << "posix_ircUserDB::addUser: use was already in the DB" << std::endl;
	}
}

//TODO this should be redone with channels by nick
void posix_ircUserDB::removeUser(std::string nick)
{
	//get user object
	usersByNick_it user_i = _usersByNick.find(nick);
	if(user_i == _usersByNick.end())
	{
		//nick doen't exist
		return;
	}

	//get the ircUser Object for the nick in question
	ircUser* user = (*user_i).second;

	//verify that the object exists
	if(user == NULL)
	{
		std::cerr << "posix_ircUserDB::removeUser: user " << nick 
			<< " had a NULL object"  << std::endl;

		//print the users for error checking
		printAllUsers();
	}


	//remove it from channels;
	channels_it channels;

	//for each channel's list of users 
	for(channels = _usersByChannel.begin(); channels != _usersByChannel.end(); channels++)
	{
		//for each nick in the channel's roster
		nicklist_it nicks;
		for (nicks = (*channels).second.begin(); nicks != (*channels).second.end(); nicks++){

			//make sure there isn't a null value in the vector
			//TODO figure out why this is necessary >.>
			if((*nicks) == NULL)
			{
				std::cerr << "posix_ircUserDB::removeUser: there was a null pointer in " 
					<< (*channels).first << "'s roster" << std::endl; 

				//print the users for debug info
				printAllUsers();

				//skip to the next user in the roster
				continue;
			}

			//compare the id of the user in question to the id of the one 
			//we want to delete from the channel
			if((*nicks)->userId() == user->userId())
			{
				//if we found nick in this channel, we can remove it
				//from the vector and then skip to the next channel
				(*channels).second.erase(nicks);
				break;
			}
		}
	}
	
	//all of the ircUser references for this object in the channes should be
	//cleaned up now, so we will move to the global structs.
	
	//remove from usersbyid
	_usersByID.erase(_usersByID.find(user->userId()));
	
	//remove from users by nick
	_usersByNick.erase(_usersByNick.find(user->nick()));

	//now that all the refernces are cleaned up,
	//we can delete the object itself
	delete user;
}	

void posix_ircUserDB::addUserToChannel(std::string nick, std::string channel)
{
	dbLog << "posix_ircUserDB: adding user: " << nick << std::endl;
	dbLog << "before: " << std::endl;
	printChannelUsers(channel);
	usersByNick_it user = _usersByNick.find(nick);

	_usersByChannel[channel].push_back((*user).second);

	dbLog << "after:" << std::endl;
	printChannelUsers(channel);
}

void posix_ircUserDB::removeUserFromChannel(std::string nick, std::string channel)
{
	//look up the channel we want
	channels_it iter = _usersByChannel.find(channel);
	
	//make sure it existes
	if(iter == _usersByChannel.end())
	{
		dbLog << "posix_ircUserDB::removeUserFromChannel(): no such channel" << std::endl;
		return;
	}

	//get the vector of users
	std::vector<ircUser*> users =  (*iter).second;
	
	//find the user we want to remove from the channel in the 
	//list of nicks
	usersByNick_it temp = _usersByNick.find(nick);


	//make sure that user actually esits
	if(temp == _usersByNick.end())
	{
		dbLog << "posix_ircUserDB::removeUserFromChannel(): no such nick exists" << std::endl;
		return;
	}

	//save off that users id
	unsigned int userId = (*temp).second->userId();


	std::vector<ircUser*>::iterator channelUser;

	printChannelUsers(channel);

	//look through the vector for a pointer to a nick with the same user id.
	for(channelUser = users.begin(); channelUser != users.end(); channelUser++)
	{
		//there have been nulls in the list before 
		//handle them if we find any
		if((*channelUser) == NULL)
		{
			dbLog << "RemoveUserFromChannel" << std::endl;
			printAllUsers();
			continue;
		}

		//
		if((*channelUser)->userId() == userId)
		{
			users.erase(channelUser);
			break;
		}
	}

	//see if the nick is any other channels
	channels_it channels;
	for(channels = _usersByChannel.begin(); channels != _usersByChannel.end(); channels++)
	{
		nicklist_it nicks;
		for (nicks = (*channels).second.begin(); nicks != (*channels).second.end(); nicks++){
			if((*nicks)->userId() == userId)
			{
				return;
			}
		}
	}

	//if its not then the user is gone and we need to clean up

	//remove from usersbyid
	_usersByID.erase(_usersByID.find(userId));
	
	//remove from users by nick
	_usersByNick.erase(nick);
}

void posix_ircUserDB::nickChange(std::string nick, std::string newNick)
{
	usersByNick_it it = _usersByNick.find(nick);
	ircUser* user = (*it).second;


	user->_nick = newNick;

	_usersByNick.erase(it);
	_usersByNick[newNick] = user;

}

ircUser* posix_ircUserDB::getUser(std::string nick)
{

	ircUser* user = NULL;
	usersByNick_it iter = _usersByNick.find(nick);
	if(iter != _usersByNick.end())
	{
		user = (*iter).second;
	}

	if(user == NULL)
	{
		dbLog << "getUser nick" << std::endl;
		printAllUsers();
	}
	return user;
}

ircUser* posix_ircUserDB::getUser(unsigned int ID)
{
	

	ircUser* user = NULL;
	usersByID_it iter = _usersByID.find(ID);
	if(iter != _usersByID.end())
	{
		user = (*iter).second;
	}

	if(user == NULL)
	{
		dbLog << "getUser id" << std::endl;
		printAllUsers();
	}
	return user;
}

std::vector<ircUser*> posix_ircUserDB::getChannelCurrentUsers(std::string channel)
{
	return _usersByChannel[channel];
}

std::vector<ircUser*> posix_ircUserDB::allUsers()
{
	usersByID_it iter;
	std::vector<ircUser*> users;
	for(iter = _usersByID.begin(); iter != _usersByID.end(); iter++)
	{
		users.push_back((*iter).second);
	}

	return users;
}
std::vector<ircUser*> posix_ircUserDB::onlineRegisteredUsers()
{
	registeredId_it iter;
	std::vector<ircUser*> users;
	for(iter = _registeredNicksToIDs.begin(); iter != _registeredNicksToIDs.end(); iter++)
	{
		usersByNick_it user = _usersByNick.find((*iter).first);
		if(user != _usersByNick.end())
		{
			users.push_back((*user).second);
		}
	}

	return users;

}
std::vector<ircUser*> posix_ircUserDB::allRegisteredUsers()
{
}
std::vector<ircUser*> posix_ircUserDB::authenticatedUsers()
{
	usersByID_it iter;
	std::vector<ircUser*> users;
	for(iter = _usersByID.begin(); iter != _usersByID.end(); iter++)
	{
		if((*iter).second->isAuthenticated())
		{
			users.push_back((*iter).second);
		}
	}

	return users;
}

void posix_ircUserDB::lockUsers()
{
	pthread_mutex_lock(&userLock);
}
void posix_ircUserDB::releaseUsers()
{
	pthread_mutex_unlock(&userLock);
}

void posix_ircUserDB::setRegistered(std::string nick,bool registered)
{
	ircUser* user = getUser(nick);
	
	if(user == NULL)
	{
		dbLog << "setRegistered" << std::endl;
		printAllUsers();
	}

	registeredId_it idIter = _registeredNicksToIDs.find(nick);
	if(registered && idIter != _registeredNicksToIDs.end())
	{
		user->_userId = (*idIter).second;
	}
	else if(registered)
	{
		//temp->_userId = getNextAvailableID();
		_registeredNicksToIDs[nick] = user->_userId;
		saveData();
	}
	
	//else it should have defaulted to false, but lets make sure	
}
void posix_ircUserDB::setAuthenticated(std::string nick, bool authenticated)
{
	ircUser* user = getUser(nick);

	if(user == NULL)
	{
		dbLog << "setAauthenticated" << std::endl;
		printAllUsers();
	}
	user->_isAuthenticated = authenticated;
}

//*************************************************************
//	not sure this function makes a whole lot of sense
//*************************************************************
void posix_ircUserDB::setRegistered(unsigned int id, bool registered)
{
	ircUser* user = getUser(id);
	registeredId_it idIter = _registeredNicksToIDs.find(user->nick());
	if(registered && idIter != _registeredNicksToIDs.end())
	{
		user->_userId = (*idIter).second;
	}
	else if(registered)
	{
		//temp->_userId = getNextAvailableID();
		_registeredNicksToIDs[user->nick()] = user->_userId;
		saveData();
	}
}
void posix_ircUserDB::setAuthenticated(unsigned int id, bool authenticated)
{
	ircUser* user = getUser(id);
	user->_isAuthenticated = authenticated;
}

void posix_ircUserDB::lockDB()
{
	pthread_mutex_lock(&theBigLock);
}

void posix_ircUserDB::unlockDB()
{
	pthread_mutex_unlock(&theBigLock);
}
unsigned int posix_ircUserDB::getNextAvailableID()
{

	unsigned int posId;

	/* This actually works correctly. */

	//XXX when this was a MACRO it was came back 0 >.>
//#define RAND_INV_RANGE(r) ((unsigned int) ((RAND_MAX + 1) / (r)))
	//unsigned int rand_inv_range = (unsigned int) ((RAND_MAX + 1) / RANGE);
	//do {
	    posId = (unsigned int) rand();
	//} while (posId >= RANGE * rand_inv_range);
	//posId /= rand_inv_range;

//#undef RAND_INV_RANGE


	bool exists = false;
	registeredId_it regs;
	for(regs = _registeredNicksToIDs.begin(); regs != _registeredNicksToIDs.end(); regs++)
	{
		if((*regs).second == posId)
		{
			exists = true;
			break;
		}
	}
	usersByID_it active = _usersByID.find(posId);

	if( exists || active != _usersByID.end())
	{
		return getNextAvailableID();
	}
	else
	{
		return posId;
	}
}

void posix_ircUserDB::saveData()
{
	dbLog << "posix_ircUserDB: saving data..." << std::endl;
	std::ofstream dbfile;
	dbfile.open("users.db");
	
	dbfile << _registeredNicksToIDs.size();
	dbLog << "-----Saved Data------" << std::endl;
	dbLog << "number of registered nicks: " << _registeredNicksToIDs.size() << std::endl;
	for(registeredId_it iter = _registeredNicksToIDs.begin(); iter != _registeredNicksToIDs.end(); iter++)
	{
		dbfile << iter->first;
		dbLog << "nick: " << iter->first << std::endl;
		dbfile << iter->second;
		dbLog << "id: " << iter->second << std::endl << std::endl;
	}
}

void posix_ircUserDB::loadData()
{
	std::ifstream dbfile;
	dbfile.open("users.db");

	dbLog <<"posix_ircUserDB: opening database file..." << std::endl;
	if(!dbfile.good())
		return;

	size_t mapsize;
	dbfile >> mapsize;

	dbLog <<"posix_ircUserDB: there are "<< mapsize << " registered nicks" << std::endl;

	for (size_t i = 0; i < mapsize; ++i)
	{
		std::string nick;
		dbfile >> nick;
		unsigned int id;
		dbfile >> id;
		_registeredNicksToIDs[nick] = id;

//		dbLog << "posix_ircUserDB: loaded nick: " << nick << " id: " << id << std::endl;

	}
}




void posix_ircUserDB::printAllUsers()
{
	std::stringstream users;

	users << std::endl << "-----DATABURRS DUMP-----" << std::endl;

	usersByNick_it nicks;

	for(nicks = _usersByNick.begin(); nicks!=_usersByNick.end(); nicks++)
	{
		users << (*nicks).first << "\t";

		if((*nicks).second == NULL)
		{
			users << "NULL\tNULL" << std::endl;
		}
		else
		{
			ircUser* temp =  (*nicks).second;
			users << temp->userId() << "\t";
			if(temp->isAuthenticated())
			{
				users << "true" << std::endl;
			}
			else
			{
				users << "false" <<std::endl;
			}
		}
	}


	users << std::endl << std::endl;
	usersByID_it ids;

	for(ids = _usersByID.begin(); ids!=_usersByID.end(); ids++)
	{
		users << (*ids).first << "\t";

		if((*ids).second == NULL)
		{
			users << "NULL\tNULL" << std::endl;
		}
		else
		{
			ircUser* temp =  (*ids).second;
			users << temp->nick() << "\t";
			if(temp->isAuthenticated())
			{
				users << "true" << std::endl;
			}
			else
			{
				users << "false" <<std::endl;
			}
		}
	}

	dbLog << users.str();
}

void posix_ircUserDB::printChannelUsers(std::string channel)
{
	std::vector<ircUser*> users =  _usersByChannel[channel];


	std::vector<ircUser*>::iterator channelUser;

	dbLog << "-----Users in channel " << channel << "-----" << std::endl;
	for(channelUser = users.begin(); channelUser != users.end(); channelUser++)
	{
		if((*channelUser) != NULL)
		{
			if((*channelUser)->_nick.size() > 32)
			{
				dbLog << "something's really wrong here, nick is more than 32 chars: " << (*channelUser)->_nick.size() << std::endl;
				//this is temporary I want to see when this case happens.
				exit(1);
			}
			dbLog << (*channelUser)->nick() << std::endl;
		}
		else 
			dbLog << "NULL" << std::endl;
	}
}
