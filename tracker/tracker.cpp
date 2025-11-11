#include <fcntl.h>		 // For open() flags like O_RDONLY,O_WRONLY etc.
#include <netinet/in.h>	 // for sockaddr_in, htons(), htonl()
#include <sys/socket.h>	 // for socket(), bind(), connect(), listen(), accept()
#include <unistd.h>		 // For open(), read(), close() of file.
#include <unordered_set>
#include <iostream>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>  // For vector
using namespace std;
struct pair_hash {	// Custom hash function for pair<string, string>
	size_t operator()(const pair<string, string> &a) const {
		return hash<string>()(a.first) ^
			   (hash<string>()(a.second)
				<< 1);	// Combine hashes of both elements
	}
};
class fileInfo {
   public:
	string file_name;
	long long int no_of_chunks;
	long long int size;
	string sha;
};

class User {
   public:
	string user_id;
	string password;
	string ip_address;
	string port;
	bool is_active;
	unordered_map<pair<string, string>, fileInfo, pair_hash>
		files;	// <file_sha, group_id> --> fileInfo object
	User() {
		user_id = "";
		password = "";
		ip_address = "";
		port = "";
		is_active = false;
		files.clear();	// Initialize files map
	}
};

class Group {
   public:
	string group_id;
	string owner_user_id;
	//<user_id , count>
	unordered_map<string, int> pending_users;
	unordered_map<string, int> accepted_users;
};

// map for all users : <user_id,User class object>
unordered_map<string, User> users;

// map for all groups : <gorup_id,Group class object>
unordered_map<string, Group> groups;

// map for all files : <fileName,fileInfo class object>
unordered_map<string, fileInfo> files;


