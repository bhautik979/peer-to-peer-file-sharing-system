#include <arpa/inet.h> // for inet_pton()
#include <condition_variable>
#include <cstring>    // for memset()
#include <fcntl.h>    // For open() flags like O_RDONLY,O_WRONLY etc.
#include <functional> // For function, bind, ref, move, forward
#include <iostream>
#include <mutex>         // For mutex
#include <netinet/in.h>  // for sockaddr_in, htons(), htonl()
#include <openssl/sha.h> // For SHA1_Init(), SHA1_Update(), SHA1_Final()
#include <queue>         // For queue
#include <sys/socket.h>  // for socket(), bind(), connect(), listen(), accept()
#include <sys/stat.h>    // For stat() to get file size
#include <thread>
#include <unistd.h>      // For open(), read(), close() of file.
#include <unordered_map> // For unordered_map
#include <vector>        // For vector
#include <unordered_set> // For unordered_set
#define CHUNK_SIZE_TCP 32768
using namespace std;

class FilesStructure {
public:
  string file_name;
  string file_path;
  string sha;
  long long int total_chunks;
  long long int total_size;
  vector<string> chunks_I_have;  //vector of all chunks
  long long int no_of_chunks_I_have;   //count of chunks

  FilesStructure() {
    file_name = "";
    file_path = "";
    sha = "";
    total_chunks = 0;
    total_size = 0;
    file_path = "";
  }
};

// <sha,obj of filestructur>
unordered_map<string, FilesStructure> filesIHave;

// When file start downloading
// <sha,{group_id,file_name}>
unordered_map<string, pair<string, string>> downloadStart;
// When any one of the chunk got downloaded
// <sha,{group_id,file_name}>
unordered_map<string, pair<string, string>> downloadPending;
// When file fully downloaded
// <sha,{group_id,file_name}>
vector<pair<string, string>> downloadComplete;

class ThreadPool {
private:
  vector<thread> workers;
  queue<function<void()>> tasks;
  mutex queueMutex;
  condition_variable condition;
  bool stop = false;

public:
  ThreadPool(size_t numThreads) {  //create threads and put in pool who wait for infinite time to get task.
    for (size_t i = 0; i < numThreads; ++i) {  
      workers.emplace_back([this] {
        while (true) {
          function<void()> task;

          {
            unique_lock<mutex> lock(queueMutex);
            condition.wait(lock, [this] { return !tasks.empty() || stop; });

            if (stop && tasks.empty()) {
              return;
            }

            task = move(tasks.front());
            tasks.pop();
          }

          task();
        }
      });
    }
  }

  // Add a task to the thread pool
  template <class F> void AddTask(F &&task) { //safly add task to queue and notify to worker to execute
    {
      unique_lock<mutex> lock(queueMutex);
      tasks.emplace(forward<F>(task));
    }
    condition.notify_one();
  }

  // Wait for all tasks to complete and stop the thread pool
  void WaitAndStop() {
    {
      unique_lock<mutex> lock(queueMutex);
      stop = true;
    }
    condition.notify_all();

    for (thread &worker : workers) {
      worker.join();
    }
  }
};


void printIncomingCommandTokenized(vector<string> &command) {
  cout << endl;
  cout << "/////////////// Incoming command tokenized /////////////////////"
       << endl;
  for (auto x : command) {
    cout << x << " ";
  }
  cout << endl;
  cout << "/////////////// Incoming command tokenized /////////////////////"
       << endl;
  cout << endl;
}

string convertToWSLPath(const string &path) {
  // Check if it's a Windows-style path: starts with letter + colon + backslash
  if (path.length() >= 3 && isalpha(path[0]) && path[1] == ':' &&
      path[2] == '\\') {
    string result = "/mnt/";
    result += tolower(path[0]); // drive letter (e.g., 'C' → 'c')
    result += path.substr(2);   // skip the colon
    for (char &ch : result) {
      if (ch == '\\')
        ch = '/'; // replace \ with /
    }
    return result;
  }

  // Not a Windows path — assume it's already a Linux/WSL path
  return path;
}

vector<string> tokenize(const string &buffer) {
  vector<string> tokens;
  string temp = "";
  for (char ch : buffer) {
    if (ch == ' ' || ch == '\n') {
      if (!temp.empty()) {
        tokens.push_back(temp);
        temp = "";
      }
    } else {
      temp += ch;
    }
  }
  if (!temp.empty()) {
    tokens.push_back(temp);
  }
  return tokens;
}

bool isUserLogin(const string &user_id) {
  // Check if the user is logged in
  if (user_id == "") {
    return false;
  }
  // if user_id is not empty, the user is logged in
  return true;
}

string calculateShaofChunk(string buffer) // This function calculates the SHA-1
                                          // hash of a given chunk of data
{
  // cout << "calculating sha of chunl : "  << buffer << endl;
  // initialize, update, finalize
  string sha = "";
  SHA_CTX shaContext;
  SHA1_Init(&shaContext);

  SHA1_Update(&shaContext, buffer.c_str(), buffer.length());

  unsigned char sha1Result[SHA_DIGEST_LENGTH];
  SHA1_Final(sha1Result, &shaContext);

  for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    char hex[3];
    sprintf(hex, "%02x", sha1Result[i]);
    sha += hex;
  }
  return sha;
}

void printFileTable() {   //print table of file data structure at user side
  cout << endl;
  cout << "//////////////////////////////////// File "
          "//////////////////////////////////"
       << endl;
  for (auto x : filesIHave) {
    cout << x.first << " " << x.second.file_name << " " << x.second.file_path
         << " " << x.second.no_of_chunks_I_have << " " << x.second.total_chunks
         << " " << x.second.total_size << endl;
    cout << "          **********Chunks I have**********" << endl;
    for (const auto &chunk : x.second.chunks_I_have) {
      cout << chunk << " ";
    }
    cout << "           *********************************" << endl;
  }
  cout << "//////////////////////////////////// File "
          "//////////////////////////////////"
       << endl;
  cout << endl;
}

struct PairHash{
  size_t operator()(const pair<string,string>&a)const{
    hash<string> hash_my_string;
    return  hash_my_string(a.first) ^ hash_my_string(a.second)<<1; 
  }
};
// bool PairHash(const pair<string, string> &p) {
//   // Custom hash function for pair<string, string>
//   return hash<string>()(p.first) ^ hash<string>()(p.second);
// }
void printShowDownloads() {
  unordered_set<pair<string, string>, PairHash> pending;
  // unordered_set<pair<string,string>,PairHash> completed;
  // Taking download start and download pending and putting it in pending result
  for (auto x : downloadStart) {
    pending.insert({x.second.first, x.second.second});
  }
  for (auto x : downloadPending) {
    pending.insert({x.second.first, x.second.second});
  }

  // for download complete we can directly take it form downloadComplete vector

  cout << endl;
  cout << "//////////////////////////////////// Downloads "
          "//////////////////////////////////"
       << endl;
  for (auto x : pending) {
    cout << "[D] : " << "[ " << x.first << " ] " << x.second << endl;
  }
  for (auto x : downloadComplete) {
    cout << "[C] : " << "[ " << x.first << " ] " << x.second << endl;
  }
  cout << "//////////////////////////////////// Downloads "
          "//////////////////////////////////"
       << endl;
  cout << endl;
}

