# Distributed File Sharing System

## Data Structures Used :
### Tracker side data structures : 
#### File Storage :
```c
 class fileInfo{
    string file_name;           // File name
    long long int no_of_chunks; // total no of chunnks file have
    long long int size;         // size of file
    string sha;                 // sha of file
};

// map for all files : <File name,fileInfo obj>
unordered_map<string,fileInfo> files;
```

#### User Storage :
```c
class User{
    string user_id;              // User id of user
    string password;             // Password
    string ip_address;           // Ip address
    string port;                 // Port
    bool is_active;              // To check user is logged in or not
    //<{sha,group_id},fileName>
    unordered_map<pair<string,string>,string,PairHash> files; //file list that user has (along with group id)
    //why sha:- to counter same file name with different content, group_id:- one user have same file in multiple groups, filename:- to identify file name.
};

// map for all users : <user_id,User class object>
unordered_map<string,User> users;
```

#### Group Storage :
```c
class Group{
    string group_id;                // Group id
    string owner_user_id;           // admin of group
    //<user_id , count>             /////////////////what is use of this ??
    unordered_map<string,int> pending_users;    // User list with pending requests
    unordered_map<string,int> accepted_users;   // User list which are accepted by admin
};

// map for all groups : <gorup_id,Group class object>
unordered_map<string,Group> groups;
```

### Peer side data structures :


#### File Storage :
```c
class FilesStructure{
    string file_name;               // File Name
    string file_path;               // File Path
    string sha;                     // Sha of file
    long long int total_chunks;     // Total no of chunks
    long long int total_size;       // File size
    vector<string> chunks_I_have;   // Which chunks user has
    long long int no_of_chunks_I_have;  // no of chunks user has
};

// all files user has : <sha,obj of filestructur>
unordered_map<string,FilesStructure> filesIHave;
```
#### File Progress Track :
```c
// When file start downloading
// <sha,{group_id,file_name}>
unordered_map<string,pair<string,string>> downloadStart;
// When any one of the chunk got downloaded
// <sha,{group_id,file_name}>
unordered_map<string,pair<string,string>> downloadPending;
// When file downloaded completely
// <sha,{group_id,file_name}>
vector<pair<string,string>> downloadComplete;
```

## Overview (Approach) 
- Tracker which is guide for every peer will handle all the requests related to commands from peer
- Peer who itself act as client and server.
- Tracker and peer both have tracker_info.txt file which contain tracker IP and port information.
- Peer will run two threads one for serving requests from other peer and one for to do communication with tracker.
- Peer will communicate to tracker and perform all tasks
- When peer want to download a file, it will request different different peer for different different chunks
- To achieve this in parallel manner and by keeping hardware support in hand I have user ***THREAD POOL***
- Rarest first algorithm is used for downloading less available chunks first.
- After this random selection algorithm is used from peers list to download other chunks
- For integrity check sha1 hash algorithm is used.
- It will check integrity of every chunk, if it fails then it will ask that chunk again.
- At the end whole file integrity is also checked.

### Commands and its implementation :
- **Create User Account :**
    - Peer send request to tracker. Tracker will store this information in user data structure and ask peer to go for login. 
- **Login :**
    - Peer must do login to run any of the command. Tracker will check whether client's user id and password matches or not.
    - Tracker will not allow multiple login from one terminal.
- **To run all below command peer must need to do login**
- **Create Group :**
    - Traccker will store the information of group created by peer and make that peer admin.
    - only this peer is allowd to accept new requests to this group.
- **Join Group :**
    - Peer can request to join in any of group. Tracker will store this information on its end.
- **Leave Group :**
    - Tracker will remove user form group data structure.
- **List pending join :**
    - Only admin of the group can run this command.
    - It will show all the peers info who has requested to join the group.
- **Accept Group Joining Request :**
    - Only admin can accept thr request of peer to join group.
    - when admin accept the user, in tracker data structure it will be moved to accepted list.
    - Only after this any peer can contribute or take benifit of that group.
- **List All Group In Network :**
    - Shows the list of all groups.
- **List All sharable Files In Group :**
    - Shows all files shared by that group users
- **Upload File:** 
    - Peer need to be part of this group to upload file in a group.
    - When peer do upload its information will be stored in peer's data structure and tracker will also marks in its data stuctre that this peer is available to share this file.
    - Along with file info peer will also share sha of that file.
- **Download File :**
    - Peer need to be part of that group to do download.
    - First request will be sent to tracker.
    - Trakcer will respond to peer with information that which other peers have this file and give their IP and port. Tracker will also share the sha of file.
    - Now client will connect to all this peers and ask them to share which chunks of the file thay have.
    - After reciving all the inforamtion client will apply rarest first algorithm.
    - And start asking for chunks form different different peer.
    - At time of reciving peer will also share the sha of that chunk, so client will check the integrity of that chunk and if any mistake founnd in data then client will do the process of downloading that chunk again.
    - Usinf lseek and mutex client will create file along with downloading chunks
    - After reciving all the chunks client will check the integrity of whole file.
- **Logout:**
    - isActive flag will be set to false at tracker end.User can do re login, but if user will do login from different IP and port its old file data will be removed.
- **Show downloads :**
    - Maintaining 3 structres to show download progress.
    - When client start downloading , file name will be put in 1st structure.
    - As soon as client recieve first chunk successfully, file name will be moved to 2nd structure, At this time client andd tracker both will store the information that ckient can also share this file form now on.
    - When whole file download completed it will be moved to 3rd structure.
- **Stop sharing :**
    - From tracker and client side file information will be removed form data structres.


## Execution (To run this file)
```shell
Tracker Terminal :
1. cd to tracker
2. g++ tracker.cpp -o tracker
3. ./tracker tracker_info.txt 1

Peer Terminal :
1. cd to client
2. g++ -o client client.cpp  -lssl -lcrypto
3. ./client 127.0.0.1:8001 tracker_info.txt
```

## Asumptions 
- Tracker is always up
- Whole file will be available in the network
- Before closing terminal peer will do logout
- tracker_info.txt with tracker information should be available to client and tracker
- Data stored on peer and tracker is not persistent,if any of them goes down all the data will be misplaced.
