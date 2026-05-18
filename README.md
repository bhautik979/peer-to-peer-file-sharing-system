# 🗂️ Distributed P2P File Sharing System

A **BitTorrent-inspired** peer-to-peer file sharing system built in C++, featuring a centralized tracker, group-based access control, parallel chunk downloading, and SHA-1 integrity verification.

---

## 🔴 Problem Statement

Traditional file transfer methods (FTP, HTTP) rely on a single server, creating:
- **Single points of failure** — if the server goes down, transfers stop
- **Bandwidth bottlenecks** — all clients pull from one source
- **No resilience** — partially downloaded files are often unrecoverable

This project solves these issues by distributing file storage across multiple peers in a network, allowing simultaneous chunk-based downloads from several sources, with group-based authorization controlling who can share or fetch files.

---

## 🏗️ Architecture

```
                        ┌─────────────────────────────────┐
                        │             TRACKER              │
                        │  - User Registry (login/auth)    │
                        │  - Group Management              │
                        │  - File Metadata & SHA Registry  │
                        │  - Peer-to-file mapping          │
                        └───────────┬─────────────┬────────┘
                                    │             │
                    ┌───────────────┘             └───────────────┐
                    ▼                                             ▼
          ┌─────────────────┐                         ┌─────────────────┐
          │    PEER A        │◄────── Chunk Req ──────►│    PEER B        │
          │  (Client+Server) │                         │  (Client+Server) │
          │  Thread Pool     │                         │  Thread Pool     │
          │  Rarest First    │                         │  Rarest First    │
          └────────┬────────┘                         └────────┬────────┘
                   │                                           │
                   └──────────────────┬────────────────────────┘
                                      ▼
                             ┌─────────────────┐
                             │    PEER C        │
                             │  (Downloader)    │
                             │  SHA-1 Verify    │
                             │  per-chunk       │
                             └─────────────────┘
```

### How it Works

| Layer | Component | Responsibility |
|---|---|---|
| **Coordination** | Tracker | Maintains user, group, and file-peer mappings |
| **Data Plane** | Peer (as Server) | Serves file chunks to requesting peers |
| **Control Plane** | Peer (as Client) | Discovers peers, downloads chunks in parallel |
| **Integrity** | SHA-1 | Verifies every chunk and the final assembled file |
| **Scheduling** | Rarest-First + Random | Prioritizes less-available chunks, then random selection |

### Download Flow

```
Peer C wants file "EP1.mkv" from group g1
  │
  ├─ 1. Ask Tracker → returns [Peer A, Peer B] + SHA of file
  │
  ├─ 2. Query Peer A & Peer B → "which chunks do you have?"
  │
  ├─ 3. Apply Rarest-First algorithm → schedule chunk requests
  │
  ├─ 4. Thread Pool dispatches chunk downloads concurrently
  │       ├─ Chunk 0 ← Peer A  (verify SHA-1 ✓)
  │       ├─ Chunk 1 ← Peer B  (verify SHA-1 ✓)
  │       └─ Chunk N ← Peer A  (verify SHA-1 ✓)
  │
  └─ 5. Assemble file → verify full-file SHA-1 ✓ → done
```

---

## 🛠️ Tech Stack

| Technology | Usage |
|---|---|
| **C++17** | Core language for both tracker and client |
| **POSIX Sockets** | TCP communication between peers and tracker |
| **OpenSSL (libcrypto)** | SHA-1 hashing for chunk & file integrity |
| **POSIX Threads (pthreads)** | Thread pool for parallel chunk downloads |
| **`lseek` + Mutex** | Concurrent file writes without corruption |
| **`unordered_map`** | O(1) lookup for users, groups, and files |

---

## 📁 Project Structure

```
Project/
├── tracker/
│   ├── tracker.cpp          # Tracker server — handles all peer requests
│   ├── tracker_info.txt     # Tracker IP and port configuration
│   └── tracker              # Compiled tracker binary
│
├── client/
│   ├── client.cpp           # Peer — acts as both client and server
│   ├── tracker_info.txt     # Tracker IP and port (same config)
│   └── client               # Compiled client binary
│
├── command.txt              # Quick-reference example commands
├── TEST_CASES.md            # Test scenarios and expected outputs
└── README.md
```

---

## ⚙️ Key Features