string extractFilenamefromPath(const string &filePath) {
  // Extract the filename from the given file path
  size_t lastSlash = filePath.find_last_of("/\\");
  if (lastSlash == string::npos) {
    return filePath; // No directory, return the whole path
  }
  return filePath.substr(lastSlash +
                         1); // Return the substring after the last slash
}
string calculateShaofFile(
    string filePath) // This function calculates the SHA-1 hash of a file
{
  // initialize, update, finalize
  string sha = "";
  int fileDescriptor = open(filePath.c_str(), O_RDONLY);

  if (fileDescriptor < 0) {
    cerr << "Error opening file" << endl;
    return sha;
  }

  SHA_CTX shaContext;
  SHA1_Init(&shaContext);

  unsigned char buffer[512 * 1024];
  ssize_t bytesRead;

  while ((bytesRead = read(fileDescriptor, buffer, sizeof(buffer)) > 0)) {
    SHA1_Update(&shaContext, buffer, bytesRead);
  }

  close(fileDescriptor);

  unsigned char sha1Result[SHA_DIGEST_LENGTH];
  SHA1_Final(sha1Result, &shaContext);

  for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    char hex[3];
    sprintf(hex, "%02x", sha1Result[i]);
    sha += hex;
  }
  return sha;
}


string inComingClientRequest(int clientSocket) {
    char buffer[512 * 1024];
  int bytesReceived;

  // cout<<"I get connection Request from client"<<endl;
  // cout<<"Sending message to client: "<<endl;
  send(clientSocket,
       "You are connected to tracker, I am here to other client you", 60, 0);
  // while (true) {
    bytesReceived = read(clientSocket, buffer, sizeof(buffer));
    // cout<<"I got request from client: "<<buffer<<endl;
    if(bytesReceived <= 0) {
      cerr << "Client disconnected or error occurred" << endl;
      // break; // Exit the loop if client disconnects or an error occurs
      return "Not Get Any Data From Clinent";
    }
    vector<string> command
     = tokenize(buffer);

    // Printing incoming command tokenized
    printIncomingCommandTokenized(command);

    if (command.size() == 0) {
      cerr << "Invalid Command" << endl;
      send(clientSocket, "Invalid Command", 1024, 0);
      // continue;
      return "Invalid Command";
    }
    // Give how much chunks I have
    else if (command[0] == "give_file_chunks_info") {   //give_file_chunks_info <sha>
      // Incoming request give_file_chunks_info sha
      if (command.size() < 2 || command[1] == "") {
        cout << "Sorry! unable to entertain request" << endl;
        send(clientSocket, "Sorry! unable to entertain request", 1024, 0);
      } else {
        if (filesIHave.find(command[1]) != filesIHave.end()) {
          string response = "success ";
          int len = filesIHave[command[1]].no_of_chunks_I_have;
          // cout << "len " << len << endl;

          //copy sha of file i have
          for (int i = 0; i < len; i++) {
            if (filesIHave[command[1]].chunks_I_have[i] != "")
              response += to_string(i) + " ";
          }

          //response: sucsess <number> <number> <number> <number>
          // cout << "response sending to : " << response << endl;
          send(clientSocket, response.c_str(), 512 * 1024, 0);
        } else {
          cout << "Sorry! I don't have this file" << endl;
          send(clientSocket, "Sorry! I don't have this file", 1024, 0);
        }
      }

      return "Successfully sent file chunks info";
    }
    else if(command[0]== "give_chunk") { //incoming command: give_chunk <sha> <chunk_no>
      // Incoming request give_chunk sha chunk_no
      if (command.size() < 3 || command[1] == "" || command[2] == "") {
        cout << "Sorry! unable to entertain request" << endl;
        send(clientSocket, "Sorry! unable to entertain request", 1024, 0);
      } else { //Send Chunk Size, Chunk in 32KB parts, Send Sha of Chunk
        if (filesIHave.find(command[1]) != filesIHave.end()) {
          char buffer1[512 * 1024];
          long int CHUNK_SIZE = 512 * 1024;
          int bReceived = 0;
          string res1 = "";
          int readFileDescriptor =
              open(filesIHave[command[1]].file_path.c_str(), O_RDONLY);
          if (readFileDescriptor < 0) {
            cerr << "at peer Error while opening input file " << endl;
          }else{
            // cout<<"Got request for file from client and open file successfully"<<endl;
            lseek(readFileDescriptor, stoi(command[2]) * CHUNK_SIZE, SEEK_SET);
            bzero(buffer1, sizeof(buffer1));
            int readFileCount = read(readFileDescriptor, buffer1, CHUNK_SIZE);
            string res(buffer1, readFileCount);
            res1 = res; //convert buffer to string
            //Send Expected Chunk Size It May Different for Last Chunk
            // cout<<"Sending chunk size: "<<readFileCount<<endl;
            send(clientSocket, (to_string(readFileCount)).c_str(), 1024, 0);  //send size of data to be send
            // cout<<"Send file size successfully now sending file in 32KB chunks"<<endl;
            int totalLength = readFileCount;
            int alreadyRead = 0;
            int readAtOnce = CHUNK_SIZE_TCP;
            int leftToRead = readFileCount;
            int i = 0;
            while (leftToRead > 0) {    //send 512 KB chunk into 32 KB smaller parts. which is good in TCP connection
              readAtOnce = (readAtOnce < leftToRead) ? readAtOnce : leftToRead;
              string data = res.substr(i * CHUNK_SIZE_TCP, readAtOnce);
               int a = send(clientSocket, data.c_str(), data.length(), 0);
              leftToRead -= data.length();
              i++;
            }
            // cout<<"All chunks sent successfully"<<endl;

          }
          bzero(buffer1, sizeof(buffer1));

          //Receive file ack
          // cout<<"Receiving ack from client"<<endl;
          bReceived = recv(clientSocket, buffer1, sizeof(buffer1), 0);
          bzero(buffer1, sizeof(buffer1));
          // cout<<"Ack received from client and send sha to client"<<endl;
          string response = calculateShaofChunk(res1);  //Send Sha
          int a = send(clientSocket, response.c_str(), response.length(), 0);
          // cout<< "Sent sha to client successfully: "<< endl;
        } else {
          cout << "Sorry! I don't have this file" << endl;
          send(clientSocket, "Sorry! I don't have this file", 1024, 0);
        }
      }
      return "Successfully sent chunk data";
    }
    else if(command[0]=="exit"){
      return "exit"; // Exit command from client
    } 
    else {
      cerr << "Invalid Command" << endl;
      send(clientSocket, "Invalid Command", 1024, 0);
    }
  // }
  close(clientSocket);
}
// Download saperate chunk from another peer //full path with file name
/*Retry Loop: Attempts to download a chunk up to 5 times if failures occur (connection issues, SHA mismatch)
Random Peer Selection: Randomly selects a peer from available peers who have the requested chunk to balance load
TCP Connection: Establishes socket connection with selected peer using IP and port from chunkInfoTable
Chunk Request Protocol: Sends "give_chunk" command with file SHA and chunk number to peer
Size-First Transfer: Receives expected chunk size first, then downloads chunk data in 32KB packets
Timeout Protection: Uses select() with 6-second timeout to prevent blocking if peer disconnects during transfer
SHA Verification: Calculates SHA of received data and verifies it matches peer's SHA to ensure data integrity
Thread-Safe Update: On success, locks mutex and updates filesIHave table with chunk info
Tracker Notification: Informs tracker when first chunk is downloaded so this client becomes available as a seeder
Automatic Retry: On SHA mismatch or connection failure, clears buffer and retries with potentially different peer until success or max attempts reached*/
string downloadWholeChunkFromPeer(
    int chunkNo, vector<vector<pair<string, string>>> &chunkInfoTable,
    string sha, string fileName, string destination_path,
    long long int no_of_chunks, long long int file_size,
    std::mutex &queueAndFileTableMutex,const char *trackerServerIp,
    int trackerServerPort, string user_id, string group_id) {
  // char resultBuffer[512*1024];
   string resultBuffer = "";
   bool flagSuccess=false;
   int noOfAttempt=0;
  //  cout<<"Downloading start by Thread"<<i<<endl;
  //create connection with other client
  while(!flagSuccess){

    noOfAttempt++;
    if(noOfAttempt > 5) {
      cerr << "Failed to connect to peer after multiple attempts." << endl;
      return resultBuffer; // Return empty result if all attempts fail
    }
    //flow: 1)get chunk size 2) get chunk data 3)get sha and verify 
    int noOfPeers = chunkInfoTable[chunkNo].size();
    // If no peer has this chunk then just break the loop
    // if (noOfPeers == 0) {
    //   flagSuccess = true;
    //   break;
    // }

    int peerSelectionIndex =
        rand() % chunkInfoTable[chunkNo].size(); // Select peer index  randomely
        cout<<"chunk no:"<<chunkNo<<" "<<"size:"<<chunkInfoTable[chunkNo].size()<<" "<<"selecting index:"<<peerSelectionIndex<<endl;
    string peerIp = chunkInfoTable[chunkNo][peerSelectionIndex].first;
    int peerPort = stoi(chunkInfoTable[chunkNo][peerSelectionIndex].second);

    // cout << "Asking to peer : " << peerIp << " PORT : " << peerPort
        //  << "Chunk no : " << chunkNo << endl;

    int peerserverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (peerserverSocketfd == -1) {
      cerr << "Socket connection failed Asking to peer : " << peerIp
           << " PORT : " << peerPort << "Chunk no : " << chunkNo
           << endl;
      continue;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(peerPort);

    if (inet_pton(AF_INET, peerIp.c_str(), &serverAddr.sin_addr) <= 0) {
      close(peerserverSocketfd);
      cerr << "Invalid address Asking to peer : " << peerIp
           << " PORT : " << peerPort << "Chunk no : " << chunkNo
           << endl;
      continue;
    }

    if (connect(peerserverSocketfd, (struct sockaddr *)&serverAddr,
                sizeof(serverAddr)) == -1) {
      close(peerserverSocketfd);
      cerr << "Connection failled Asking to peer : " << peerIp
           << " PORT : " << peerPort << "Chunk no : " << chunkNo
           << endl;
      continue;
    }

    cout << "Connected with peer : " << peerIp << " PORT : " << peerPort
         << "Chunk no : " << chunkNo << endl;

    int bytesReceived = 0;
    char buffer[512 * 1024];
    string chunk_sha_from_peer = "";
    // connection message
    bytesReceived = recv(peerserverSocketfd, buffer, sizeof(buffer), 0);
    // cout <<"Printing Buffer recived first time from server:"<< buffer << endl;
    string arguments = "";
    arguments += "give_chunk";
    arguments += " " + sha;
    arguments += " " + to_string(chunkNo);

    //Ask For Chunk Sha
    // cout<<"Sending request to peer: "<<arguments<<endl;
    send(peerserverSocketfd, arguments.c_str(), arguments.length(), 0);  //send 
    // bzero(buffer, sizeof(buffer));
    memset(buffer, 0, sizeof(buffer));
    cout<<"Waiting for chunk size from peer: "<<peerIp<<" PORT : "<<peerPort<<endl;
    //Get Chunk Size
    bytesReceived = recv(peerserverSocketfd, buffer, sizeof(buffer), 0);
    cout<< "Chunk size received from peer: " << buffer << endl;
    string lengthOfChunk(buffer);
    
    // Check if peer returned error message instead of chunk size
    if (lengthOfChunk.find("Sorry") != string::npos || lengthOfChunk.find("unable") != string::npos) {
      cerr << "Peer doesn't have this file/chunk: " << lengthOfChunk << endl;
      close(peerserverSocketfd);
      continue; // Retry with different peer
    }
    
    // long long int expectedSize = lengthOfChunk.size();
    long long int expectedSize = 0;
    try {
      expectedSize = stoll(lengthOfChunk);
    } catch (const exception& e) {
      cerr << "Invalid chunk size received from peer: " << lengthOfChunk << endl;
      close(peerserverSocketfd);
      continue; // Retry with different peer
    }

    char newBuffer[CHUNK_SIZE_TCP];    //we already have buffer then why use this new buffer 
    // int readAtOnce = CHUNK_SIZE_TCP;
    // long long int leftToRead = expectedSize;
    // int k = 0;
    // resultBuffer = "";  //if previous data not get properly then clear it
    // while(true){
    //   if(leftToRead <= 0) {
    //     break; // If nothing left to read then break
    //   }

    // int readAtOnce = CHUNK_SIZE_TCP;
    int leftToRead = expectedSize;
    int k = 0;
    resultBuffer = "";
    //Scenario: if server get disconnected then client get blocked at recv function so use fd_set and select to add a timer
    cout<<"Start receiving chunks from client"<<endl;
    while (true) {
      if (leftToRead <= 0) {
        break;
      }
      fd_set readfds;
      struct timeval timeout;

      FD_ZERO(&readfds);
      FD_SET(peerserverSocketfd, &readfds);

      timeout.tv_sec = 6; // Set a 6-second timeout
      timeout.tv_usec = 0;

      int selectResult =
          select(peerserverSocketfd + 1, &readfds, NULL, NULL, &timeout);

      if (selectResult < 0) {
        // Handle select error
        break;
      } else if (selectResult == 0) {
        // Timeout reached, no data received within the timeout
        break;
      }
      bzero(newBuffer, sizeof(newBuffer));
      cout<<"Waiting for data from peer: " << peerIp << " PORT : "<<peerPort<<endl;
      bytesReceived = recv(peerserverSocketfd, newBuffer, sizeof(newBuffer), 0);
      cout<<"Received bytes: "<<bytesReceived<<endl;
      if(bytesReceived>0){
        // string result=newBuffer;
        // resultBuffer += result; // Append the received data to the result buffer
        resultBuffer.append(newBuffer, bytesReceived);
        leftToRead -= bytesReceived; // Decrease the left to read size
      }
      if(bytesReceived <= 0) {
        // If no data received, break the loop
        // cerr << "No data received from peer: " << peerIp << " PORT : "
        //      << peerPort << endl;
        break;
      }

    }

    // calculate sha of chunk
// cout<<"Sending ack to peer: " << peerIp << " PORT : "
        //  << peerPort <<"Ack"<< endl;
    string shaChunk = calculateShaofChunk(resultBuffer);
    // Got sha message ack
    send(peerserverSocketfd, "got file", 1024, 0);
    // bzero(buffer, sizeof(buffer));
    memset(buffer, 0, sizeof(buffer));

    cout<<"Waiting for sha from peer: " << peerIp << " PORT : "
         << peerPort << endl;
    bytesReceived = recv(peerserverSocketfd, buffer, 40, 0);  //recive sha from peer 
    cout<<"Sha received from peer: " << buffer << endl;
    close(peerserverSocketfd);
    string recivedSha= buffer;
    if(recivedSha == shaChunk) {
      std::lock_guard<std::mutex> lock(queueAndFileTableMutex);
      flagSuccess = true; // If sha matched then set flag to true

       if (downloadStart.find(sha) != downloadStart.end()) {  //Ckeck is file is already downloading
        // Put lock here
        downloadStart.erase(sha);
        downloadPending[sha] = {group_id, fileName};
      }

      //Add File if not exist and if exist then update chunk info
      if(filesIHave.find(sha) == filesIHave.end()) {
        vector<string> shaEveryChunk(no_of_chunks, "");
          shaEveryChunk[chunkNo] = shaChunk;
          FilesStructure fs;
          fs.file_path = destination_path;
          fs.file_name = fileName;
          fs.sha = sha;
          fs.total_chunks = no_of_chunks;
          fs.total_size = file_size;
          fs.chunks_I_have = shaEveryChunk;
          fs.no_of_chunks_I_have = 1;
          filesIHave[fs.sha] = fs;

        //Inform to tracker that i have new file
        int trackerserverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
        if (trackerserverSocketfd == -1) {
          perror("Socket creation failed with peer");
          // return;
        } else {
          sockaddr_in serverAddr;
          serverAddr.sin_family = AF_INET;
          serverAddr.sin_port = htons(trackerServerPort);

          if (inet_pton(AF_INET, trackerServerIp, &serverAddr.sin_addr) <= 0) {
            perror("Invalid address");
            close(trackerserverSocketfd);
            // return;
          } else {
            if (connect(trackerserverSocketfd, (struct sockaddr *)&serverAddr,
                        sizeof(serverAddr)) == -1) {
              perror("Connection to peer failed");
              close(trackerserverSocketfd);
              // return;
            } else {
              // update_file_table user_id group_id fileName no_of_chunks sha
              // file_size
              string arguments = "";
              cout<<"Completing First Chunk and informing to tracker"<<endl;
              arguments += "update_file_table " + user_id + " " + group_id +
                           " " + fileName + " " + to_string(no_of_chunks) +
                           " " + sha + " " + to_string(file_size);

              bytesReceived =
                  recv(trackerserverSocketfd, buffer, sizeof(buffer), 0);
              bzero(buffer, sizeof(buffer));

              send(trackerserverSocketfd, arguments.c_str(), 1024, 0);

              close(trackerserverSocketfd);
            }
          }
        }
      }else{
        cout<<"Already have chunk and updating client side table"<<endl;
        filesIHave[sha].chunks_I_have[chunkNo] = shaChunk;
        filesIHave[sha].no_of_chunks_I_have++;
      }      
    } 
    
    else {
      cerr << "Chunk sha not matched with peer: " << peerIp << " PORT : "
           << peerPort << endl;
      resultBuffer = ""; // Clear the result buffer if sha not matched
    }

  }

  return resultBuffer;
}

vector<int>
connectWithClientAndGetChunkInfo(int i, string ip, string port, string sha) {
  // cout << i << " " << ip << " " << port << endl;
  cout << "To get chunk data contacting client : IP : " << ip
       << " PORT : " << port << endl;

  //<chunk_no>
  vector<int> response;

  int peerserverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
  if (peerserverSocketfd == -1) {
    perror("Socket creation failed with peer");
    return response;
  }

  int peerPort = stoi(port);

  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(peerPort);

  if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
    perror("Invalid address");
    close(peerserverSocketfd);
    return response;
  }

  if (connect(peerserverSocketfd, (struct sockaddr *)&serverAddr,
              sizeof(serverAddr)) == -1) {
    perror("Connection to peer failed");
    close(peerserverSocketfd);
    return response;
  }

  // cout << "to recive data i am connected with client : IP : " << ip
      //  << " PORT : " << port << endl;

  int bytesReceived = 0;
  char buffer[512 * 1024];
  bytesReceived = recv(peerserverSocketfd, buffer, sizeof(buffer), 0);
  cout << buffer << endl;
  
  string arguments = "";
  arguments += "give_file_chunks_info";
  arguments += " " + sha;
  // cout<<"transfering request to peer:"<<ip<<port<<endl;
  send(peerserverSocketfd, arguments.c_str(), 1024, 0);

  bzero(buffer, sizeof(buffer));

  bytesReceived = recv(peerserverSocketfd, buffer, sizeof(buffer), 0);

  // cout << "response got from peer" <<  buffer << endl;
//response: success <number> <number> <number> <number>
  vector<string> res1 = tokenize(buffer);
  if (res1.size() > 0 && res1[0] == "success") {
    string temp = "";
    int bufferLength = bytesReceived;

    // spliting buffer command by space and extracting only chunk numbers
    for (int i = 1; i < res1.size(); i++) {
      response.push_back(stoi(res1[i]));
    }
  } else {
    cout << buffer << endl;
  }

  close(peerserverSocketfd);
  return response;
}

