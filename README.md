# URL Shortener Backend Simulator

A C++ backend simulator that mimics how services like Bitly or TinyURL work under the hood. It converts long URLs into short, unique codes, stores the mappings using hashing, and even runs a tiny local HTTP server so the generated short links actually redirect in a browser.

Built as a DSA mini project to apply **hashing (`unordered_map`)** for O(1) average-time storage and retrieval.

## Features

- 🔗 **Shorten** any long URL into a compact, unique short code
- 🔍 **Retrieve** the original URL from its short code
- 📋 **List** all stored URL mappings in a table
- 🌐 **Live redirects** — a local HTTP server actually resolves `http://localhost:8080/<code>` to the original URL (HTTP 302 redirect)
- ♻️ **Duplicate-safe** — shortening the same URL twice returns the same code instead of creating a new one
- 🔒 **Thread-safe** — the shared data structure is protected with a Windows critical section so the console loop and the redirect server can run concurrently

## How It Works

1. **Hashing** — `std::unordered_map<string, string>` stores two maps: long URL → short code, and short code → long URL, giving average O(1) insert and lookup.
2. **Code generation** — each long URL is hashed (`std::hash`) and the result is encoded into a short, 7-character alphanumeric string using **base-62 encoding** (`0–9`, `a–z`, `A–Z`).
3. **Local redirect server** — built with the **Winsock2** API. On startup, it scans ports `8080`–`8090`, binds to the first free one, and listens for incoming HTTP requests, parsing the short code from the request path and responding with a `302 Found` redirect (or `404` if the code is unknown).
4. **Concurrency** — the server runs on its own thread (`CreateThread`) while the main thread handles the console menu; both share the `UrlShortener` instance safely via a `CRITICAL_SECTION`.

## Tech Stack

| Component        | Technology                          |
|-------------------|--------------------------------------|
| Language          | C++17                                |
| Core data structure | `std::unordered_map` (hashing)     |
| Networking         | Winsock2 (`winsock2.h`, `ws2tcpip.h`) |
| Concurrency        | Windows threads + critical sections |
| Compiler           | g++ (MinGW)                          |
| Platform           | Windows                              |

## Getting Started

### Prerequisites
- Windows OS
- `g++` (MinGW) with C++17 support

### Build
```bash
g++ main.cpp -std=c++17 -Wall -Wextra -o url_shortener.exe -lws2_32
```

### Run
```bash
.\url_shortener.exe
```

## Usage

On launch, the program starts the local redirect server and shows a menu:

```
URL Shortener Backend Simulator
1. Shorten a long URL
2. Retrieve original URL using short code
3. Display all mappings
4. Exit
Enter your choice:
```

**Example session:**

```
Enter your choice: 1
Enter the long URL: https://www.example.com/some/very/long/path
Short code generated: TxrXD00
Short URL: http://localhost:8080/TxrXD00

Enter your choice: 2
Enter the short code: TxrXD00
Short URL: http://localhost:8080/TxrXD00
Original URL: https://www.example.com/some/very/long/path

Enter your choice: 3
Stored URL Mappings
Short URL                          Original URL
--------------------------------------------------------------
http://localhost:8080/TxrXD00      https://www.example.com/some/very/long/path
```

Paste the generated short URL into a browser while the program is running to see the live redirect in action.

## Project Structure

```
.
├── main.cpp     # Full implementation: hashing, encoding, HTTP server, CLI
└── README.md
```

## Possible Improvements

- Persist mappings to a file or database (currently in-memory only)
- Add URL expiry / TTL support
- Add click-analytics per short code
- Cross-platform networking (replace Winsock with a portable socket library)

## License

This project is for educational purposes as part of a DSA mini project submission.
