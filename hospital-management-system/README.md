# MediCare HMS - Hospital Management System

A complete hospital management web application.

- **Backend:** C++17 (single file, no external libraries). It is both a REST API server and a static file server, built on POSIX sockets with one thread per connection.
- **Frontend:** plain HTML, CSS and JavaScript (no frameworks, no build step).
- **Storage:** tab-separated text files in `data/`, created automatically.

## Features

| Module | What you can do |
|---|---|
| Dashboard | Totals, today's and upcoming appointments, recent patients, revenue and pending payments |
| Patients | Register, edit, delete, search. Deleting a patient also removes their appointments and bills |
| Doctors | Add, edit, set available/unavailable. A doctor with appointment records cannot be deleted |
| Appointments | Book, edit, complete, cancel. Prevents double-booking a doctor and booking unavailable doctors |
| Billing | Create bills, mark paid, track revenue vs. pending amounts |

## Project structure

```
hospital-management-system/
├── backend/main.cpp      C++ server: HTTP, JSON, validation, persistence, REST API
├── frontend/
│   ├── index.html        App shell
│   ├── style.css         Styling (responsive)
│   └── app.js            UI logic, calls the API with fetch()
├── data/                 Created at first run (patients.tsv, doctors.tsv, ...)
├── build.bat             Windows: compile hms.exe
├── run.bat               Windows: build if needed, start server, open browser
├── Makefile              Linux/macOS build
└── README.md
```

## Build and run on Windows

You need a C++17 compiler. Pick one:

**Option A: MSYS2 / MinGW-w64 (recommended)**
1. Install MSYS2 from <https://www.msys2.org>.
2. Open the **MSYS2 UCRT64** shell and run: `pacman -S mingw-w64-ucrt-x86_64-gcc`
3. Add `C:\msys64\ucrt64\bin` to your Windows PATH, then open a **new** Command Prompt.

**Option B: Visual Studio Build Tools** with the "Desktop development with C++" workload. Run the scripts from the **x64 Native Tools Command Prompt for VS**.

Then, in the project folder, just double-click **`run.bat`** (or run it from Command Prompt). It builds `hms.exe` if needed, starts the server and opens <http://localhost:8080>.

Manual commands, if you prefer:

```bat
:: MinGW-w64
g++ -std=c++17 -O2 -pthread -o hms.exe backend\main.cpp -lws2_32

:: or Visual Studio
cl /EHsc /O2 /std:c++17 /Fe:hms.exe backend\main.cpp

hms.exe 8080 frontend data
```

If Windows Firewall asks about `hms.exe`, allow access on private networks. If port 8080 is busy, use another one, e.g. `hms.exe 9090 frontend data`, and open that port in your browser. Press Ctrl+C in the window to stop the server.

## Build and run on Linux / macOS

Requirements: `g++` (C++17) and `make`.

```bash
make          # compiles ./hms
make run      # starts the server on http://localhost:8080
```

Or manually:

```bash
g++ -std=c++17 -O2 -pthread -o hms backend/main.cpp
./hms 8080 frontend data       # arguments: port, frontend dir, data dir
```

Open <http://localhost:8080> in your browser. Three sample doctors are added on the very first run.

## REST API

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/patients` | List all patients |
| POST | `/api/patients` | Create a patient |
| GET | `/api/patients/{id}` | Get one patient |
| PUT | `/api/patients/{id}` | Update (partial updates allowed) |
| DELETE | `/api/patients/{id}` | Delete |
| GET | `/api/stats` | Dashboard statistics |

The same endpoints exist for `doctors`, `appointments` and `bills`.

Examples:

```bash
curl -X POST localhost:8080/api/patients -H 'Content-Type: application/json' \
  -d '{"name":"Ravi Kumar","age":34,"gender":"Male","phone":"9876543210","disease":"Fever"}'

curl -X POST localhost:8080/api/appointments -H 'Content-Type: application/json' \
  -d '{"patientId":1,"doctorId":1,"date":"2026-11-05","time":"10:30"}'

curl -X PUT localhost:8080/api/bills/1 -H 'Content-Type: application/json' -d '{"paid":true}'
```

Errors are returned as `{"error":"message"}` with status 400 (validation), 404 (not found) or 409 (conflict).

## Notes and possible extensions

This is a learning/demo project. Before real-world use you would want to add:

- User login and role-based access (admin, doctor, receptionist) with password hashing
- HTTPS (for example behind nginx) and stronger request limits
- A real database (SQLite or PostgreSQL) instead of flat files
- Medical records, prescriptions, wards/beds and pharmacy inventory modules