void downloadRequestHandler(string user_id, string commandInput,
                            vector<string> command, const char *trackerServerIp,
                            int trackerServerPort) {
  // command vector contain -> download_file <group_id> <file_name>
  // <destination_path>

  //  flow : go to tracker and get all ip:port who have file in this group
  //  tokanize all user
  //  go to client and collect chunk information
  //  go one by one client and get chink by threads
  //  write file in destination and return to main flow.

  // Task 1: go to tracker and get all client ip:port
  string fileName = command[2];
  if (fileName == "") {
    cerr << "Please give filename " << endl;
    return;
  }

  string destination_path = command[3];
  if (destination_path[1] == ':') {
    destination_path = convertToWSLPath(destination_path);
  }
  if (destination_path == "") {
    cerr << "Please enter destination path" << endl;
    return;
  }
  if (!isUserLogin(user_id)) {
    cerr << "Please do login first" << endl;
    return;
  }
  if (command.size() != 4) {
    cerr
        << "Please pass download_file <group_id> <file_name> <destination_path>"
        << endl;
    return;
  } else {
    struct stat folder;
    if (stat(destination_path.c_str(), &folder) ==
        0) { // state is use for cheking destination path is exist or not
    } else {
      cerr << "Destination path is not valid" << endl;
      return;
    }
  }

  // append file name at the end of destiantion
  if (destination_path[destination_path.length() - 1] == '/') {
    destination_path += fileName;
  } else {
    destination_path += "/" + fileName;
  }

  int trackerserverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
  if (trackerserverSocketfd == -1) {
    perror("Socket creation failed with peer");
    return;
  }

  // int peerPort = trackerServerPort;

  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(trackerServerPort);

  if (inet_pton(AF_INET, trackerServerIp, &serverAddr.sin_addr) <= 0) {
    perror("Invalid address");
    close(trackerserverSocketfd);
    return;
  }

  if (connect(trackerserverSocketfd, (struct sockaddr *)&serverAddr,
              sizeof(serverAddr)) == -1) {
    perror("Connection to peer failed");
    close(trackerserverSocketfd);
    return;
  }

  long long int bytesReceived = 0;
  char buffer[512 * 1024];

  bytesReceived = recv(trackerserverSocketfd, buffer, sizeof(buffer),
                       0); // get confirmation of connection from server
  bzero(buffer, sizeof(buffer));

  string arguments = ""; // command send to tracker  download_file <group_id>
                         // <file_name> <destination_path> <user_id>
  arguments += commandInput;
  arguments += " " + user_id;
  send(trackerserverSocketfd, arguments.c_str(), 1024, 0);

  // response format : success sha no_of_chunks size ip port ip port ip port
  bytesReceived = recv(trackerserverSocketfd, buffer, sizeof(buffer), 0);

  close(trackerserverSocketfd);

  //  Phase 2 start: connect to all peer and get chunk information
  string fileSha = "";
  long long int total_no_of_chunks = 0;
  long long int file_size = 0;
  // 2 tokanize full string
  vector<string> res1 = tokenize(buffer);
  //   cout << "Tokanize successfully" << endl;
  if (res1.size() > 0 && res1[0] == "success") {
    // cout << "Success" << endl;
    // response format : success sha no_of_chunks size ip port ip port ip port
    if (res1.size() <= 4) {
      cout << "No User have this File in this group" << endl;
    } else {
      //   cout << "Yes i got Response:";
      fileSha = res1[1];
      total_no_of_chunks = stoi(res1[2]);
      file_size = stoi(res1[3]);

      // Now we have to connect with all client who have this file
      ThreadPool pool(10);

      // who have which chunk info table
      // Index represents chunk_no, value is vector of clients {ip,port}
      vector<vector<pair<string, string>>> chunkInfoTable(total_no_of_chunks);
      std::mutex chunkInfoTableMutex;

      std::vector<std::vector<std::string>> allResults;

      for (int i = 4; i < res1.size(); i = i + 2) {
        // cout<<"Thread: "<<i-3<<" Running"<<endl;
        // request clients for file chunk information
        pool.AddTask([i, res1, fileSha, &chunkInfoTableMutex, &chunkInfoTable] {
          // Use lambda to capture and pass arguments
          vector<int> result =
              connectWithClientAndGetChunkInfo(i, res1[i], res1[i + 1],fileSha);
        {
          std::lock_guard<std::mutex> lock(chunkInfoTableMutex);
          for (auto chunkNo : result) {
            chunkInfoTable[chunkNo].push_back({res1[i], res1[i + 1]});
          }
        } // lock_guard will automatically release the lock when it goes out of

        });
      }

      pool.WaitAndStop(); // Wait for all tasks to complete
      cout<<"All thread get information of file sucsessfully"<<endl;

      for(int i = 0; i < chunkInfoTable.size(); i++){
        cout << "Chunk " << i << ": ";
        for(auto mp : chunkInfoTable[i]){
          cout << "{" << mp.first << "," << mp.second << "} ";
        }
        cout << endl;
      }

      /////////////////////Recieved who has which chunk/////////////////////

      ///////////////////// Downloading process starts////////////////////
      downloadStart[fileSha] = {command[1], fileName}; // file_sha ---> file sha, group id,

      //To do rarest piece first - sort by number of peers who have each chunk
      vector<int> chunkOrder(total_no_of_chunks);
      for(int i = 0; i < total_no_of_chunks; i++) {
        chunkOrder[i] = i;
      }
      sort(chunkOrder.begin(), chunkOrder.end(),
           [&chunkInfoTable](int a, int b) {
             return chunkInfoTable[a].size() < chunkInfoTable[b].size();
           });

      //print chunkInfoTable
      // for(auto chunkNumber: chunkInfoTable) {
      //   cout << chunkNumber.first << ":";
      //   for (auto mp : chunkNumber.second) {
      //     cout << "{" << mp.first << "," << mp.second << "} ";
      //   }
      //   cout << endl;
      // }

      //Ask for each chunk from client
      ThreadPool poolforGetFileChunk(15);

      // Creating one file :
      long int CHUNK_SIZE = 512 * 1024;

      int writeFileDescriptor =
          open(destination_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);

      if (writeFileDescriptor < 0) {
        cerr << "Error while opening destination file " << endl;
        return;
      }
      if (chmod(destination_path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        perror("Error changing file permissions");
        return;
      }
  /*Creates tasks for each chunk - Loops through chunkOrder (sorted rarest-first) and creates a download task for each chunk
  Downloads chunk in parallel - Each thread calls downloadWholeChunkFromPeer() to fetch a chunk from a peer and stores it in resultBuffer
  Thread-safe file writing - Uses writeFileDescriptorMutex to ensure only one thread writes to the file at a time (prevents corruption)
  Writes to correct position - Uses lseek() to move file pointer to actualChunkNo * CHUNK_SIZE offset, then writes the chunk data
  Parallel execution - All 15 threads (from ThreadPool) work simultaneously, downloading different chunks and writing them to their correct positions in the destination file*/
      std::mutex writeFileDescriptorMutex;
      std::mutex queueAndFileTableMutex;
      string group_id = command[1];
      for(int i = 0; i < chunkOrder.size(); i++){
        poolforGetFileChunk.AddTask([i, &chunkOrder, fileSha, &chunkInfoTable, &writeFileDescriptorMutex,
                       writeFileDescriptor, CHUNK_SIZE, fileName,
                       &queueAndFileTableMutex, destination_path,
                       total_no_of_chunks, file_size, trackerServerIp,
                       trackerServerPort, user_id, group_id]{
           // Use lambda to capture and pass arguments
          // cout << "haha" << endl;
          int actualChunkNo = chunkOrder[i];
          string resultBuffer;  //store file content of chunk
          resultBuffer = downloadWholeChunkFromPeer(
              actualChunkNo, chunkInfoTable, fileSha, fileName, destination_path,
              total_no_of_chunks, file_size, queueAndFileTableMutex,
              trackerServerIp, trackerServerPort, user_id, group_id);
          // cout << resultBuffer << "     This is I got back" << endl;
          std::lock_guard<std::mutex> lock(writeFileDescriptorMutex);
          lseek(writeFileDescriptor, actualChunkNo * CHUNK_SIZE,
                SEEK_SET);
          // cout << "Size iof result buffer at time of file writing " <<
          // resultBuffer.length() << endl;
          write(writeFileDescriptor, resultBuffer.c_str(),
                resultBuffer.length());                                   

        });
      }

      poolforGetFileChunk.WaitAndStop();
      cout << "All thread work is completed" << endl;

       // Check final sha :
      if (downloadPending.find(fileSha) != downloadPending.end()) {
        downloadPending.erase(fileSha);
      }
      string mySha = calculateShaofFile(destination_path);
      if (mySha == fileSha) {
        cout << "File Downloaded Successfully : " << destination_path << endl;
        // Putting in complete queue :
        downloadComplete.push_back({group_id, fileName});
        std::lock_guard<std::mutex> lock(queueAndFileTableMutex);
        //update local file Tables
        if (filesIHave.find(mySha) != filesIHave.end()) {
          filesIHave[fileSha].no_of_chunks_I_have =
              filesIHave[fileSha].total_chunks;

          vector<string> shaEveryChunk(total_no_of_chunks);
          int index = 0;
          char chunkOfFile[512 * 1024]; // Read 512 Kbytes at a time
          ssize_t bytesRead;

          int fileDescriptor = open(destination_path.c_str(), O_RDONLY);

          if (fileDescriptor < 0) {
            // std::cerr << "Error opening file" << std::endl;
            // continue;
          } else {
            bzero(chunkOfFile, sizeof(chunkOfFile));

            while ((bytesRead = read(fileDescriptor, chunkOfFile,
                                     sizeof(chunkOfFile)) > 0)) {
              shaEveryChunk[index] = calculateShaofChunk(chunkOfFile);
              index++;
              bzero(chunkOfFile, sizeof(chunkOfFile));
            }
            filesIHave[fileSha].chunks_I_have = shaEveryChunk;
          }

          close(fileDescriptor);
        } else {
          FilesStructure fs;
          fs.file_path = destination_path;
          fs.file_name = fileName;
          fs.sha = fileSha;
          fs.total_chunks = total_no_of_chunks;
          fs.total_size = file_size;
          fs.no_of_chunks_I_have = 1;

          // cout<<fs.no_of_chunks_I_have << "no of chunk stored" << endl;

          filesIHave[fileSha] = fs;

          filesIHave[fileSha].no_of_chunks_I_have = total_no_of_chunks;

          vector<string> shaEveryChunk(total_no_of_chunks, "");
          int index = 0;
          char chunkOfFile[512 * 1024]; // Read 512 Kbytes at a time
          ssize_t bytesRead;

          int fileDescriptor = open(destination_path.c_str(), O_RDONLY);

          if (fileDescriptor < 0) {
            // std::cerr << "Error opening file" << std::endl;
            // continue;
          } else {
            bzero(chunkOfFile, sizeof(chunkOfFile));

            while ((bytesRead = read(fileDescriptor, chunkOfFile,
                                     sizeof(chunkOfFile)) > 0)) {
              shaEveryChunk[index] = calculateShaofChunk(chunkOfFile);
              index++;
              bzero(chunkOfFile, sizeof(chunkOfFile));
            }
            filesIHave[fileSha].chunks_I_have = shaEveryChunk;
          }

          close(fileDescriptor);
        }

        //  Informing tracker that I have this file
        int trackerserverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
        if (trackerserverSocketfd == -1) {
          perror("Socket creation failed with peer");
          // return;
        } else {
          sockaddr_in serverAddr;
          serverAddr.sin_family = AF_INET;
          serverAddr.sin_port = htons(trackerServerPort);

          if (inet_pton(AF_INET, trackerServerIp, &serverAddr.sin_addr) <= 0) {
            perror("Invalid address");
            close(trackerserverSocketfd);
            // return;
          } else {
            if (connect(trackerserverSocketfd, (struct sockaddr *)&serverAddr,
                        sizeof(serverAddr)) == -1) {
              perror("Connection to peer failed");
              close(trackerserverSocketfd);
              // return;
            } else {
              // update_file_table user_id group_id fileName no_of_chunks sha
              // file_size
              string arguments = "";
              cout<<"Finally give Update to tracker"<<endl;
              arguments += "update_file_table " + user_id + " " + group_id +
                           " " + fileName + " " +
                           to_string(total_no_of_chunks) + " " + fileSha + " " +
                           to_string(file_size);

              bytesReceived =
                  recv(trackerserverSocketfd, buffer, sizeof(buffer), 0);
              bzero(buffer, sizeof(buffer));

              send(trackerserverSocketfd, arguments.c_str(), 1024, 0);

              close(trackerserverSocketfd);
            }
          }
        }

      } else {
        cout << "File" << endl;
        // cout << "File Downloading Failed,please try again : " <<
        // destination_path << endl; Putting in complete queue :
        downloadComplete.push_back({group_id, fileName + "-failed"});
        cout<<"File Downloading Failed, SHA mismatch : " << destination_path << endl;
        // downloadStart[fileSha] = fileName + "-failed";
        // remove file entry from files table

        // if(filesIHave.find(fileSha)!=filesIHave.end())
        // {
        //     filesIHave.erase(fileSha);
        // }
      }

      printFileTable();
    }
  } else {
    cout << buffer << endl;
  }
}


void connectToTracker(const char *connectToTrackerIp, int connectToTrackerPort,
                      string ownServerIp, int ownServerPort) {
  // Store user_id so it can be used in further processes
  string user_id = "";

  int serverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocketfd == -1) {
    perror("Socket creation failed");
    return;
  }

  sockaddr_in serverAddr;  //create server address structure
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port =
      htons(connectToTrackerPort); // why here we use connectToTrackerport here
                                   // we have to create for client right

  if (inet_pton(AF_INET, connectToTrackerIp, &serverAddr.sin_addr) <=  //convert server ip/port to binary
      0) { // this use to specify both ip and port
    perror("Invalid address in Tracker file");
    return;
  }

  if (connect(serverSocketfd, (struct sockaddr *)&serverAddr, 
              sizeof(serverAddr)) == -1) { //connect to server by socket
    perror("Connection to tracker failed");
    close(serverSocketfd);
    return;
  }

  char buffer[1024];
  int bytesReceived;
  bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
  cout << buffer << endl;

  // char buffer[1024];
  // int bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer) - 1, 0);
  // if (bytesReceived > 0) {
  // 	buffer[bytesReceived] = '\0';  // Null-terminate
  // 	cout << buffer << endl;
  // } else if (bytesReceived == 0) {
  // 	cout << "Connection closed by server." << endl;
  // } else {
  // 	perror("recv failed");
  // }

  while (true) {
    string commandInput;
    cout << "Enter Command:-";
    getline(cin, commandInput);
    string temp = "";
    vector<string> command = tokenize(commandInput);
    if (command.size() == 0) {
      cerr << "Invalid Command" << endl;
      continue;
    } 
    else if (command[0] == "create_user") {
      if (command.size() != 3) {
        cerr << "Please pass create_user <user_id> <passwd>" << endl;
        continue;
      }
      user_id = "";
      string arguments = "";
      arguments += commandInput;
      arguments += " " + ownServerIp;
      arguments += " " + to_string(ownServerPort);

      send(serverSocketfd, arguments.c_str(), 1024, 0);

      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      cout << buffer << endl;
    } 
    else if (command[0] == "login") {
      if (user_id != "") {
        cerr << "Please try to login for some other device,some user "
                "is already logged in log out first"
             << endl;
        continue;
      }
      if (command.size() != 3) {
        cerr << "Please pass login <user_id> <passwd>" << endl;
        continue;
      }

      string arguments = "";
      arguments += commandInput;
      arguments += " " + ownServerIp;
      arguments += " " + to_string(ownServerPort);

      send(serverSocketfd, arguments.c_str(), 1024, 0);

      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      cout << buffer << endl;
      if (strcmp(buffer, "Logged in successfully") == 0) {
        user_id = command[1];
      } else {
        user_id = "";
      }
    } 
    else if (command[0] == "create_group") {
      {
        if (!isUserLogin(user_id)) {
          cerr << "Please do login first" << endl;
          continue;
        }
        if (command.size() != 2) {
          cerr << "Please pass create_group <group_id>" << endl;
          continue;
        }

        string arguments = "";
        arguments += commandInput;
        arguments += " " + user_id;

        send(serverSocketfd, arguments.c_str(), 1024, 0);

        bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
        cout << buffer << endl;
      }
    } else if (command[0] == "join_group") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      }
      if (command.size() != 2) {
        cerr << "Please pass join_group <group_id>" << endl;
        continue;
      }

      string arguments = "";
      arguments += commandInput;
      arguments += " " + user_id;

      send(serverSocketfd, arguments.c_str(), 1024, 0);

      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      cout << buffer << endl;
    } 
    else if (command[0] == "leave_group") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      }
      if (command.size() != 2) {
        cerr << "Please pass leave_group <group_id>" << endl;
        continue;
      }

      string arguments = "";
      arguments += commandInput;
      arguments += " " + user_id;

      send(serverSocketfd, arguments.c_str(), 1024, 0);

      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      cout << buffer << endl;
    } else if (command[0] == "list_requests") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      }
      if (command.size() != 2) {
        cerr << "Please pass list_requests <group_id>" << endl;
        continue;
      }

      string arguments = "";
      arguments += commandInput;
      arguments += " " + user_id;

      send(serverSocketfd, arguments.c_str(), 1024, 0);

      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      // print vector of list requests
      // cout << buffer << endl;
      vector<string> response = tokenize(buffer);
      if (response.size() > 0 && response[0] == "success") {
        cout << "Users of pending requests : " << endl;
        int len = response.size();
        for (int i = 1; i < len; i++)
          cout << response[i] << endl;
      } else {
        cout << buffer << endl;
      }
    }
     else if (command[0] == "accept_request") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      }
      if (command.size() != 3) {
        cerr << "Please pass accept_request <group_id> <user_id>" << endl;
        continue;
      }

      string arguments = "";
      arguments += commandInput;
      arguments += " " + user_id;

      send(serverSocketfd, arguments.c_str(), 1024, 0);

      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      cout << buffer << endl;
    } else if (command[0] == "list_groups") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      }
      if (command.size() != 1) {
        cerr << "Please pass list_groups" << endl;
        continue;
      }

      // share <list_groups> <user_id>
      string arguments = "";
      arguments += commandInput;
      arguments += " " + user_id;

      send(serverSocketfd, arguments.c_str(), 1024, 0);

      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      // cout << buffer << endl;
      vector<string> response = tokenize(buffer);
      if (response.size() > 0 &&
          response[0] ==
              "success") // if response is success then print group ids
      {
        cout << "Group id of groups : " << endl;
        int len = response.size();
        for (int i = 1; i < len; i++)
          cout << response[i] << endl;
      } else {
        cout << buffer << endl; // if response is not success then
                                // print error message
      }
    }
     else if (command[0] == "upload_file") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      }
      if (command.size() != 3) {
        cerr << "Please pass upload_file <file_path> <group_id>" << endl;
        continue;
      }

      if (command[1][1] == ':') {
        command[1] = convertToWSLPath(command[1]);
      }
      // check file permission
      if (access(command[1].c_str(), F_OK) != 0) {
        cerr << "Given file does not exist" << endl;
        continue;
      }
      // calculate sha
      string sha = calculateShaofFile(command[1]);
      if (sha == "") {
        cerr << "Error while calculating sha value, please try again" << endl;
        continue;
      }
      // calculate number of chunks require
      string fileName = extractFilenamefromPath(command[1]);

      struct stat fileStat; // use to get file properties like size
      if (stat(command[1].c_str(), &fileStat) < 0) {
        cerr << "Error getting file size" << endl;
        continue;
      }
      long long int fileSize = fileStat.st_size;
      long long int chunkSize = 512 * 1024; // 512 KB
      long long int no_of_chunks = (fileSize + chunkSize - 1) / chunkSize;

      // get all chunks of file, so that one client request it directly
      vector<string> shaofEveryChunk(no_of_chunks); // store sha of every chunk
      int index = 0;
      char chunkOfFile[512 * 1024]; // Read 512 Kbytes at a time
      ssize_t bytesRead;

      int fileDescriptor = open(command[1].c_str(), O_RDONLY);

      if (fileDescriptor < 0) {
        cerr << "Error opening file" << endl;
        continue;
      }
      bzero(chunkOfFile,
            sizeof(chunkOfFile)); // fill zero to chunkOfFile to endsure
                                  // that chunk is empty

      while ((bytesRead =
                  read(fileDescriptor, chunkOfFile, sizeof(chunkOfFile)) > 0)) {
        string chunkData(chunkOfFile, bytesRead); // only actual data
        shaofEveryChunk[index] =
            calculateShaofChunk(chunkOfFile); // calculate sha of chunk
        index++;
        bzero(chunkOfFile, sizeof(chunkOfFile));
      }

      close(fileDescriptor); // close file descriptor after reading all
                             // chunks

      // send to tracker
      // Passing : upload_file <file_path> <group_id> <user_id>
      // <file_name> <sha> <no_of_chunks> <file_size>
      string arguments = "";
      arguments += commandInput;
      arguments += " " + user_id;
      arguments += " " + fileName;
      arguments += " " + sha;
      arguments += " " + to_string(no_of_chunks);
      arguments += " " + to_string(fileSize);

      send(serverSocketfd, arguments.c_str(), 1024, 0);
      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      string res = buffer;

      // if response is success from tracker then store file object in
      // filesIHav
      if (res.substr(0, 7) == "Success") {
        // store file object in filesIHave
        FilesStructure fileObject;
        fileObject.file_name = fileName;
        fileObject.file_path = command[1];
        fileObject.sha = sha;
        fileObject.total_chunks = no_of_chunks;
        fileObject.total_size = fileSize;

        for (int i = 0; i < no_of_chunks; i++) {
          fileObject.chunks_I_have.push_back(shaofEveryChunk[i]);
        }
        fileObject.no_of_chunks_I_have = no_of_chunks;

        filesIHave[sha] = fileObject; // store it in filesIHave map
        printFileTable();             // print file table
      }
      cout << buffer << endl; // print response from tracker
    } 
    else if (command[0] == "list_files") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      } else {
        if (command.size() != 2) {
          cerr << "Please pass list_files <group_id>" << endl;
          continue;
        }

        // share <list_groups> <user_id>
        string arguments = "";
        arguments += commandInput;
        arguments += " " + user_id;

        send(serverSocketfd, arguments.c_str(), 1024, 0);

        bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
        // cout << buffer << endl;
        vector<string> response = tokenize(buffer);
        if (response.size() > 0 &&
            response[0] == "success") // if response is success then
                                      // print group ids
        {
          cout << "Group id of groups : " << endl;
          int len = response.size();
          for (int i = 1; i < len; i = i + 2)
            cout << response[i] << endl;
        } else {
          cout << buffer << endl; // if response is not success then
                                  // print error message
        }
      }
    } 
    else if (command[0] == "download_file") {
      thread(downloadRequestHandler, user_id, commandInput, command,
             connectToTrackerIp, connectToTrackerPort)
          .detach(); // close connection once file get downloaded
                     // downloadRequestHandler(serverSocketfd, user_id,
                     // commandInput, command);
    }
    else if (command[0] == "logout") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      }

      string arguments = "";
      arguments += commandInput;
      arguments += " " + user_id;

      
      send(serverSocketfd, arguments.c_str(), 1024, 0);
      
      bytesReceived = recv(serverSocketfd, buffer, sizeof(buffer), 0);
      // cout << buffer << endl;
      //conver buffer to string
      string response(buffer);
      cout << response << endl;
      if(response == "Logged out successfully") {
        user_id = "";
      } 
    }
    else if (command[0] == "show_downloads") {
      if (!isUserLogin(user_id)) {
        cerr << "Please do login first" << endl;
        continue;
      }
      printShowDownloads();
    }

    else {
      cerr << "Please Enter valid command" << endl;
    }
  }
}
int main(int argc, char const *argv[]) {
  if (argc < 3) {
    cerr << "Please pass input argument in this format : ./client "
            "<IP>:<PORT> tracker_info.txt"
         << endl;
    exit(1);
  }

  // open tracker_info file
  const char *trackerInfoFileName = argv[2];
  int readTrackerInfoFileDescriptor = open(trackerInfoFileName, O_RDONLY);
  if (readTrackerInfoFileDescriptor < 0) {
    cerr << "Error while opening tracker_info file " << endl;
    exit(1);
  }

  char buffer[1024];
  int dataRead = 0;

  string temp = "";
  vector<string> trackerInfo;

  // Reading first 4 lines of tracker info file
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

  string connectToTrackerIp = trackerInfo[0];
  int connectToTrackerPort = stoi(trackerInfo[1]);

  string ipPort = argv[1]; // reading client IP
  string serverIp = "";
  int serverPort;
  temp = "";

  for (int i = 0; i < ipPort.length(); i++) {
    if (ipPort[i] == ':') {
      serverIp = temp;
      temp = "";
    } else {
      temp += ipPort[i];
    }
  }
  serverPort = stoi(temp);

  // Create server: argument whcih i give in command line is use to creating this serer, cient connect with tracker that port is decide by os
  int serverSocketfd = socket(AF_INET, SOCK_STREAM, 0);  //IPV4, TCP, default protocol
  if (serverSocketfd == -1) {
    cerr << "Socket creation failed" << endl;
    exit(1);
  }

  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(serverPort);  //port is logical number which packet consume by which process
  serverAddr.sin_addr.s_addr = INADDR_ANY; // open all IP for connection If you wanted to listen only on localhost (only local connections):serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int opt = 1;

  if (setsockopt(serverSocketfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    cerr << "setsockopt" << endl;
    exit(1);
  }

  if (bind(serverSocketfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) ==
      -1) {
    cerr << "Binding failed" << endl;
    exit(1);
  }

  if (listen(serverSocketfd, 100) == -1) { // listen on all IP and port, only 100 connection can be waiting in queue other get rejected
    cerr << "Listening failed" << endl;
    exit(1);
  }

  cout << "Listening on port " << serverPort << endl;

  //// Client connect with tracker by below function and after connecting it
  /// create seprate thread which always connected with tracker and second
  /// thread which always in litsen mode to connect with other client where
  /// this is run as a server /////
  thread(connectToTracker, connectToTrackerIp.c_str(), connectToTrackerPort,
         serverIp, serverPort)
      .detach();

  // cout<<"Jumping to while loop to accept client request"<<endl;
  while (true) {
    //  cout << "[Server] Waiting on accept..." << endl;
    sockaddr_in clientAddr;   //this is new socket use fature communication with client, older one is use for listening other client
    socklen_t clientAddrLen = sizeof(clientAddr);
    int clientSocket =
        accept(serverSocketfd, (struct sockaddr *)&clientAddr, &clientAddrLen);  //new file desctiptor providing by OS for client connection, old fd remain same to listen other client
    // cout << "[Server] Accept returned: " << clientSocket << endl;
    if (clientSocket == -1) {
      cerr << "Peer Accept failed" << endl;
      continue;
    }
    // cout<<"I got request from one client"<<endl;
    // Create a new thread to handle the client connection
    thread(inComingClientRequest, clientSocket).detach();
  }

  close(serverSocketfd);
  return 0;
}