bool isUserLogin(const string &user_id) {
	// Check if the user is logged in
	if (users.find(user_id) == users.end()) {
		cout << "Issue in isUserLogin function, user_id not found" << endl;
		return false;
	}
	// if user_id is not empty, the user is logged in
	return users[user_id].is_active;
}
vector<string> tokenize(const char *buffer) {
	vector<string> tokens;
	string temp = "";
	for (int i = 0; buffer[i] != '\0'; i++) {
		if (buffer[i] == ' ' || buffer[i] == '\n') {
			if (!temp.empty()) {
				tokens.push_back(temp);
				temp = "";
			}
		} else {
			temp += buffer[i];
		}
	}
	if (!temp.empty()) {
		tokens.push_back(temp);
	}
	return tokens;
}
void printIncomingCommandTokenized(const vector<string> &command) {
	cout << "Incoming command: ";
	for (const auto &token : command) {
		cout << token << " ";
	}
	cout << endl;
}
void inComingClientRequest(int clientSocket) {
	// For example, echo server
	char buffer[1024];
	int bytesReceived;
	send(clientSocket,
		 "You are connected to tracker, Tracker is here for serve you", 59, 0);
	while ((bytesReceived = read(clientSocket, buffer, sizeof(buffer))) > 0) {
		vector<string> command = tokenize(buffer);
		printIncomingCommandTokenized(command);
		if (command.size() == 0) {
			send(clientSocket, "Invalid command", 20, 0);
			continue;  // If no command, skip to next iteration
		} else if (command[0] == "create_user") {
			if (command.size() < 3) {
				send(clientSocket, "Invalid command format", 23, 0);
				continue;
			}
			string userId = command[1];
			string password = command[2];
			string ipAddress = command[3];
			string port = command[4];

			if (users.find(userId) != users.end()) {
				send(clientSocket, "User already exists, Plase try to login",
					 39, 0);
				continue;
			}

			User newUser;
			newUser.user_id = userId;
			newUser.password = password;
			newUser.ip_address = ipAddress;
			newUser.port = port;
			newUser.is_active = false;

			users[userId] = newUser;

			send(clientSocket, "User created successfully", 26, 0);
		} else if (command[0] == "login") {
			if (users.find(command[1]) == users.end()) {
				send(clientSocket, "User does not exist,Please register", 1024,
					 0);
				// cout << "user exist" << endl;
				continue;
			}
			if (users[command[1]].is_active) {
				// If user's port and ip got changed
				send(clientSocket, "Already logged in", 1024, 0);
				continue;
			}
			if (users[command[1]].password == command[2]) {
				users[command[1]].is_active = true;
				// If user's port and ip got changed
				if (users[command[1]].port != command[4] ||
					users[command[1]].ip_address != command[3]) {
					users[command[1]].ip_address = command[3];
					users[command[1]].port = command[4];
					// users[command[1]].files.clear();
				}
				send(clientSocket, "Logged in successfully", 1024, 0);
			}
		} else if (command[0] == "create_group") {
			Group newGroup;
			if (command.size() < 3) {
				send(clientSocket, "Invalid command format", 23, 0);
				continue;
			}

			string groupId = command[1];
			if (groups.find(groupId) != groups.end()) {
				send(clientSocket, "Group already exists", 21, 0);
				continue;
			}

			if (isUserLogin(command[2]) == false) {
				send(clientSocket, "User is not logged in", 22, 0);
				continue;
			}
			newGroup.group_id = groupId;
			newGroup.owner_user_id = command[2];
			newGroup.accepted_users[command[2]] = 1;
			groups[groupId] = newGroup;
			send(clientSocket, "Group created successfully", 1024, 0);
			continue;
		} else if (command[0] == "join_group") {
			// incoming request : join_group <group_id> user_id
			if (command.size() < 3 || command[2] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024,
					 0);
			} else {
				if (isUserLogin(command[2]) == true) {
					if (groups.find(command[1]) == groups.end()) {
						send(clientSocket,
							 "Group does not exist with given group id", 1024,
							 0);
						// continue;
					} else {
						if (groups[command[1]].accepted_users.find(
								command[2]) !=
								groups[command[1]].accepted_users.end() ||
							groups[command[1]].owner_user_id == command[2]) {
							send(clientSocket,
								 "You are already part of this group", 1024, 0);
						} else if (groups[command[1]].pending_users.find(
									   command[2]) !=
								   groups[command[1]].pending_users.end()) {
							groups[command[1]].pending_users[command[2]]++;
							send(clientSocket,
								 "Your request has already sent to "
								 "ownler,please wait",
								 1024, 0);
						} else {
							groups[command[1]].pending_users[command[2]]++;
							send(clientSocket,
								 "Your request has been sent to owner,please "
								 "wait",
								 1024, 0);
						}
					}
					// continue;
				} else {
					send(clientSocket, "User is not Login please login first",
						 1024, 0);
					// continue;
				}
			}
		} else if (command[0] == "leave_group") {
			// incoming request : leave_group <group_id> user_id
			if (command.size() < 3 || command[2] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024,
					 0);
			} else {
				if (isUserLogin(command[2]) == true) {
					if (groups.find(command[1]) == groups.end()) {
						send(clientSocket,
							 "Group does not exist with given group id", 1024,
							 0);
						// continue;
					} else {
						if (groups[command[1]].accepted_users.find(
								command[2]) !=
								groups[command[1]].accepted_users.end() ||
							groups[command[1]].pending_users.find(command[2]) !=
								groups[command[1]].pending_users.end()) {
							groups[command[1]].accepted_users.erase(command[2]);
							groups[command[1]].pending_users.erase(command[2]);
							// Remove all files uploaded by the user in this group
							for (auto it = users[command[2]].files.begin(); it != users[command[2]].files.end(); ) {
								if (it->first.second == command[1]) {  // it->first is pair<sha, group_id>, check group_id
									it = users[command[2]].files.erase(it);
								} else {
									++it;
								}
							}
							// If owner is leaving the group
							if (groups[command[1]].owner_user_id ==
								command[2]) {
								if (groups[command[1]].accepted_users.size() <=
									0) {
									groups.erase(command[1]);
								} else {
									// assign new owner of the group
									groups[command[1]].owner_user_id =
										groups[command[1]]
											.accepted_users.begin()
											->first;
									groups[command[1]].accepted_users.erase(
										groups[command[1]].owner_user_id);
								}
							}
							send(clientSocket, "Leaved from group successfully",
								 1024, 0);
						} else {
							send(clientSocket,
								 "Your are not part of this group", 1024, 0);
						}
					}
				} else {
					send(clientSocket, "User is not Login please login first",
						 1024, 0);
					continue;
				}
			}
		} else if (command[0] == "list_requests") {
			// incoming request : list_requests <group_id> user_id
			if (command.size() < 3 || command[2] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024,
					 0);
			} else {
				if (isUserLogin(command[2]) == true) {
					if (groups.find(command[1]) == groups.end()) {
						send(clientSocket,
							 "Group does not exist with given group id", 1024,
							 0);
						// continue;
					} else {
						if (groups[command[1]].owner_user_id == command[2]) {
							// add success to notify client that this is valid
							// response not any error
							string response = "success ";
							for (auto ele : groups[command[1]].pending_users) {
								response += ele.first + " ";
							}
							send(clientSocket, response.c_str(), 1024, 0);
						} else {
							send(clientSocket,
								 "Your are not owner of this group", 1024, 0);
						}
					}
					// continue;
				} else {
					send(clientSocket, "User is not Login please login first",
						 1024, 0);
					// continue;
				}
			}
		} else if (command[0] == "accept_request") {
			// incoming request : accept_request <group_id> <user_id>
			// owner_user_id
			if (command.size() < 4 || command[2] == "" || command[3] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024,
					 0);
			} else {
				if (isUserLogin(command[3]) == true) {
					if (groups.find(command[1]) == groups.end()) {
						send(clientSocket,
							 "Group does not exist with given group id", 1024,
							 0);
						// continue;
					} else {
						if (groups[command[1]].owner_user_id == command[3]) {
							if (groups[command[1]].pending_users.find(
									command[2]) !=
								groups[command[1]].pending_users.end()) {
								groups[command[1]].accepted_users[command[2]] =
									groups[command[1]]
										.pending_users[command[2]];
								groups[command[1]].pending_users.erase(
									command[2]);
								send(clientSocket,
									 "Request accepted successfully", 1024, 0);
							} else {
								send(clientSocket,
									 "No pending request found for this user",
									 1024, 0);
							}
						} else {
							send(clientSocket,
								 "Your are not owner of this group", 1024, 0);
						}
					}
					// continue;
				} else {
					send(clientSocket, "User is not Login please login first",
						 1024, 0);
					// continue;
				}
			}
		} else if (command[0] == "list_groups") {
			// incoming request : list_groups user_id
			if (command.size() < 2 || command[1] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024,
					 0);
			} else {
				if (isUserLogin(command[1]) == true) {
					string response = "success ";
					for (auto &group : groups) {
						response += group.first + " ";
					}
					send(clientSocket, response.c_str(), 1024, 0);
				} else {
					send(clientSocket, "User is not Login please login first",
						 1024, 0);
				}
			}
		} else if (command[0] == "upload_file") {
			// incoming request : upload_file <file_path> <group_id> <user_id>
			// <file_name> <sha> <no_of_chunks> <file_size>
			if (command.size() < 8 || command[1] == "" || command[2] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024,
					 0);
			} else {
				if (isUserLogin(command[3]) == true) {
					if (groups.find(command[2]) == groups.end()) {
						send(clientSocket,
							 "Group does not exist with given group id", 1024,
							 0);
					} else {
						if (groups[command[2]].accepted_users.find(
								command[3]) !=
								groups[command[2]].accepted_users.end() ||
							groups[command[2]].owner_user_id == command[3]) {
							// Create fileInfo object
							fileInfo newFile;
							newFile.file_name = command[4];
							newFile.sha = command[5];
							newFile.no_of_chunks = stoll(command[6]);
							newFile.size = stoll(command[7]);
							// Store file info in the map
							files[command[4]] = newFile;
							// Store file info in the user's files map
							users[command[3]].files[{command[5], command[2]}] =
								newFile;
							string response = "Success, File uploaded";
							send(clientSocket, response.c_str(), 1024, 0);
						} else {
							send(clientSocket,
								 "You are not part of this group, please "
								 "join group first",
								 1024, 0);
						}
					}
				} else {
					send(clientSocket, "User is not Login please login first",
						 1024, 0);
				}
			}
		}else if (command[0] == "update_file_table") {  //update_file_table <user_id> <group_id> <fileName> <no_of_chunk> <sha> <file_size>
			// cout << "inside update file entry" << endl;
			string file_name = command[3];
			if (files.find(file_name) == files.end()) {
				fileInfo f1;
				f1.file_name = file_name;
				f1.no_of_chunks = stoll(command[4]);  //total chunks
				f1.size = stoll(command[6]);
				f1.sha = command[5];

				files[command[3]] = f1;
			}

			users[command[1]].files[{command[5], command[2]}] = files[file_name]; //it might possible that one person have file in different group can download from other group so tracker must know that in this group this person have this file

		}
		else if (command[0] == "list_files") {
			// incoming request : list_files <group_id> <user_id>
			if (command.size() < 3 || command[1] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024,
					 0);
			} else if (isUserLogin(command[2]) == true) {
				if (groups.find(command[1]) == groups.end()) {
					send(clientSocket,
						 "Group does not exist with given group id", 1024, 0);
				} else {
					if (groups[command[1]].accepted_users.find(command[2]) !=
							groups[command[1]].accepted_users.end() ||
						groups[command[1]].owner_user_id == command[2]) {
						string response = "success ";

						// set which contain all user for that group
						set<string> usersInGroup;
						// set which contain all files for that group
						set<pair<string, string>> filesInGroup;
						// Iterate through all users and their files
						for (auto it : groups[command[1]].accepted_users) {
							if (users[it.first].is_active == true) {
								usersInGroup.insert(it.first);
							}
						}
						for (auto it : usersInGroup) {
							for (auto fileIt : users[it].files) {
								if (fileIt.first.second == command[1]) {
									filesInGroup.insert(
										{fileIt.second.file_name,
										 fileIt.second.sha});
								}
							}
						}
						for (auto it : filesInGroup) {
							response += it.first + " " + it.second + " ";
						}
						send(clientSocket, response.c_str(), 1024, 0);
					} else {
						send(clientSocket,
							 "You are not part of this group, please "
							 "join group first",
							 1024, 0);
					}
				}
			} else {
				send(clientSocket, "User is not Login please login first", 1024,
					 0);
			}
		} else if (command[0] == "download_file") {
			// incoming request : download_file <group_id> <file_name>
			// <destination_path> <user_id>
			if (command.size() < 5 || command[4] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024,
					 0);
			} else if (isUserLogin(command[4]) == true) {
				if (groups.find(command[1]) == groups.end()) {
					send(clientSocket,
						 "Group does not exist with given group id", 1024, 0);
				}else if(files.find(command[2]) == files.end()){
					send(clientSocket, "File does not exist",
						 1024, 0);
					continue;
				} 
				else {
					if (groups[command[1]].accepted_users.find(command[4]) !=
							groups[command[1]].accepted_users.end() ||
						groups[command[1]].owner_user_id == command[4]) {
						// get list of all active user in that group
						vector<string> activeUsers;
						for (auto it : groups[command[1]].accepted_users) {
							if (users[it.first].is_active == true) {
								activeUsers.push_back(it.first);
							}
						}
						cout << "Active users in group " << command[1] << ": ";
						for (auto it : activeUsers) {
							cout << it << " ";
						}
						unordered_set<pair<string, string>,pair_hash> responseUsers;
						for (auto it : activeUsers) {
							// send file info to all active users in that group
							if (users[it].files.find(
									{files[command[2]].sha, command[1]}) !=
									users[it].files.end() &&
								users[it].is_active == true) {
								responseUsers.insert(
									{users[it].ip_address, users[it].port});  //to avoide duplicate IP:PORT combination
							}
						}
						if (responseUsers.size() <= 0) {
							send(clientSocket,
								 "No file found in this group with given name",
								 1024, 0);
							continue;
						}
						string response =
							"success";	// sucsess, file_sha, no_of_chunks,
										// size, all(user_ip, user_port)
						response += " " + files[command[2]].sha;
						response +=
							" " + to_string(files[command[2]].no_of_chunks);
						response +=
							" " + to_string(files[command[2]].size) + " ";

						for (auto ele : responseUsers) {
							response += ele.first + " " + ele.second + " ";
						}
						cout <<endl<< "response:" << response << endl;
						// response format : success sha no_of_chunks size ip port ip port
						send(clientSocket, response.c_str(), 512 * 1024, 0);
					} else {
						send(clientSocket,
							 "You are not part of this group, please "
							 "join group first",
							 1024, 0);
					}
				}
			} else {
				send(clientSocket, "User is not Login please login first", 1024,
					 0);
			}
		}
		else if(command[0]=="logout"){
			// incoming request : logout <user_id>
			if(command.size() < 2 || command[1] == "") {
				send(clientSocket, "Sorry! unable to entertain request", 1024, 0);
			} else if (isUserLogin(command[1]) == true) {
				users[command[1]].is_active = false;  // set user as inactive
				send(clientSocket, "Logged out successfully", 1024, 0);
			}
			else{
				send(clientSocket, "Not any User is Login please login first", 1024,
					 0);
			}
		}
		else {
			send(clientSocket, "Invalid Credentials", 1024, 0);
		}
	}

	close(clientSocket);  // Close the client socket after handling the
						  // request
}
int main(int argc, char const *argv[]) {
	// User a;
	// a.user_id = "a";
	// a.password = "1";
	// a.ip_address = "127.0.0.1";
	// a.port = "8001";
	// a.is_active = false;

	// User b;
	// a.user_id = "b";
	// a.password = "1";
	// a.ip_address = "127.0.0.1";
	// a.port = "8002";
	// a.is_active = false;

	// User c;
	// a.user_id = "c";
	// a.password = "1";
	// a.ip_address = "127.0.0.1";
	// a.port = "8003";
	// a.is_active = false;

	// User d;
	// a.user_id = "d";
	// a.password = "1";
	// a.ip_address = "127.0.0.1";
	// a.port = "8004";
	// a.is_active = false;
	
	// getting user input
	if (argc < 3) {
		cerr << "Provide Tracker file name and Tracker number" << endl;
		return 1;
	}

	// Reading tracker_info file
	const char *trackerInfoFileName = argv[1];
	int readTrackerInfoFileDescriptor = open(trackerInfoFileName, O_RDONLY);
	if (readTrackerInfoFileDescriptor < 0) {
		cerr << "Error while opening tracker_info file " << endl;
		exit(1);
	}

	// Getting track no from user argument
	int trackerNo = atoi(argv[2]);

	if (trackerNo > 2 || trackerNo < 1) {
		cerr << "Please select tracker no from 1 and 2" << endl;
		close(readTrackerInfoFileDescriptor);
		exit(1);
	}

	char buffer[1024];
	int dataRead = 0;

	string temp = "";
	vector<string> trackerInfo;

	// Reading first 4 lines
	dataRead = read(readTrackerInfoFileDescriptor, buffer, sizeof(buffer));
	for (int i = 0; i < dataRead; i++) {
		if (buffer[i] == '\n') {
			trackerInfo.push_back(temp);
			temp = "";
		} else {
			temp += buffer[i];
		}
	}

	// close tracker_info file
	close(readTrackerInfoFileDescriptor);
	if (trackerInfo.size() < 4) {
		cerr << "Tracker_info file does not have corrct data" << endl;
		exit(1);
	}

	string serverIp = trackerInfo[((trackerNo - 1) * 2)];
	int serverPort = stoi(trackerInfo[((trackerNo - 1) * 2) + 1]);

	// Create server
	int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == -1) {
		cerr << "Socket creation failed" << endl;
		exit(1);
	}

	sockaddr_in serverAddr;	 // server's address structure (sockaddr_in)
							 // that will be used when binding the socket.
	serverAddr.sin_family =
		AF_INET;  // Specifies that the socket will use IPv4 addresses.
	serverAddr.sin_port = htons(serverPort);  // Sets the port number on which
											  // the server will listen.
	serverAddr.sin_addr.s_addr =
		INADDR_ANY;	 // This tells the socket to accept connections on
					 // any network interface (e.g., Ethernet, Wi-Fi,
					 // localhost).

	int opt = 1;

	if (setsockopt(
			serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
			sizeof(opt))) {	 // this will set socket options to allow reuse
							 // of the address and port. SO_REUSEADDR
							 // allows the socket to bind to an address
							 // that is already in use, and SO_REUSEPORT
							 // allows multiple sockets to listen on the
							 // same port for more then one server.
		cerr << "setsockopt" << endl;
		exit(1);
	}

	if (bind(serverSocket, (struct sockaddr *)&serverAddr,
			 sizeof(serverAddr)) ==
		-1) {  // Explicitely assign the address and port
		cerr << "Binding failed" << endl;
		exit(1);
	}

	if (listen(serverSocket, 50) == -1) {  // Listen for incoming connections
		cerr << "Listening failed" << endl;
		exit(1);
	}

	cout << "Tracker " << trackerNo << " is listening on port " << serverPort
		 << endl;

	while (true) {
		sockaddr_in clientAddr;	 // structure to store the address info
								 // of the client that connects
		socklen_t clientAddrLen = sizeof(clientAddr);
		int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr,
								  &clientAddrLen);

		if (clientSocket == -1) {
			cerr << "Tracker Accept failed" << endl;
			continue;
		}
		// cout<<"Connection accepted from tracker client"<<endl;
		// Create a new thread to handle the client connection
		thread(inComingClientRequest, clientSocket).detach();
	}

	close(serverSocket);

	return 0;
}