- **Group-Based Access Control** — Users must join and be approved by a group admin before sharing/downloading
- **Parallel Chunk Downloads** — Thread pool fetches different chunks from different peers simultaneously
- **Rarest-First Scheduling** — Ensures the least-available chunks are prioritized, improving swarm health
- **Per-Chunk SHA-1 Verification** — Corrupted chunks are re-downloaded automatically
- **Full-File Integrity Check** — Final SHA-1 hash verified after all chunks are assembled
- **Partial Sharing** — A peer can share chunks it has even before the full download completes
- **Download Progress Tracking** — Three-state tracker: `started → pending (partial) → complete`

---

## 🚀 How to Run Locally

### Prerequisites

- **Linux / WSL / Ubuntu**
- `g++` with C++17 support
- `libssl-dev` (OpenSSL)

```bash
sudo apt update
sudo apt install g++ libssl-dev
```

---

### Step 1 — Configure the Tracker Info

Both `tracker/tracker_info.txt` and `client/tracker_info.txt` must contain the tracker's IP and port.

**`tracker_info.txt` format:**
```
127.0.0.1
6000
```

> Make sure both files match before starting.

---

### Step 2 — Start the Tracker

```bash
# Terminal 1
cd tracker
g++ -o tracker tracker.cpp
./tracker tracker_info.txt 1
```

> The `1` is the tracker instance ID (used when running multiple trackers).

---

### Step 3 — Start a Peer

```bash
# Terminal 2 (Peer A on port 8001)
cd client
g++ -o client client.cpp -lssl -lcrypto
./client 127.0.0.1:8001 tracker_info.txt
```

Repeat in new terminals for more peers, each on a different port (e.g., `127.0.0.1:8002`, `127.0.0.1:8003`).

---

### Step 4 — Example Session

```bash
# --- Peer A (Terminal 2) ---
create_user alice password123
login alice password123
create_group g1
upload_file /path/to/EP1.mkv g1

# --- Peer B (Terminal 3) ---
create_user bob password123
login bob password123
join_group g1

# --- Peer A (Terminal 2) — accept Bob ---
list_requests g1
accept_request g1 bob

# --- Peer B (Terminal 3) ---
list_files g1
download_file g1 EP1.mkv /path/to/output/dir

# Check download progress
show_downloads
```

---

## 📋 Supported Commands

| Command | Description |
|---|---|
| `create_user <id> <pass>` | Register a new user |
| `login <id> <pass>` | Authenticate with the tracker |
| `create_group <group_id>` | Create a group (caller becomes admin) |
| `join_group <group_id>` | Request to join a group |
| `leave_group <group_id>` | Leave a group |
| `list_requests <group_id>` | View pending join requests (admin only) |
| `accept_request <group_id> <user_id>` | Accept a join request (admin only) |
| `list_groups` | List all groups in the network |
| `list_files <group_id>` | List all shared files in a group |
| `upload_file <filepath> <group_id>` | Share a file with a group |
| `download_file <group_id> <filename> <dest>` | Download a file from a group |
| `show_downloads` | Display download progress |
| `stop_share <group_id> <filename>` | Remove a file from sharing |
| `logout` | Log out from the tracker |

---

## ⚠️ Assumptions

- The tracker is always running and reachable
- The complete file is available across the peer swarm collectively
- Peers perform a clean `logout` before exiting their terminal
- `tracker_info.txt` is present in both `tracker/` and `client/` directories
- **State is in-memory only** — restarting the tracker or any peer clears all data

---

## 🔒 Data Structures at a Glance

<details>
<summary><strong>Tracker-side</strong></summary>

```cpp
// File metadata registry
unordered_map<string, fileInfo> files;          // <file_name, fileInfo>

// User registry
unordered_map<string, User> users;              // <user_id, User>

// Group registry
unordered_map<string, Group> groups;            // <group_id, Group>
```

</details>

<details>
<summary><strong>Peer-side</strong></summary>

```cpp
// Files this peer owns
unordered_map<string, FilesStructure> filesIHave;   // <sha, FilesStructure>

// Download progress tracking
unordered_map<string, pair<string,string>> downloadStart;    // Started
unordered_map<string, pair<string,string>> downloadPending;  // Partial (seeding)
vector<pair<string,string>>               downloadComplete;  // Done
```

</details>
